// Copyright 2021 Google LLC.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "experimental/ngatoy/Cmds.h"
#include "experimental/ngatoy/Fake.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkGraphics.h"
#include "include/gpu/GrDirectContext.h"
#include "src/core/SkOSFile.h"
#include "src/gpu/GrCaps.h"
#include "src/gpu/GrDirectContextPriv.h"
#include "src/utils/SkOSPath.h"
#include "tools/ToolUtils.h"
#include "tools/flags/CommandLineFlags.h"
#include "tools/gpu/GrContextFactory.h"

#include <algorithm>

/*
 * Questions this is trying to answer:
 *   How to handle saveLayers (in w/ everything or separate)
 *   How to handle blurs & other off screen draws
 *   How to handle clipping
 *   How does sorting stack up against buckets
 *   How does creating batches interact w/ the sorting
 *   How does batching work w/ text
 *   How does text (esp. atlasing) work at all
 *   Batching quality vs. existing
 *   Memory churn/overhead vs existing (esp. wrt batching)
 *   gpu vs cpu boundedness
 *
 * Futher Questions:
 *   How can we collect uniforms & not store the fps -- seems complicated
 *   Do all the blend modes (esp. advanced work front-to-back)?
 *   NGA perf vs. OGA perf
 *   Can we prepare any of the saveLayers or off-screen draw render passes in parallel?
 *
 * Small potatoes:
 *   Incorporate CTM into the simulator
 */

/*
 * How does this all work:
 *
 * Each test is specified by a set of RectCmds (which have a unique ID and carry their material
 * and MC state info) along with the order they are expected to be drawn in with the NGA.
 *
 * To generate an expected image, the RectCmds are replayed into an SkCanvas in the order
 * provided.
 *
 * For the actual (NGA) image, the RectCmds are replayed into a FakeCanvas - preserving the
 * unique ID of the RectCmd. The FakeCanvas creates new RectCmd objects, sorts them using
 * the SortKey and then performs a kludgey z-buffered rasterization. The FakeCanvas also
 * preserves the RectCmd order it ultimately used for its rendering and this can be compared
 * with the expected order from the test.
 *
 * The use of the RectCmds to create the tests is a mere convenience to avoid creating a
 * separate representation of the desired draws.
 *
 ***************************
 * Here are some of the simplifying assumptions of this simulation (and their justification):
 *
 * Only SkIRects are used for draws and clips - since MSAA should be taking care of AA for us in
 * the NGA we don't really need SkRects. This also greatly simplifies the z-buffered rasterization.
 *
 **************************
 * Areas for improvement:
 *   We should add strokes since there are two distinct drawing methods in the NGA (fill v. stroke)
 */

using sk_gpu_test::GrContextFactory;

static DEFINE_string2(writePath, w, "", "If set, write bitmaps here as .pngs.");

static void exitf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    exit(1);
}

static void save_files(int testID, const SkBitmap& expected, const SkBitmap& actual) {
    if (FLAGS_writePath.isEmpty()) {
        return;
    }

    const char* dir = FLAGS_writePath[0];

    SkString path = SkOSPath::Join(dir, "expected");
    path.appendU32(testID);
    path.append(".png");

    if (!sk_mkdir(dir)) {
        exitf("failed to create directory for png \"%s\"", path.c_str());
    }
    if (!ToolUtils::EncodeImageToFile(path.c_str(), expected, SkEncodedImageFormat::kPNG, 100)) {
        exitf("failed to save png to \"%s\"", path.c_str());
    }

    path = SkOSPath::Join(dir, "actual");
    path.appendU32(testID);
    path.append(".png");

    if (!ToolUtils::EncodeImageToFile(path.c_str(), actual, SkEncodedImageFormat::kPNG, 100)) {
        exitf("failed to save png to \"%s\"", path.c_str());
    }
}

// Exercise basic SortKey behavior
static void key_test() {
    SortKey k;
    SkASSERT(!k.transparent());
    SkASSERT(k.clipID() == 0);
    SkASSERT(k.depth() == 0);
    SkASSERT(k.material() == 0);
//    k.dump();

    SortKey k1(false, 4, 1, 3);
    SkASSERT(!k1.transparent());
    SkASSERT(k1.clipID() == 4);
    SkASSERT(k1.depth() == 1);
    SkASSERT(k1.material() == 3);
//    k1.dump();

    SortKey k2(true, 7, 2, 1);
    SkASSERT(k2.transparent());
    SkASSERT(k2.clipID() == 7);
    SkASSERT(k2.depth() == 2);
    SkASSERT(k2.material() == 1);
//    k2.dump();
}

