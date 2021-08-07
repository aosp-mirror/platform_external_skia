/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkCanvas.h"
#include "include/core/SkClipOp.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPath.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkRegion.h"
#include "include/core/SkScalar.h"
#include "include/core/SkSize.h"
#include "include/core/SkString.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypes.h"
#include "include/effects/SkGradientShader.h"
#include "include/gpu/GrConfig.h"
#include "include/gpu/GrDirectContext.h"
#include "include/private/GrResourceKey.h"
#include "include/private/SkTemplates.h"
#include "include/utils/SkRandom.h"
#include "src/core/SkClipStack.h"
#include "src/core/SkTLList.h"
#include "src/gpu/GrClip.h"
#include "src/gpu/GrDirectContextPriv.h"
#include "src/gpu/GrResourceCache.h"
#include "src/gpu/GrTexture.h"
#include "src/gpu/GrTextureProxy.h"
#include "tests/Test.h"
#include "tools/gpu/GrContextFactory.h"

#include <cstring>
#include <initializer_list>
#include <new>

class GrCaps;

static void test_assign_and_comparison(skiatest::Reporter* reporter) {
    SkClipStack s;
    bool doAA = false;

    REPORTER_ASSERT(reporter, 0 == s.getSaveCount());

    // Build up a clip stack with a path, an empty clip, and a rect.
    s.save();
    REPORTER_ASSERT(reporter, 1 == s.getSaveCount());

    SkPath p;
    p.moveTo(5, 6);
    p.lineTo(7, 8);
    p.lineTo(5, 9);
    p.close();
    s.clipPath(p, SkMatrix::I(), SkClipOp::kIntersect, doAA);

    s.save();
    REPORTER_ASSERT(reporter, 2 == s.getSaveCount());

    SkRect r = SkRect::MakeLTRB(1, 2, 103, 104);
    s.clipRect(r, SkMatrix::I(), SkClipOp::kIntersect, doAA);
    r = SkRect::MakeLTRB(4, 5, 56, 57);
    s.clipRect(r, SkMatrix::I(), SkClipOp::kIntersect, doAA);

    s.save();
    REPORTER_ASSERT(reporter, 3 == s.getSaveCount());

    r = SkRect::MakeLTRB(14, 15, 16, 17);
    s.clipRect(r, SkMatrix::I(), SkClipOp::kDifference, doAA);

    // Test that assignment works.
    SkClipStack copy = s;
    REPORTER_ASSERT(reporter, s == copy);

    // Test that different save levels triggers not equal.
    s.restore();
    REPORTER_ASSERT(reporter, 2 == s.getSaveCount());
    REPORTER_ASSERT(reporter, s != copy);

    // Test that an equal, but not copied version is equal.
    s.save();
    REPORTER_ASSERT(reporter, 3 == s.getSaveCount());
    r = SkRect::MakeLTRB(14, 15, 16, 17);
    s.clipRect(r, SkMatrix::I(), SkClipOp::kDifference, doAA);
    REPORTER_ASSERT(reporter, s == copy);

    // Test that a different op on one level triggers not equal.
    s.restore();
    REPORTER_ASSERT(reporter, 2 == s.getSaveCount());
    s.save();
    REPORTER_ASSERT(reporter, 3 == s.getSaveCount());
    r = SkRect::MakeLTRB(14, 15, 16, 17);
    s.clipRect(r, SkMatrix::I(), SkClipOp::kIntersect, doAA);
    REPORTER_ASSERT(reporter, s != copy);

    // Test that version constructed with rect-path rather than a rect is still considered equal.
    s.restore();
    s.save();
    SkPath rp;
    rp.addRect(r);
    s.clipPath(rp, SkMatrix::I(), SkClipOp::kDifference, doAA);
    REPORTER_ASSERT(reporter, s == copy);

    // Test that different rects triggers not equal.
    s.restore();
    REPORTER_ASSERT(reporter, 2 == s.getSaveCount());
    s.save();
    REPORTER_ASSERT(reporter, 3 == s.getSaveCount());

    r = SkRect::MakeLTRB(24, 25, 26, 27);
    s.clipRect(r, SkMatrix::I(), SkClipOp::kDifference, doAA);
    REPORTER_ASSERT(reporter, s != copy);

    s.restore();
    REPORTER_ASSERT(reporter, 2 == s.getSaveCount());

    copy.restore();
    REPORTER_ASSERT(reporter, 2 == copy.getSaveCount());
    REPORTER_ASSERT(reporter, s == copy);
    s.restore();
    REPORTER_ASSERT(reporter, 1 == s.getSaveCount());
    copy.restore();
    REPORTER_ASSERT(reporter, 1 == copy.getSaveCount());
    REPORTER_ASSERT(reporter, s == copy);

    // Test that different paths triggers not equal.
    s.restore();
    REPORTER_ASSERT(reporter, 0 == s.getSaveCount());
    s.save();
    REPORTER_ASSERT(reporter, 1 == s.getSaveCount());

    p.addRect(r);
    s.clipPath(p, SkMatrix::I(), SkClipOp::kIntersect, doAA);
    REPORTER_ASSERT(reporter, s != copy);
}

static void assert_count(skiatest::Reporter* reporter, const SkClipStack& stack,
                         int count) {
    SkClipStack::B2TIter iter(stack);
    int counter = 0;
    while (iter.next()) {
        counter += 1;
    }
    REPORTER_ASSERT(reporter, count == counter);
}

// Exercise the SkClipStack's bottom to top and bidirectional iterators
// (including the skipToTopmost functionality)
static void test_iterators(skiatest::Reporter* reporter) {
    SkClipStack stack;

    static const SkRect gRects[] = {
        { 0,   0,  40,  40 },
        { 60,  0, 100,  40 },
        { 0,  60,  40, 100 },
        { 60, 60, 100, 100 }
    };

    for (size_t i = 0; i < SK_ARRAY_COUNT(gRects); i++) {
        // the difference op will prevent these from being fused together
        stack.clipRect(gRects[i], SkMatrix::I(), SkClipOp::kDifference, false);
    }

    assert_count(reporter, stack, 4);

    // bottom to top iteration
    {
        const SkClipStack::Element* element = nullptr;

        SkClipStack::B2TIter iter(stack);
        int i;

        for (i = 0, element = iter.next(); element; ++i, element = iter.next()) {
            REPORTER_ASSERT(reporter, SkClipStack::Element::DeviceSpaceType::kRect ==
                                              element->getDeviceSpaceType());
            REPORTER_ASSERT(reporter, element->getDeviceSpaceRect() == gRects[i]);
        }

        SkASSERT(i == 4);
    }

    // top to bottom iteration
    {
        const SkClipStack::Element* element = nullptr;

        SkClipStack::Iter iter(stack, SkClipStack::Iter::kTop_IterStart);
        int i;

        for (i = 3, element = iter.prev(); element; --i, element = iter.prev()) {
            REPORTER_ASSERT(reporter, SkClipStack::Element::DeviceSpaceType::kRect ==
                                              element->getDeviceSpaceType());
            REPORTER_ASSERT(reporter, element->getDeviceSpaceRect() == gRects[i]);
        }

        SkASSERT(i == -1);
    }

    // skipToTopmost
    {
        const SkClipStack::Element* element = nullptr;

        SkClipStack::Iter iter(stack, SkClipStack::Iter::kBottom_IterStart);

        element = iter.skipToTopmost(SkClipOp::kDifference);
        REPORTER_ASSERT(reporter, SkClipStack::Element::DeviceSpaceType::kRect ==
                                          element->getDeviceSpaceType());
        REPORTER_ASSERT(reporter, element->getDeviceSpaceRect() == gRects[3]);
    }
}

