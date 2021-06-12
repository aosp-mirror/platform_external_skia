// Copyright 2021 Google LLC.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "experimental/ngatoy/Fake.h"

#include "experimental/ngatoy/Cmds.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"


void FakeMCBlob::MCState::apply(SkCanvas* canvas) const {
    canvas->save();

    for (auto c : fRects) {
        canvas->clipIRect(c);
    }

    canvas->translate(fTrans.fX, fTrans.fY);
}

void FakeMCBlob::MCState::apply(FakeCanvas* canvas) const {
    canvas->save();

    for (auto c : fRects) {
        canvas->clipRect(c);
    }

    canvas->translate(fTrans);
}

//-------------------------------------------------------------------------------------------------
// Linearly blend between c0 & c1:
//      (t == 0) -> c0
//      (t == 1) -> c1
static SkColor blend(float t, SkColor c0, SkColor c1) {
    SkASSERT(t >= 0.0f && t <= 1.0f);

    SkColor4f top = SkColor4f::FromColor(c0);
    SkColor4f bot = SkColor4f::FromColor(c1);

    SkColor4f result = {
        t * bot.fR + (1.0f - t) * top.fR,
        t * bot.fG + (1.0f - t) * top.fG,
        t * bot.fB + (1.0f - t) * top.fB,
        t * bot.fA + (1.0f - t) * top.fA
    };
    return result.toSkColor();
}

SkColor FakePaint::evalColor(int x, int y) const {
    switch (fType) {
        case Type::kNormal: return fColor0;
        case Type::kLinear: {
            float t = SK_ScalarRoot2Over2 * x + SK_ScalarRoot2Over2 * y;
            t /= SK_ScalarSqrt2 * 256.0f;
            return blend(t, fColor0, fColor1);
        }
        case Type::kRadial: {
            x -= 128;
            y -= 128;
            float dist = sqrt(x*x + y*y) / 128.0f;
            if (dist > 1.0f) {
                return fColor0;
            } else {
                return blend(dist, fColor0, fColor1);
            }
        }
    }
    SkUNREACHABLE;
}

//-------------------------------------------------------------------------------------------------
void FakeDevice::save() {
    fTracker.push();
}

void FakeDevice::drawRect(ID id, PaintersOrder paintersOrder, SkIRect r, FakePaint p) {

    sk_sp<FakeMCBlob> state = fTracker.snapState();

    auto tmp = new RectCmd(id, paintersOrder, r, p, std::move(state));

    fSortedCmds.push_back(tmp);
}

void FakeDevice::clipRect(SkIRect r) {
    fTracker.clipRect(r);
}

void FakeDevice::restore() {
    fTracker.pop();
}

void FakeDevice::finalize() {
    SkASSERT(!fFinalized);
    fFinalized = true;

    this->sort();
    for (auto c : fSortedCmds) {
        c->rasterize(fZBuffer, &fBM);
    }
}

void FakeDevice::getOrder(std::vector<ID>* ops) const {
    SkASSERT(fFinalized);

//    ops->reserve(fSortedCmds.size());

    for (auto c : fSortedCmds) {
        ops->push_back(c->id());
    }
}

void FakeDevice::sort() {
    // In general we want:
    //  opaque draws to occur front to back (i.e., in reverse painter's order) while minimizing
    //        state changes due to materials
    //  transparent draws to occur back to front (i.e., in painter's order)
    //
    // In both scenarios we would like to batch as much as possible.
    std::sort(fSortedCmds.begin(), fSortedCmds.end(),
                [](Cmd* a, Cmd* b) {
                    return a->getKey() < b->getKey();
                });
}

//-------------------------------------------------------------------------------------------------
void FakeCanvas::drawRect(ID id, SkIRect r, FakePaint p) {
    SkASSERT(!fFinalized);

    fDeviceStack.back()->drawRect(id, this->nextPaintersOrder(), r, p);
}

void FakeCanvas::clipRect(SkIRect r) {
    SkASSERT(!fFinalized);

    fDeviceStack.back()->clipRect(r);
}

void FakeCanvas::finalize() {
    SkASSERT(!fFinalized);
    fFinalized = true;

    for (auto& d : fDeviceStack) {
        d->finalize();
    }
}

std::vector<ID> FakeCanvas::getOrder() const {
    SkASSERT(fFinalized);

    std::vector<ID> ops;

    for (auto& d : fDeviceStack) {
        d->getOrder(&ops);
    }

    return ops;
}