static void check_state(FakeMCBlob* actualState,
                        SkIPoint expectedCTM,
                        const std::vector<SkIRect>& expectedClips) {
    SkASSERT(actualState->ctm() == expectedCTM);

    int i = 0;
    auto states = actualState->mcStates();
    for (auto& s : states) {
        for (auto r : s.rects()) {
            SkAssertResult(i < (int) expectedClips.size());
            SkAssertResult(r == expectedClips[i]);
            i++;
        }
    }
}

// Exercise the FakeMCBlob object
static void mcstack_test() {
    const SkIRect r { 0, 0, 10, 10 };
    const SkIPoint s1Trans { 10, 10 };
    const SkIPoint s2TransA { -5, -2 };
    const SkIPoint s2TransB { -3, -1 };

    const std::vector<SkIRect> expectedS0Clips;
    const std::vector<SkIRect> expectedS1Clips {
        r.makeOffset(s1Trans)
    };
    const std::vector<SkIRect> expectedS2aClips {
        r.makeOffset(s1Trans),
        r.makeOffset(s2TransA)
    };
    const std::vector<SkIRect> expectedS2bClips {
        r.makeOffset(s1Trans),
        r.makeOffset(s2TransA),
        r.makeOffset(s2TransA + s2TransB)
    };

    //----------------
    FakeStateTracker s;

    auto state0 = s.snapState();
    // The initial state should have no translation & no clip
    check_state(state0.get(), { 0, 0 }, expectedS0Clips);

    //----------------
    s.push();
    s.translate(s1Trans);
    s.clipRect(r);

    auto state1 = s.snapState();
    check_state(state1.get(), s1Trans, expectedS1Clips);

    //----------------
    s.push();
    s.translate(s2TransA);
    s.clipRect(r);

    auto state2a = s.snapState();
    check_state(state2a.get(), s1Trans + s2TransA, expectedS2aClips);

    s.translate(s2TransB);
    s.clipRect(r);

    auto state2b = s.snapState();
    check_state(state2b.get(), s1Trans + s2TransA + s2TransB, expectedS2bClips);
    SkASSERT(state2a != state2b);

    //----------------
    s.pop();
    auto state3 = s.snapState();
    check_state(state3.get(), s1Trans, expectedS1Clips);
    SkASSERT(state1 == state3);

    //----------------
    s.pop();
    auto state4 = s.snapState();
    check_state(state4.get(), { 0, 0 }, expectedS0Clips);
    SkASSERT(state0 == state4);
}

static void check_order(const std::vector<int>& actualOrder,
                        const std::vector<int>& expectedOrder) {
    if (expectedOrder.size() != actualOrder.size()) {
        exitf("Op count mismatch. Expected %d - got %d\n",
              expectedOrder.size(),
              actualOrder.size());
    }

    if (expectedOrder != actualOrder) {
        SkDebugf("order mismatch:\n");
        SkDebugf("E %d: ", expectedOrder.size());
        for (auto t : expectedOrder) {
            SkDebugf("%d", t);
        }
        SkDebugf("\n");

        SkDebugf("A %d: ", actualOrder.size());
        for (auto t : actualOrder) {
            SkDebugf("%d", t);
        }
        SkDebugf("\n");
    }
}

typedef int (*PFTest)(std::vector<const Cmd*>* test, std::vector<int>* expectedOrder);

static void sort_test(PFTest testcase) {
    std::vector<const Cmd*> test;
    std::vector<int> expectedOrder;
    int testID = testcase(&test, &expectedOrder);


    SkBitmap expectedBM;
    expectedBM.allocPixels(SkImageInfo::MakeN32Premul(256, 256));
    expectedBM.eraseColor(SK_ColorBLACK);
    SkCanvas real(expectedBM);

    SkBitmap actualBM;
    actualBM.allocPixels(SkImageInfo::MakeN32Premul(256, 256));
    actualBM.eraseColor(SK_ColorBLACK);

    FakeCanvas fake(actualBM);
    const FakeMCBlob* prior = nullptr;
    for (auto c : test) {
        c->execute(&fake);
        c->execute(&real, prior);
        prior = c->state();
    }

    fake.finalize();

    std::vector<int> actualOrder = fake.getOrder();
    check_order(actualOrder, expectedOrder);

    save_files(testID, expectedBM, actualBM);
}