// Exercise the SkClipStack's getConservativeBounds computation
static void test_bounds(skiatest::Reporter* reporter,
                        SkClipStack::Element::DeviceSpaceType primType) {
    static const int gNumCases = 8;
    static const SkRect gAnswerRectsBW[gNumCases] = {
        // A op B
        { 40, 40, 50, 50 },
        { 10, 10, 50, 50 },

        // invA op B
        { 40, 40, 80, 80 },
        { 0, 0, 100, 100 },

        // A op invB
        { 10, 10, 50, 50 },
        { 40, 40, 50, 50 },

        // invA op invB
        { 0, 0, 100, 100 },
        { 40, 40, 80, 80 },
    };

    static const SkClipOp gOps[] = {
        SkClipOp::kIntersect,
        SkClipOp::kDifference
    };

    SkRect rectA, rectB;

    rectA.setLTRB(10, 10, 50, 50);
    rectB.setLTRB(40, 40, 80, 80);

    SkRRect rrectA, rrectB;
    rrectA.setOval(rectA);
    rrectB.setRectXY(rectB, SkIntToScalar(1), SkIntToScalar(2));

    SkPath pathA, pathB;

    pathA.addRoundRect(rectA, SkIntToScalar(5), SkIntToScalar(5));
    pathB.addRoundRect(rectB, SkIntToScalar(5), SkIntToScalar(5));

    SkClipStack stack;
    SkRect devClipBound;
    bool isIntersectionOfRects = false;

    int testCase = 0;
    int numBitTests = SkClipStack::Element::DeviceSpaceType::kPath == primType ? 4 : 1;
    for (int invBits = 0; invBits < numBitTests; ++invBits) {
        for (size_t op = 0; op < SK_ARRAY_COUNT(gOps); ++op) {

            stack.save();
            bool doInvA = SkToBool(invBits & 1);
            bool doInvB = SkToBool(invBits & 2);

            pathA.setFillType(doInvA ? SkPathFillType::kInverseEvenOdd :
                                       SkPathFillType::kEvenOdd);
            pathB.setFillType(doInvB ? SkPathFillType::kInverseEvenOdd :
                                       SkPathFillType::kEvenOdd);

            switch (primType) {
                case SkClipStack::Element::DeviceSpaceType::kShader:
                case SkClipStack::Element::DeviceSpaceType::kEmpty:
                    SkDEBUGFAIL("Don't call this with kEmpty or kShader.");
                    break;
                case SkClipStack::Element::DeviceSpaceType::kRect:
                    stack.clipRect(rectA, SkMatrix::I(), SkClipOp::kIntersect, false);
                    stack.clipRect(rectB, SkMatrix::I(), gOps[op], false);
                    break;
                case SkClipStack::Element::DeviceSpaceType::kRRect:
                    stack.clipRRect(rrectA, SkMatrix::I(), SkClipOp::kIntersect, false);
                    stack.clipRRect(rrectB, SkMatrix::I(), gOps[op], false);
                    break;
                case SkClipStack::Element::DeviceSpaceType::kPath:
                    stack.clipPath(pathA, SkMatrix::I(), SkClipOp::kIntersect, false);
                    stack.clipPath(pathB, SkMatrix::I(), gOps[op], false);
                    break;
            }

            REPORTER_ASSERT(reporter, !stack.isWideOpen());
            REPORTER_ASSERT(reporter, SkClipStack::kWideOpenGenID != stack.getTopmostGenID());

            stack.getConservativeBounds(0, 0, 100, 100, &devClipBound,
                                        &isIntersectionOfRects);

            if (SkClipStack::Element::DeviceSpaceType::kRect == primType) {
                REPORTER_ASSERT(reporter, isIntersectionOfRects ==
                        (gOps[op] == SkClipOp::kIntersect));
            } else {
                REPORTER_ASSERT(reporter, !isIntersectionOfRects);
            }

            SkASSERT(testCase < gNumCases);
            REPORTER_ASSERT(reporter, devClipBound == gAnswerRectsBW[testCase]);
            ++testCase;

            stack.restore();
        }
    }
}

// Test out 'isWideOpen' entry point
static void test_isWideOpen(skiatest::Reporter* reporter) {
    {
        // Empty stack is wide open. Wide open stack means that gen id is wide open.
        SkClipStack stack;
        REPORTER_ASSERT(reporter, stack.isWideOpen());
        REPORTER_ASSERT(reporter, SkClipStack::kWideOpenGenID == stack.getTopmostGenID());
    }

    SkRect rectA, rectB;

    rectA.setLTRB(10, 10, 40, 40);
    rectB.setLTRB(50, 50, 80, 80);

    // Stack should initially be wide open
    {
        SkClipStack stack;

        REPORTER_ASSERT(reporter, stack.isWideOpen());
        REPORTER_ASSERT(reporter, SkClipStack::kWideOpenGenID == stack.getTopmostGenID());
    }

    // Test out empty difference from a wide open clip
    {
        SkClipStack stack;

        SkRect emptyRect;
        emptyRect.setEmpty();

        stack.clipRect(emptyRect, SkMatrix::I(), SkClipOp::kDifference, false);

        REPORTER_ASSERT(reporter, stack.isWideOpen());
        REPORTER_ASSERT(reporter, SkClipStack::kWideOpenGenID == stack.getTopmostGenID());
    }

    // Test out return to wide open
    {
        SkClipStack stack;

        stack.save();

        stack.clipRect(rectA, SkMatrix::I(), SkClipOp::kIntersect, false);

        REPORTER_ASSERT(reporter, !stack.isWideOpen());
        REPORTER_ASSERT(reporter, SkClipStack::kWideOpenGenID != stack.getTopmostGenID());

        stack.restore();

        REPORTER_ASSERT(reporter, stack.isWideOpen());
        REPORTER_ASSERT(reporter, SkClipStack::kWideOpenGenID == stack.getTopmostGenID());
    }
}

static int count(const SkClipStack& stack) {

    SkClipStack::Iter iter(stack, SkClipStack::Iter::kTop_IterStart);

    const SkClipStack::Element* element = nullptr;
    int count = 0;

    for (element = iter.prev(); element; element = iter.prev(), ++count) {
    }

    return count;
}

static void test_rect_inverse_fill(skiatest::Reporter* reporter) {
    // non-intersecting rectangles
    SkRect rect  = SkRect::MakeLTRB(0, 0, 10, 10);

    SkPath path;
    path.addRect(rect);
    path.toggleInverseFillType();
    SkClipStack stack;
    stack.clipPath(path, SkMatrix::I(), SkClipOp::kIntersect, false);

    SkRect bounds;
    SkClipStack::BoundsType boundsType;
    stack.getBounds(&bounds, &boundsType);
    REPORTER_ASSERT(reporter, SkClipStack::kInsideOut_BoundsType == boundsType);
    REPORTER_ASSERT(reporter, bounds == rect);
}