// Simple test - green rect should appear atop the red rect
static int test1(std::vector<const Cmd*>* test, std::vector<int>* expectedOrder) {
    // front-to-back order bc all opaque
    expectedOrder->push_back(1);
    expectedOrder->push_back(0);

    FakeStateTracker s;
    sk_sp<FakeMCBlob> state = s.snapState();

    SkIRect r{0, 0, 100, 100};
    test->push_back(new RectCmd(0, kSolidMat, r.makeOffset(8, 8),   SK_ColorRED,   SK_ColorUNUSED, state));
    test->push_back(new RectCmd(1, kSolidMat, r.makeOffset(48, 48), SK_ColorGREEN, SK_ColorUNUSED, state));
    return 1;
}

// Simple test - blue rect atop green rect atop red rect
static int test2(std::vector<const Cmd*>* test, std::vector<int>* expectedOrder) {
    // front-to-back order bc all opaque
    expectedOrder->push_back(2);
    expectedOrder->push_back(1);
    expectedOrder->push_back(0);

    FakeStateTracker s;
    sk_sp<FakeMCBlob> state = s.snapState();

    SkIRect r{0, 0, 100, 100};
    test->push_back(new RectCmd(0, kSolidMat, r.makeOffset(8, 8),   SK_ColorRED,   SK_ColorUNUSED, state));
    test->push_back(new RectCmd(1, kSolidMat, r.makeOffset(48, 48), SK_ColorGREEN, SK_ColorUNUSED, state));
    test->push_back(new RectCmd(2, kSolidMat, r.makeOffset(98, 98), SK_ColorBLUE,  SK_ColorUNUSED, state));
    return 2;
}

// Transparency test - opaque blue rect atop transparent green rect atop opaque red rect
static int test3(std::vector<const Cmd*>* test, std::vector<int>* expectedOrder) {
    // opaque draws are first and are front-to-back. Transparent draw is last.
    expectedOrder->push_back(2);
    expectedOrder->push_back(0);
    expectedOrder->push_back(1);

    FakeStateTracker s;
    sk_sp<FakeMCBlob> state = s.snapState();

    SkIRect r{0, 0, 100, 100};
    test->push_back(new RectCmd(0, kSolidMat, r.makeOffset(8, 8),   SK_ColorRED,  SK_ColorUNUSED, state));
    test->push_back(new RectCmd(1, kSolidMat, r.makeOffset(48, 48), 0x8000FF00,   SK_ColorUNUSED, state));
    test->push_back(new RectCmd(2, kSolidMat, r.makeOffset(98, 98), SK_ColorBLUE, SK_ColorUNUSED, state));
    return 3;
}

// Multi-transparency test - transparent blue rect atop transparent green rect atop
// transparent red rect
static int test4(std::vector<const Cmd*>* test, std::vector<int>* expectedOrder) {
    // All in back-to-front order bc they're all transparent
    expectedOrder->push_back(0);
    expectedOrder->push_back(1);
    expectedOrder->push_back(2);

    FakeStateTracker s;
    sk_sp<FakeMCBlob> state = s.snapState();

    SkIRect r{0, 0, 100, 100};
    test->push_back(new RectCmd(0, kSolidMat, r.makeOffset(8, 8),   0x80FF0000, SK_ColorUNUSED, state));
    test->push_back(new RectCmd(1, kSolidMat, r.makeOffset(48, 48), 0x8000FF00, SK_ColorUNUSED, state));
    test->push_back(new RectCmd(2, kSolidMat, r.makeOffset(98, 98), 0x800000FF, SK_ColorUNUSED, state));
    return 4;
}

// Multiple opaque materials test
// All opaque:
//   normal1, linear1, radial1, normal2, linear2, radial2
// Which gets sorted to:
//   normal2, normal1, linear2, linear1, radial2, radial1
// So, front to back w/in each material type.
static int test5(std::vector<const Cmd*>* test, std::vector<int>* expectedOrder) {
    // Note: This pushes sorting by material above sorting by Z. Thus we'll get less front to
    // back benefit.
    expectedOrder->push_back(3);
    expectedOrder->push_back(0);
    expectedOrder->push_back(4);
    expectedOrder->push_back(1);
    expectedOrder->push_back(5);
    expectedOrder->push_back(2);

    FakeStateTracker s;
    sk_sp<FakeMCBlob> state = s.snapState();

    SkIRect r{0, 0, 100, 100};
    test->push_back(new RectCmd(0, kSolidMat,  r.makeOffset(8, 8),     SK_ColorRED,     SK_ColorUNUSED, state));
    test->push_back(new RectCmd(1, kLinearMat, r.makeOffset(48, 48),   SK_ColorGREEN,   SK_ColorWHITE,  state));
    test->push_back(new RectCmd(2, kRadialMat, r.makeOffset(98, 98),   SK_ColorBLUE,    SK_ColorBLACK,  state));
    test->push_back(new RectCmd(3, kSolidMat,  r.makeOffset(148, 148), SK_ColorCYAN,    SK_ColorUNUSED, state));
    test->push_back(new RectCmd(4, kLinearMat, r.makeOffset(148, 8),   SK_ColorMAGENTA, SK_ColorWHITE,  state));
    test->push_back(new RectCmd(5, kRadialMat, r.makeOffset(8, 148),   SK_ColorYELLOW,  SK_ColorBLACK,  state));
    return 5;
}

// simple clipping test - 1 clip rect
static int test6(std::vector<const Cmd*>* test, std::vector<int>* expectedOrder) {
    expectedOrder->push_back(1);
    expectedOrder->push_back(0);

    FakeStateTracker s;
    s.clipRect(SkIRect::MakeXYWH(28, 28, 40, 40));

    sk_sp<FakeMCBlob> state = s.snapState();

    SkIRect r{0, 0, 100, 100};
    test->push_back(new RectCmd(0, kSolidMat, r.makeOffset(8, 8),   SK_ColorRED,   SK_ColorUNUSED, state));
    test->push_back(new RectCmd(1, kSolidMat, r.makeOffset(48, 48), SK_ColorGREEN, SK_ColorUNUSED, state));
    return 6;
}

// more complicated clipping w/ opaque draws -> should reorder
static int test7(std::vector<const Cmd*>* test, std::vector<int>* expectedOrder) {
    // The expected is front to back modulated by the two clip states
    expectedOrder->push_back(5);
    expectedOrder->push_back(4);
    expectedOrder->push_back(1);
    expectedOrder->push_back(0);

    expectedOrder->push_back(3);
    expectedOrder->push_back(2);

    FakeStateTracker s;
    s.clipRect(SkIRect::MakeXYWH(85, 0, 86, 256));  // select the middle third in x

    sk_sp<FakeMCBlob> state = s.snapState();

    SkIRect r{0, 0, 100, 100};
    test->push_back(new RectCmd(0, kSolidMat, r.makeOffset(8, 8),     SK_ColorRED,     SK_ColorUNUSED, state));
    test->push_back(new RectCmd(1, kSolidMat, r.makeOffset(48, 48),   SK_ColorGREEN,   SK_ColorUNUSED, state));

    s.push();
    s.clipRect(SkIRect::MakeXYWH(0, 85, 256, 86));  // intersect w/ the middle third in y
    state = s.snapState();

    test->push_back(new RectCmd(2, kSolidMat, r.makeOffset(98, 98),   SK_ColorBLUE,    SK_ColorUNUSED, state));
    test->push_back(new RectCmd(3, kSolidMat, r.makeOffset(148, 148), SK_ColorCYAN,    SK_ColorUNUSED, state));

    s.pop();
    state = s.snapState();

    test->push_back(new RectCmd(4, kSolidMat, r.makeOffset(148, 8),   SK_ColorMAGENTA, SK_ColorUNUSED, state));
    test->push_back(new RectCmd(5, kSolidMat, r.makeOffset(8, 148),   SK_ColorYELLOW,  SK_ColorUNUSED, state));
    return 7;
}


int main(int argc, char** argv) {
    CommandLineFlags::Parse(argc, argv);

    SkGraphics::Init();

    key_test();
    mcstack_test();
    sort_test(test1);
    sort_test(test2);
    sort_test(test3);
    sort_test(test4);
    sort_test(test5);
    sort_test(test6);
    sort_test(test7);

    return 0;
}