static void test_rect_replace(skiatest::Reporter* reporter) {
    SkRect rect = SkRect::MakeWH(100, 100);
    SkRect rect2 = SkRect::MakeXYWH(50, 50, 100, 100);

    SkRect bound;
    SkClipStack::BoundsType type;
    bool isIntersectionOfRects;

    // Adding a new rect with the replace operator should not increase
    // the stack depth. BW replacing BW.
    {
        SkClipStack stack;
        REPORTER_ASSERT(reporter, 0 == count(stack));
        stack.replaceClip(rect, false);
        REPORTER_ASSERT(reporter, 1 == count(stack));
        stack.replaceClip(rect, false);
        REPORTER_ASSERT(reporter, 1 == count(stack));
    }

    // Adding a new rect with the replace operator should not increase
    // the stack depth. AA replacing AA.
    {
        SkClipStack stack;
        REPORTER_ASSERT(reporter, 0 == count(stack));
        stack.replaceClip(rect, true);
        REPORTER_ASSERT(reporter, 1 == count(stack));
        stack.replaceClip(rect, true);
        REPORTER_ASSERT(reporter, 1 == count(stack));
    }

    // Adding a new rect with the replace operator should not increase
    // the stack depth. BW replacing AA replacing BW.
    {
        SkClipStack stack;
        REPORTER_ASSERT(reporter, 0 == count(stack));
        stack.replaceClip(rect, false);
        REPORTER_ASSERT(reporter, 1 == count(stack));
        stack.replaceClip(rect, true);
        REPORTER_ASSERT(reporter, 1 == count(stack));
        stack.replaceClip(rect, false);
        REPORTER_ASSERT(reporter, 1 == count(stack));
    }

    // Make sure replace clip rects don't collapse too much.
    {
        SkClipStack stack;
        stack.replaceClip(rect, false);
        stack.clipRect(rect2, SkMatrix::I(), SkClipOp::kIntersect, false);
        REPORTER_ASSERT(reporter, 1 == count(stack));

        stack.save();
        stack.replaceClip(rect, false);
        REPORTER_ASSERT(reporter, 2 == count(stack));
        stack.getBounds(&bound, &type, &isIntersectionOfRects);
        REPORTER_ASSERT(reporter, bound == rect);
        stack.restore();
        REPORTER_ASSERT(reporter, 1 == count(stack));

        stack.save();
        stack.replaceClip(rect, false);
        stack.replaceClip(rect, false);
        REPORTER_ASSERT(reporter, 2 == count(stack));
        stack.restore();
        REPORTER_ASSERT(reporter, 1 == count(stack));

        stack.save();
        stack.replaceClip(rect, false);
        stack.clipRect(rect2, SkMatrix::I(), SkClipOp::kIntersect, false);
        stack.replaceClip(rect, false);
        REPORTER_ASSERT(reporter, 2 == count(stack));
        stack.restore();
        REPORTER_ASSERT(reporter, 1 == count(stack));
    }
}

// Simplified path-based version of test_rect_replace.
static void test_path_replace(skiatest::Reporter* reporter) {
    auto replacePath = [](SkClipStack* stack, const SkPath& path, bool doAA) {
        const SkRect wideOpen = SkRect::MakeLTRB(-1000, -1000, 1000, 1000);
        stack->replaceClip(wideOpen, false);
        stack->clipPath(path, SkMatrix::I(), SkClipOp::kIntersect, doAA);
    };
    SkRect rect = SkRect::MakeWH(100, 100);
    SkPath path;
    path.addCircle(50, 50, 50);

    // Emulating replace operations with more complex geometry is not atomic, it's a replace
    // with a wide-open rect and then an intersection with the complex geometry. The replace can
    // combine with prior elements, but the subsequent intersect cannot be combined so the stack
    // continues to grow.
    {
        SkClipStack stack;
        REPORTER_ASSERT(reporter, 0 == count(stack));
        replacePath(&stack, path, false);
        REPORTER_ASSERT(reporter, 2 == count(stack));
        replacePath(&stack, path, false);
        REPORTER_ASSERT(reporter, 2 == count(stack));
    }

    // Replacing rect with path.
    {
        SkClipStack stack;
        stack.replaceClip(rect, true);
        REPORTER_ASSERT(reporter, 1 == count(stack));
        replacePath(&stack, path, true);
        REPORTER_ASSERT(reporter, 2 == count(stack));
    }
}

// Test out SkClipStack's merging of rect clips. In particular exercise
// merging of aa vs. bw rects.
static void test_rect_merging(skiatest::Reporter* reporter) {

    SkRect overlapLeft  = SkRect::MakeLTRB(10, 10, 50, 50);
    SkRect overlapRight = SkRect::MakeLTRB(40, 40, 80, 80);

    SkRect nestedParent = SkRect::MakeLTRB(10, 10, 90, 90);
    SkRect nestedChild  = SkRect::MakeLTRB(40, 40, 60, 60);

    SkRect bound;
    SkClipStack::BoundsType type;
    bool isIntersectionOfRects;

    // all bw overlapping - should merge
    {
        SkClipStack stack;
        stack.clipRect(overlapLeft, SkMatrix::I(), SkClipOp::kIntersect, false);
        stack.clipRect(overlapRight, SkMatrix::I(), SkClipOp::kIntersect, false);

        REPORTER_ASSERT(reporter, 1 == count(stack));

        stack.getBounds(&bound, &type, &isIntersectionOfRects);

        REPORTER_ASSERT(reporter, isIntersectionOfRects);
    }

    // all aa overlapping - should merge
    {
        SkClipStack stack;
        stack.clipRect(overlapLeft, SkMatrix::I(), SkClipOp::kIntersect, true);
        stack.clipRect(overlapRight, SkMatrix::I(), SkClipOp::kIntersect, true);

        REPORTER_ASSERT(reporter, 1 == count(stack));

        stack.getBounds(&bound, &type, &isIntersectionOfRects);

        REPORTER_ASSERT(reporter, isIntersectionOfRects);
    }

    // mixed overlapping - should _not_ merge
    {
        SkClipStack stack;
        stack.clipRect(overlapLeft, SkMatrix::I(), SkClipOp::kIntersect, true);
        stack.clipRect(overlapRight, SkMatrix::I(), SkClipOp::kIntersect, false);

        REPORTER_ASSERT(reporter, 2 == count(stack));

        stack.getBounds(&bound, &type, &isIntersectionOfRects);

        REPORTER_ASSERT(reporter, !isIntersectionOfRects);
    }

    // mixed nested (bw inside aa) - should merge
    {
        SkClipStack stack;
        stack.clipRect(nestedParent, SkMatrix::I(), SkClipOp::kIntersect, true);
        stack.clipRect(nestedChild, SkMatrix::I(), SkClipOp::kIntersect, false);

        REPORTER_ASSERT(reporter, 1 == count(stack));

        stack.getBounds(&bound, &type, &isIntersectionOfRects);

        REPORTER_ASSERT(reporter, isIntersectionOfRects);
    }

    // mixed nested (aa inside bw) - should merge
    {
        SkClipStack stack;
        stack.clipRect(nestedParent, SkMatrix::I(), SkClipOp::kIntersect, false);
        stack.clipRect(nestedChild, SkMatrix::I(), SkClipOp::kIntersect, true);

        REPORTER_ASSERT(reporter, 1 == count(stack));

        stack.getBounds(&bound, &type, &isIntersectionOfRects);

        REPORTER_ASSERT(reporter, isIntersectionOfRects);
    }

    // reverse nested (aa inside bw) - should _not_ merge
    {
        SkClipStack stack;
        stack.clipRect(nestedChild, SkMatrix::I(), SkClipOp::kIntersect, false);
        stack.clipRect(nestedParent, SkMatrix::I(), SkClipOp::kIntersect, true);

        REPORTER_ASSERT(reporter, 2 == count(stack));

        stack.getBounds(&bound, &type, &isIntersectionOfRects);

        REPORTER_ASSERT(reporter, !isIntersectionOfRects);
    }
}

static void test_quickContains(skiatest::Reporter* reporter) {
    SkRect testRect = SkRect::MakeLTRB(10, 10, 40, 40);
    SkRect insideRect = SkRect::MakeLTRB(20, 20, 30, 30);
    SkRect intersectingRect = SkRect::MakeLTRB(25, 25, 50, 50);
    SkRect outsideRect = SkRect::MakeLTRB(0, 0, 50, 50);
    SkRect nonIntersectingRect = SkRect::MakeLTRB(100, 100, 110, 110);

    SkPath insideCircle;
    insideCircle.addCircle(25, 25, 5);
    SkPath intersectingCircle;
    intersectingCircle.addCircle(25, 40, 10);
    SkPath outsideCircle;
    outsideCircle.addCircle(25, 25, 50);
    SkPath nonIntersectingCircle;
    nonIntersectingCircle.addCircle(100, 100, 5);

    {
        SkClipStack stack;
        stack.clipRect(outsideRect, SkMatrix::I(), SkClipOp::kDifference, false);
        // return false because quickContains currently does not care for kDifference
        REPORTER_ASSERT(reporter, false == stack.quickContains(testRect));
    }

    // Replace Op tests
    {
        SkClipStack stack;
        stack.replaceClip(outsideRect, false);
        REPORTER_ASSERT(reporter, true == stack.quickContains(testRect));
    }

    {
        SkClipStack stack;
        stack.clipRect(insideRect, SkMatrix::I(), SkClipOp::kIntersect, false);
        stack.save(); // To prevent in-place substitution by replace OP
        stack.replaceClip(outsideRect, false);
        REPORTER_ASSERT(reporter, true == stack.quickContains(testRect));
        stack.restore();
    }

    {
        SkClipStack stack;
        stack.clipRect(outsideRect, SkMatrix::I(), SkClipOp::kIntersect, false);
        stack.save(); // To prevent in-place substitution by replace OP
        stack.replaceClip(insideRect, false);
        REPORTER_ASSERT(reporter, false == stack.quickContains(testRect));
        stack.restore();
    }

    // Verify proper traversal of multi-element clip
    {
        SkClipStack stack;
        stack.clipRect(insideRect, SkMatrix::I(), SkClipOp::kIntersect, false);
        // Use a path for second clip to prevent in-place intersection
        stack.clipPath(outsideCircle, SkMatrix::I(), SkClipOp::kIntersect, false);
        REPORTER_ASSERT(reporter, false == stack.quickContains(testRect));
    }

    // Intersect Op tests with rectangles
    {
        SkClipStack stack;
        stack.clipRect(outsideRect, SkMatrix::I(), SkClipOp::kIntersect, false);
        REPORTER_ASSERT(reporter, true == stack.quickContains(testRect));
    }

    {
        SkClipStack stack;
        stack.clipRect(insideRect, SkMatrix::I(), SkClipOp::kIntersect, false);
        REPORTER_ASSERT(reporter, false == stack.quickContains(testRect));
    }

    {
        SkClipStack stack;
        stack.clipRect(intersectingRect, SkMatrix::I(), SkClipOp::kIntersect, false);
        REPORTER_ASSERT(reporter, false == stack.quickContains(testRect));
    }

    {
        SkClipStack stack;
        stack.clipRect(nonIntersectingRect, SkMatrix::I(), SkClipOp::kIntersect, false);
        REPORTER_ASSERT(reporter, false == stack.quickContains(testRect));
    }

    // Intersect Op tests with circle paths
    {
        SkClipStack stack;
        stack.clipPath(outsideCircle, SkMatrix::I(), SkClipOp::kIntersect, false);
        REPORTER_ASSERT(reporter, true == stack.quickContains(testRect));
    }

    {
        SkClipStack stack;
        stack.clipPath(insideCircle, SkMatrix::I(), SkClipOp::kIntersect, false);
        REPORTER_ASSERT(reporter, false == stack.quickContains(testRect));
    }

    {
        SkClipStack stack;
        stack.clipPath(intersectingCircle, SkMatrix::I(), SkClipOp::kIntersect, false);
        REPORTER_ASSERT(reporter, false == stack.quickContains(testRect));
    }

    {
        SkClipStack stack;
        stack.clipPath(nonIntersectingCircle, SkMatrix::I(), SkClipOp::kIntersect, false);
        REPORTER_ASSERT(reporter, false == stack.quickContains(testRect));
    }

    // Intersect Op tests with inverse filled rectangles
    {
        SkClipStack stack;
        SkPath path;
        path.addRect(outsideRect);
        path.toggleInverseFillType();
        stack.clipPath(path, SkMatrix::I(), SkClipOp::kIntersect, false);
        REPORTER_ASSERT(reporter, false == stack.quickContains(testRect));
    }

    {
        SkClipStack stack;
        SkPath path;
        path.addRect(insideRect);
        path.toggleInverseFillType();
        stack.clipPath(path, SkMatrix::I(), SkClipOp::kIntersect, false);
        REPORTER_ASSERT(reporter, false == stack.quickContains(testRect));
    }

    {
        SkClipStack stack;
        SkPath path;
        path.addRect(intersectingRect);
        path.toggleInverseFillType();
        stack.clipPath(path, SkMatrix::I(), SkClipOp::kIntersect, false);
        REPORTER_ASSERT(reporter, false == stack.quickContains(testRect));
    }

    {
        SkClipStack stack;
        SkPath path;
        path.addRect(nonIntersectingRect);
        path.toggleInverseFillType();
        stack.clipPath(path, SkMatrix::I(), SkClipOp::kIntersect, false);
        REPORTER_ASSERT(reporter, true == stack.quickContains(testRect));
    }

    // Intersect Op tests with inverse filled circles
    {
        SkClipStack stack;
        SkPath path = outsideCircle;
        path.toggleInverseFillType();
        stack.clipPath(path, SkMatrix::I(), SkClipOp::kIntersect, false);
        REPORTER_ASSERT(reporter, false == stack.quickContains(testRect));
    }

    {
        SkClipStack stack;
        SkPath path = insideCircle;
        path.toggleInverseFillType();
        stack.clipPath(path, SkMatrix::I(), SkClipOp::kIntersect, false);
        REPORTER_ASSERT(reporter, false == stack.quickContains(testRect));
    }

    {
        SkClipStack stack;
        SkPath path = intersectingCircle;
        path.toggleInverseFillType();
        stack.clipPath(path, SkMatrix::I(), SkClipOp::kIntersect, false);
        REPORTER_ASSERT(reporter, false == stack.quickContains(testRect));
    }

    {
        SkClipStack stack;
        SkPath path = nonIntersectingCircle;
        path.toggleInverseFillType();
        stack.clipPath(path, SkMatrix::I(), SkClipOp::kIntersect, false);
        REPORTER_ASSERT(reporter, true == stack.quickContains(testRect));
    }
}

static void set_region_to_stack(const SkClipStack& stack, const SkIRect& bounds, SkRegion* region) {
    region->setRect(bounds);
    SkClipStack::Iter iter(stack, SkClipStack::Iter::kBottom_IterStart);
    while (const SkClipStack::Element *element = iter.next()) {
        SkRegion elemRegion;
        SkRegion boundsRgn(bounds);
        SkPath path;

        switch (element->getDeviceSpaceType()) {
            case SkClipStack::Element::DeviceSpaceType::kEmpty:
                elemRegion.setEmpty();
                break;
            default:
                element->asDeviceSpacePath(&path);
                elemRegion.setPath(path, boundsRgn);
                break;
        }

        region->op(elemRegion, element->getRegionOp());
    }
}

static void test_invfill_diff_bug(skiatest::Reporter* reporter) {
    SkClipStack stack;
    stack.clipRect({10, 10, 20, 20}, SkMatrix::I(), SkClipOp::kIntersect, false);

    SkPath path;
    path.addRect({30, 10, 40, 20});
    path.setFillType(SkPathFillType::kInverseWinding);
    stack.clipPath(path, SkMatrix::I(), SkClipOp::kDifference, false);

    REPORTER_ASSERT(reporter, SkClipStack::kEmptyGenID == stack.getTopmostGenID());

    SkRect stackBounds;
    SkClipStack::BoundsType stackBoundsType;
    stack.getBounds(&stackBounds, &stackBoundsType);

    REPORTER_ASSERT(reporter, stackBounds.isEmpty());
    REPORTER_ASSERT(reporter, SkClipStack::kNormal_BoundsType == stackBoundsType);

    SkRegion region;
    set_region_to_stack(stack, {0, 0, 50, 30}, &region);

    REPORTER_ASSERT(reporter, region.isEmpty());
}

///////////////////////////////////////////////////////////////////////////////////////////////////

#if SK_GPU_V1
#include "src/gpu/GrClipStackClip.h"
#include "src/gpu/GrReducedClip.h"
#include "src/gpu/v1/SurfaceDrawContext_v1.h"

typedef GrReducedClip::ElementList ElementList;
typedef GrReducedClip::InitialState InitialState;

// Functions that add a shape to the clip stack. The shape is computed from a rectangle.
// AA is always disabled since the clip stack reducer can cause changes in aa rasterization of the
// stack. A fractional edge repeated in different elements may be rasterized fewer times using the
// reduced stack.
typedef void (*AddElementFunc) (const SkRect& rect,
                                bool invert,
                                SkClipOp op,
                                SkClipStack* stack,
                                bool doAA);

static void add_round_rect(const SkRect& rect, bool invert, SkClipOp op, SkClipStack* stack,
                           bool doAA) {
    SkScalar rx = rect.width() / 10;
    SkScalar ry = rect.height() / 20;
    if (invert) {
        SkPath path;
        path.addRoundRect(rect, rx, ry);
        path.setFillType(SkPathFillType::kInverseWinding);
        stack->clipPath(path, SkMatrix::I(), op, doAA);
    } else {
        SkRRect rrect;
        rrect.setRectXY(rect, rx, ry);
        stack->clipRRect(rrect, SkMatrix::I(), op, doAA);
    }
};

static void add_rect(const SkRect& rect, bool invert, SkClipOp op, SkClipStack* stack,
                     bool doAA) {
    if (invert) {
        SkPath path;
        path.addRect(rect);
        path.setFillType(SkPathFillType::kInverseWinding);
        stack->clipPath(path, SkMatrix::I(), op, doAA);
    } else {
        stack->clipRect(rect, SkMatrix::I(), op, doAA);
    }
};

static void add_oval(const SkRect& rect, bool invert, SkClipOp op, SkClipStack* stack,
                     bool doAA) {
    SkPath path;
    path.addOval(rect);
    if (invert) {
        path.setFillType(SkPathFillType::kInverseWinding);
    }
    stack->clipPath(path, SkMatrix::I(), op, doAA);
};

static void add_shader(const SkRect& rect, bool invert, SkClipOp op, SkClipStack* stack,
                       bool doAA) {
    // invert, op, and doAA don't apply to shaders at the SkClipStack level; this is handled earlier
    // in the SkCanvas->SkDevice stack. Use rect to produce unique gradients, however.
    SkPoint corners[2] = { {rect.fLeft, rect.fTop}, {rect.fRight, rect.fBottom} };
    SkColor colors[2] = { SK_ColorBLACK, SK_ColorTRANSPARENT };
    auto gradient = SkGradientShader::MakeLinear(corners, colors, nullptr, 2, SkTileMode::kDecal);
    stack->clipShader(std::move(gradient));
}

static void add_elem_to_stack(const SkClipStack::Element& element, SkClipStack* stack) {
    if (element.isReplaceOp()) {
        const SkRect resetBounds = SkRect::MakeLTRB(-10000.f, -10000.f, 10000.f, 10000.f);
        stack->replaceClip(resetBounds, element.isAA());
    }
    switch (element.getDeviceSpaceType()) {
        case SkClipStack::Element::DeviceSpaceType::kRect:
            stack->clipRect(element.getDeviceSpaceRect(), SkMatrix::I(), element.getOp(),
                            element.isAA());
            break;
        case SkClipStack::Element::DeviceSpaceType::kRRect:
            stack->clipRRect(element.getDeviceSpaceRRect(), SkMatrix::I(), element.getOp(),
                             element.isAA());
            break;
        case SkClipStack::Element::DeviceSpaceType::kPath:
            stack->clipPath(element.getDeviceSpacePath(), SkMatrix::I(), element.getOp(),
                            element.isAA());
            break;
        case SkClipStack::Element::DeviceSpaceType::kShader:
            SkDEBUGFAIL("Why did the reducer put this in the mask elements.");
            stack->clipShader(element.refShader());
            break;
        case SkClipStack::Element::DeviceSpaceType::kEmpty:
            SkDEBUGFAIL("Why did the reducer produce an explicit empty.");
            stack->clipEmpty();
            break;
    }
}

static void test_reduced_clip_stack(skiatest::Reporter* reporter, bool enableClipShader) {
    // We construct random clip stacks, reduce them, and then rasterize both versions to verify that
    // they are equal.

    // All the clip elements will be contained within these bounds.
    static const SkIRect kIBounds = SkIRect::MakeWH(100, 100);
    static const SkRect kBounds = SkRect::Make(kIBounds);

    enum {
        kNumTests = 250,
        kMinElemsPerTest = 1,
        kMaxElemsPerTest = 50,
    };

    // min/max size of a clip element as a fraction of kBounds.
    static const SkScalar kMinElemSizeFrac = SK_Scalar1 / 5;
    static const SkScalar kMaxElemSizeFrac = SK_Scalar1;

    static const SkClipOp kOps[] = {
        SkClipOp::kDifference,
        SkClipOp::kIntersect,
    };

    // We want to test inverse fills. However, they are quite rare in practice so don't over do it.
    static const SkScalar kFractionInverted = SK_Scalar1 / kMaxElemsPerTest;

    static const SkScalar kFractionAntialiased = 0.25;

    static const AddElementFunc kElementFuncs[] = {
        add_rect,
        add_round_rect,
        add_oval,
        add_shader
    };

    SkRandom r;

    for (int i = 0; i < kNumTests; ++i) {
        SkString testCase;
        testCase.printf("Iteration %d", i);

        // Randomly generate a clip stack.
        SkClipStack stack;
        int numElems = r.nextRangeU(kMinElemsPerTest, kMaxElemsPerTest);
        bool doAA = r.nextBiasedBool(kFractionAntialiased);
        for (int e = 0; e < numElems; ++e) {
            size_t opLimit = enableClipShader ? ((size_t) SkClipOp::kIntersect + 1)
                                              : SK_ARRAY_COUNT(kOps);
            SkClipOp op = kOps[r.nextULessThan(opLimit)];

            // saves can change the clip stack behavior when an element is added.
            bool doSave = r.nextBool();

            SkSize size = SkSize::Make(
                kBounds.width()  * r.nextRangeScalar(kMinElemSizeFrac, kMaxElemSizeFrac),
                kBounds.height() * r.nextRangeScalar(kMinElemSizeFrac, kMaxElemSizeFrac));

            SkPoint xy = {r.nextRangeScalar(kBounds.fLeft, kBounds.fRight - size.fWidth),
                          r.nextRangeScalar(kBounds.fTop, kBounds.fBottom - size.fHeight)};

            SkRect rect;
            if (doAA) {
                rect.setXYWH(xy.fX, xy.fY, size.fWidth, size.fHeight);
                if (GrClip::IsPixelAligned(rect)) {
                    // Don't create an element that may accidentally become not antialiased.
                    rect.outset(0.5f, 0.5f);
                }
                SkASSERT(!GrClip::IsPixelAligned(rect));
            } else {
                rect.setXYWH(SkScalarFloorToScalar(xy.fX),
                             SkScalarFloorToScalar(xy.fY),
                             SkScalarCeilToScalar(size.fWidth),
                             SkScalarCeilToScalar(size.fHeight));
            }

            bool invert = r.nextBiasedBool(kFractionInverted);

            size_t functionLimit = SK_ARRAY_COUNT(kElementFuncs);
            if (!enableClipShader) {
                functionLimit--;
            }
            kElementFuncs[r.nextULessThan(functionLimit)](rect, invert, op, &stack, doAA);
            if (doSave) {
                stack.save();
            }
        }

        sk_sp<GrDirectContext> context = GrDirectContext::MakeMock(nullptr);
        const GrCaps* caps = context->priv().caps();

        // Zero the memory we will new the GrReducedClip into. This ensures the elements gen ID
        // will be kInvalidGenID if left uninitialized.
        SkAlignedSTStorage<1, GrReducedClip> storage;
        memset(storage.get(), 0, sizeof(GrReducedClip));
        static_assert(0 == SkClipStack::kInvalidGenID);

        // Get the reduced version of the stack.
        SkRect queryBounds = kBounds;
        queryBounds.outset(kBounds.width() / 2, kBounds.height() / 2);
        const GrReducedClip* reduced = new (storage.get()) GrReducedClip(stack, queryBounds, caps);

        REPORTER_ASSERT(reporter,
                        reduced->maskElements().isEmpty() ||
                                SkClipStack::kInvalidGenID != reduced->maskGenID(),
                        testCase.c_str());

        if (!reduced->maskElements().isEmpty()) {
            REPORTER_ASSERT(reporter, reduced->hasScissor(), testCase.c_str());
            SkRect stackBounds;
            SkClipStack::BoundsType stackBoundsType;
            stack.getBounds(&stackBounds, &stackBoundsType);
            REPORTER_ASSERT(reporter, reduced->maskRequiresAA() == doAA, testCase.c_str());
        }

        // Build a new clip stack based on the reduced clip elements
        SkClipStack reducedStack;
        if (GrReducedClip::InitialState::kAllOut == reduced->initialState()) {
            // whether the result is bounded or not, the whole plane should start outside the clip.
            reducedStack.clipEmpty();
        }
        for (ElementList::Iter iter(reduced->maskElements()); iter.get(); iter.next()) {
            add_elem_to_stack(*iter.get(), &reducedStack);
        }
        if (reduced->hasShader()) {
            REPORTER_ASSERT(reporter, enableClipShader);
            reducedStack.clipShader(reduced->shader());
        }

        SkIRect scissor = reduced->hasScissor() ? reduced->scissor() : kIBounds;

        // GrReducedClipStack assumes that the final result is clipped to the returned bounds
        reducedStack.clipDevRect(scissor, SkClipOp::kIntersect);
        stack.clipDevRect(scissor, SkClipOp::kIntersect);

        // convert both the original stack and reduced stack to SkRegions and see if they're equal
        SkRegion region;
        set_region_to_stack(stack, scissor, &region);

        SkRegion reducedRegion;
        set_region_to_stack(reducedStack, scissor, &reducedRegion);

        REPORTER_ASSERT(reporter, region == reducedRegion, testCase.c_str());

        reduced->~GrReducedClip();
    }
}

#ifdef SK_BUILD_FOR_WIN
    #define SUPPRESS_VISIBILITY_WARNING
#else
    #define SUPPRESS_VISIBILITY_WARNING __attribute__((visibility("hidden")))
#endif

static void test_reduced_clip_stack_no_aa_crash(skiatest::Reporter* reporter) {
    SkClipStack stack;
    stack.clipDevRect(SkIRect::MakeXYWH(0, 0, 100, 100), SkClipOp::kIntersect);
    stack.clipDevRect(SkIRect::MakeXYWH(0, 0, 50, 50), SkClipOp::kIntersect);
    SkRect bounds = SkRect::MakeXYWH(0, 0, 100, 100);

    sk_sp<GrDirectContext> context = GrDirectContext::MakeMock(nullptr);
    const GrCaps* caps = context->priv().caps();

    // At the time, this would crash.
    const GrReducedClip reduced(stack, bounds, caps);
    REPORTER_ASSERT(reporter, reduced.maskElements().isEmpty());
}

enum class ClipMethod {
    kSkipDraw,
    kIgnoreClip,
    kScissor,
    kAAElements
};

static void test_aa_query(skiatest::Reporter* reporter, const SkString& testName,
                          const SkClipStack& stack, const SkMatrix& queryXform,
                          const SkRect& preXformQuery, ClipMethod expectedMethod,
                          int numExpectedElems = 0) {
    sk_sp<GrDirectContext> context = GrDirectContext::MakeMock(nullptr);
    const GrCaps* caps = context->priv().caps();

    SkRect queryBounds;
    queryXform.mapRect(&queryBounds, preXformQuery);
    const GrReducedClip reduced(stack, queryBounds, caps);

    SkClipStack::BoundsType stackBoundsType;
    SkRect stackBounds;
    stack.getBounds(&stackBounds, &stackBoundsType);

    switch (expectedMethod) {
        case ClipMethod::kSkipDraw:
            SkASSERT(0 == numExpectedElems);
            REPORTER_ASSERT(reporter, reduced.maskElements().isEmpty(), testName.c_str());
            REPORTER_ASSERT(reporter,
                            GrReducedClip::InitialState::kAllOut == reduced.initialState(),
                            testName.c_str());
            return;
        case ClipMethod::kIgnoreClip:
            SkASSERT(0 == numExpectedElems);
            REPORTER_ASSERT(
                    reporter,
                    !reduced.hasScissor() || GrClip::IsInsideClip(reduced.scissor(), queryBounds),
                    testName.c_str());
            REPORTER_ASSERT(reporter, reduced.maskElements().isEmpty(), testName.c_str());
            REPORTER_ASSERT(reporter,
                            GrReducedClip::InitialState::kAllIn == reduced.initialState(),
                            testName.c_str());
            return;
        case ClipMethod::kScissor: {
            SkASSERT(SkClipStack::kNormal_BoundsType == stackBoundsType);
            SkASSERT(0 == numExpectedElems);
            SkIRect expectedScissor;
            stackBounds.round(&expectedScissor);
            REPORTER_ASSERT(reporter, reduced.maskElements().isEmpty(), testName.c_str());
            REPORTER_ASSERT(reporter, reduced.hasScissor(), testName.c_str());
            REPORTER_ASSERT(reporter, expectedScissor == reduced.scissor(), testName.c_str());
            REPORTER_ASSERT(reporter,
                            GrReducedClip::InitialState::kAllIn == reduced.initialState(),
                            testName.c_str());
            return;
        }
        case ClipMethod::kAAElements: {
            SkIRect expectedClipIBounds = GrClip::GetPixelIBounds(queryBounds);
            if (SkClipStack::kNormal_BoundsType == stackBoundsType) {
                SkAssertResult(expectedClipIBounds.intersect(GrClip::GetPixelIBounds(stackBounds)));
            }
            REPORTER_ASSERT(reporter, numExpectedElems == reduced.maskElements().count(),
                            testName.c_str());
            REPORTER_ASSERT(reporter, reduced.hasScissor(), testName.c_str());
            REPORTER_ASSERT(reporter, expectedClipIBounds == reduced.scissor(), testName.c_str());
            REPORTER_ASSERT(reporter,
                            reduced.maskElements().isEmpty() || reduced.maskRequiresAA(),
                            testName.c_str());
            break;
        }
    }
}

static void test_reduced_clip_stack_aa(skiatest::Reporter* reporter) {
    constexpr SkScalar IL = 2, IT = 1, IR = 6, IB = 7;         // Pixel aligned rect.
    constexpr SkScalar L = 2.2f, T = 1.7f, R = 5.8f, B = 7.3f; // Generic rect.
    constexpr SkScalar l = 3.3f, t = 2.8f, r = 4.7f, b = 6.2f; // Small rect contained in R.

    SkRect alignedRect = {IL, IT, IR, IB};
    SkRect rect = {L, T, R, B};
    SkRect innerRect = {l, t, r, b};

    SkMatrix m;
    m.setIdentity();

    constexpr SkScalar kMinScale = 2.0001f;
    constexpr SkScalar kMaxScale = 3;
    constexpr int kNumIters = 8;

    SkString name;
    SkRandom rand;

    for (int i = 0; i < kNumIters; ++i) {
        // Pixel-aligned rect (iior=true).
        name.printf("Pixel-aligned rect test, iter %i", i);
        SkClipStack stack;
        stack.clipRect(alignedRect, SkMatrix::I(), SkClipOp::kIntersect, true);
        test_aa_query(reporter, name, stack, m, {IL, IT, IR, IB}, ClipMethod::kIgnoreClip);
        test_aa_query(reporter, name, stack, m, {IL, IT-1, IR, IT}, ClipMethod::kSkipDraw);
        test_aa_query(reporter, name, stack, m, {IL, IT-2, IR, IB}, ClipMethod::kScissor);

        // Rect (iior=true).
        name.printf("Rect test, iter %i", i);
        stack.reset();
        stack.clipRect(rect, SkMatrix::I(), SkClipOp::kIntersect, true);
        test_aa_query(reporter, name, stack, m, {L, T,  R, B}, ClipMethod::kIgnoreClip);
        test_aa_query(reporter, name, stack, m, {L-.1f, T, L, B}, ClipMethod::kSkipDraw);
        test_aa_query(reporter, name, stack, m, {L-.1f, T, L+.1f, B}, ClipMethod::kAAElements, 1);

        // Difference rect (iior=false, inside-out bounds).
        name.printf("Difference rect test, iter %i", i);
        stack.reset();
        stack.clipRect(rect, SkMatrix::I(), SkClipOp::kDifference, true);
        test_aa_query(reporter, name, stack, m, {L, T, R, B}, ClipMethod::kSkipDraw);
        test_aa_query(reporter, name, stack, m, {L, T-.1f, R, T}, ClipMethod::kIgnoreClip);
        test_aa_query(reporter, name, stack, m, {L, T-.1f, R, T+.1f}, ClipMethod::kAAElements, 1);

        // Complex clip (iior=false, normal bounds).
        name.printf("Complex clip test, iter %i", i);
        stack.reset();
        stack.clipRect(rect, SkMatrix::I(), SkClipOp::kIntersect, true);
        stack.clipRect(innerRect, SkMatrix::I(), SkClipOp::kDifference, true);
        test_aa_query(reporter, name, stack, m, {l, t, r, b}, ClipMethod::kSkipDraw);
        test_aa_query(reporter, name, stack, m, {r-.1f, t, R, b}, ClipMethod::kAAElements, 1);
        test_aa_query(reporter, name, stack, m, {r-.1f, t, R+.1f, b}, ClipMethod::kAAElements, 2);
        test_aa_query(reporter, name, stack, m, {r, t, R+.1f, b}, ClipMethod::kAAElements, 1);
        test_aa_query(reporter, name, stack, m, {r, t, R, b}, ClipMethod::kIgnoreClip);
        test_aa_query(reporter, name, stack, m, {R, T, R+.1f, B}, ClipMethod::kSkipDraw);

        // Complex clip where outer rect is pixel aligned (iior=false, normal bounds).
        name.printf("Aligned Complex clip test, iter %i", i);
        stack.reset();
        stack.clipRect(alignedRect, SkMatrix::I(), SkClipOp::kIntersect, true);
        stack.clipRect(innerRect, SkMatrix::I(), SkClipOp::kDifference, true);
        test_aa_query(reporter, name, stack, m, {l, t, r, b}, ClipMethod::kSkipDraw);
        test_aa_query(reporter, name, stack, m, {l, b-.1f, r, IB}, ClipMethod::kAAElements, 1);
        test_aa_query(reporter, name, stack, m, {l, b-.1f, r, IB+.1f}, ClipMethod::kAAElements, 1);
        test_aa_query(reporter, name, stack, m, {l, b, r, IB+.1f}, ClipMethod::kAAElements, 0);
        test_aa_query(reporter, name, stack, m, {l, b, r, IB}, ClipMethod::kIgnoreClip);
        test_aa_query(reporter, name, stack, m, {IL, IB, IR, IB+.1f}, ClipMethod::kSkipDraw);

        // Apply random transforms and try again. This ensures the clip stack reduction is hardened
        // against FP rounding error.
        SkScalar sx = rand.nextRangeScalar(kMinScale, kMaxScale);
        sx = SkScalarFloorToScalar(sx * alignedRect.width()) / alignedRect.width();
        SkScalar sy = rand.nextRangeScalar(kMinScale, kMaxScale);
        sy = SkScalarFloorToScalar(sy * alignedRect.height()) / alignedRect.height();
        SkScalar tx = SkScalarRoundToScalar(sx * alignedRect.x()) - sx * alignedRect.x();
        SkScalar ty = SkScalarRoundToScalar(sy * alignedRect.y()) - sy * alignedRect.y();

        SkMatrix xform = SkMatrix::Scale(sx, sy);
        xform.postTranslate(tx, ty);
        xform.mapRect(&alignedRect);
        xform.mapRect(&rect);
        xform.mapRect(&innerRect);
        m.postConcat(xform);
    }
}

static void test_tiny_query_bounds_assertion_bug(skiatest::Reporter* reporter) {
    // https://bugs.chromium.org/p/skia/issues/detail?id=5990
    const SkRect clipBounds = SkRect::MakeXYWH(1.5f, 100, 1000, 1000);

    SkClipStack rectStack;
    rectStack.clipRect(clipBounds, SkMatrix::I(), SkClipOp::kIntersect, true);

    SkPath clipPath;
    clipPath.moveTo(clipBounds.left(), clipBounds.top());
    clipPath.quadTo(clipBounds.right(), clipBounds.top(),
                    clipBounds.right(), clipBounds.bottom());
    clipPath.quadTo(clipBounds.left(), clipBounds.bottom(),
                    clipBounds.left(), clipBounds.top());
    SkClipStack pathStack;
    pathStack.clipPath(clipPath, SkMatrix::I(), SkClipOp::kIntersect, true);

    sk_sp<GrDirectContext> context = GrDirectContext::MakeMock(nullptr);
    const GrCaps* caps = context->priv().caps();

    for (const SkClipStack& stack : {rectStack, pathStack}) {
        for (SkRect queryBounds : {SkRect::MakeXYWH(53, 60, GrClip::kBoundsTolerance, 1000),
                                   SkRect::MakeXYWH(53, 60, GrClip::kBoundsTolerance/2, 1000),
                                   SkRect::MakeXYWH(53, 160, 1000, GrClip::kBoundsTolerance),
                                   SkRect::MakeXYWH(53, 160, 1000, GrClip::kBoundsTolerance/2)}) {
            const GrReducedClip reduced(stack, queryBounds, caps);
            REPORTER_ASSERT(reporter, !reduced.hasScissor());
            REPORTER_ASSERT(reporter, reduced.maskElements().isEmpty());
            REPORTER_ASSERT(reporter,
                            GrReducedClip::InitialState::kAllOut == reduced.initialState());
        }
    }
}

#endif // SK_GPU_V1

static void test_is_rrect_deep_rect_stack(skiatest::Reporter* reporter) {
    static constexpr SkRect kTargetBounds = SkRect::MakeWH(1000, 500);
    // All antialiased or all not antialiased.
    for (bool aa : {false, true}) {
        SkClipStack stack;
        for (int i = 0; i <= 100; ++i) {
            stack.save();
            stack.clipRect(SkRect::MakeLTRB(i, 0.5, kTargetBounds.width(), kTargetBounds.height()),
                           SkMatrix::I(), SkClipOp::kIntersect, aa);
        }
        SkRRect rrect;
        bool isAA;
        SkRRect expected = SkRRect::MakeRect(
                SkRect::MakeLTRB(100, 0.5, kTargetBounds.width(), kTargetBounds.height()));
        if (stack.isRRect(kTargetBounds, &rrect, &isAA)) {
            REPORTER_ASSERT(reporter, rrect == expected);
            REPORTER_ASSERT(reporter, aa == isAA);
        } else {
            ERRORF(reporter, "Expected to be an rrect.");
        }
    }
    // Mixed AA and non-AA without simple containment.
    SkClipStack stack;
    for (int i = 0; i <= 100; ++i) {
        bool aa = i & 0b1;
        int j = 100 - i;
        stack.save();
        stack.clipRect(SkRect::MakeLTRB(i, j + 0.5, kTargetBounds.width(), kTargetBounds.height()),
                       SkMatrix::I(), SkClipOp::kIntersect, aa);
    }
    SkRRect rrect;
    bool isAA;
    REPORTER_ASSERT(reporter, !stack.isRRect(kTargetBounds, &rrect, &isAA));
}

DEF_TEST(ClipStack, reporter) {
    SkClipStack stack;

    REPORTER_ASSERT(reporter, 0 == stack.getSaveCount());
    assert_count(reporter, stack, 0);

    static const SkIRect gRects[] = {
        { 0, 0, 100, 100 },
        { 25, 25, 125, 125 },
        { 0, 0, 1000, 1000 },
        { 0, 0, 75, 75 }
    };
    for (size_t i = 0; i < SK_ARRAY_COUNT(gRects); i++) {
        stack.clipDevRect(gRects[i], SkClipOp::kIntersect);
    }

    // all of the above rects should have been intersected, leaving only 1 rect
    SkClipStack::B2TIter iter(stack);
    const SkClipStack::Element* element = iter.next();
    SkRect answer;
    answer.setLTRB(25, 25, 75, 75);

    REPORTER_ASSERT(reporter, element);
    REPORTER_ASSERT(reporter,
                    SkClipStack::Element::DeviceSpaceType::kRect == element->getDeviceSpaceType());
    REPORTER_ASSERT(reporter, SkClipOp::kIntersect == element->getOp());
    REPORTER_ASSERT(reporter, element->getDeviceSpaceRect() == answer);
    // now check that we only had one in our iterator
    REPORTER_ASSERT(reporter, !iter.next());

    stack.reset();
    REPORTER_ASSERT(reporter, 0 == stack.getSaveCount());
    assert_count(reporter, stack, 0);

    test_assign_and_comparison(reporter);
    test_iterators(reporter);
    test_bounds(reporter, SkClipStack::Element::DeviceSpaceType::kRect);
    test_bounds(reporter, SkClipStack::Element::DeviceSpaceType::kRRect);
    test_bounds(reporter, SkClipStack::Element::DeviceSpaceType::kPath);
    test_isWideOpen(reporter);
    test_rect_merging(reporter);
    test_rect_replace(reporter);
    test_rect_inverse_fill(reporter);
    test_path_replace(reporter);
    test_quickContains(reporter);
    test_invfill_diff_bug(reporter);

#if SK_GPU_V1
    test_reduced_clip_stack(reporter, /* clipShader */ false);
    test_reduced_clip_stack(reporter, /* clipShader */ true);
    test_reduced_clip_stack_no_aa_crash(reporter);
    test_reduced_clip_stack_aa(reporter);
    test_tiny_query_bounds_assertion_bug(reporter);
#endif
    test_is_rrect_deep_rect_stack(reporter);
}

//////////////////////////////////////////////////////////////////////////////

// For the GrClipStack case, this is covered in GrClipStack_SWMask
#if defined(SK_DISABLE_NEW_GR_CLIP_STACK)

sk_sp<GrTextureProxy> GrClipStackClip::testingOnly_createClipMask(
        GrRecordingContext* context) const {
    const GrReducedClip reducedClip(*fStack, SkRect::MakeWH(512, 512), nullptr);
    return this->createSoftwareClipMask(context, reducedClip, nullptr).asTextureProxyRef();
}

// Verify that clip masks are freed up when the clip state that generated them goes away.
DEF_GPUTEST_FOR_ALL_CONTEXTS(ClipMaskCache, reporter, ctxInfo) {
    // This test uses resource key tags which only function in debug builds.
#ifdef SK_DEBUG
    auto context = ctxInfo.directContext();
    SkClipStack stack;

    SkPath path;
    path.addCircle(10, 10, 8);
    path.addCircle(15, 15, 8);
    path.setFillType(SkPathFillType::kEvenOdd);

    SkIRect stackBounds = path.getBounds().roundOut();

    static const char* kTag = GrClipStackClip::kMaskTestTag;
    GrResourceCache* cache = context->priv().getResourceCache();

    static constexpr int kN = 5;

    for (int i = 0; i < kN; ++i) {
        SkMatrix m;
        m.setTranslate(0.5, 0.5);
        stack.save();
        stack.clipPath(path, m, SkClipOp::kIntersect, true);
        sk_sp<GrTextureProxy> mask =
                GrClipStackClip(stackBounds.size(), &stack).testingOnly_createClipMask(context);
        mask->instantiate(context->priv().resourceProvider());
        GrTexture* tex = mask->peekTexture();
        REPORTER_ASSERT(reporter, 0 == strcmp(tex->getUniqueKey().tag(), kTag));
        // Make sure mask isn't pinned in cache.
        mask.reset(nullptr);
        context->flushAndSubmit();
        REPORTER_ASSERT(reporter, i + 1 == cache->countUniqueKeysWithTag(kTag));
    }

    for (int i = 0; i < kN; ++i) {
        stack.restore();
        cache->purgeAsNeeded();
        REPORTER_ASSERT(reporter, kN - (i + 1) == cache->countUniqueKeysWithTag(kTag));
    }
#endif
}
#endif // SK_DISABLE_NEW_GR_CLIP_STACK
