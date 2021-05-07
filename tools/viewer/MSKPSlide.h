/*
 * Copyright 2021 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef MSKPSlide_DEFINED
#define MSKPSlide_DEFINED

#include "tools/MSKPPlayer.h"
#include "tools/viewer/Slide.h"

class SkStreamSeekable;

class MSKPSlide : public Slide {
public:
    MSKPSlide(const SkString& name, const SkString& path);
    MSKPSlide(const SkString& name, std::unique_ptr<SkStreamSeekable>);

    SkISize getDimensions() const override;

    void draw(SkCanvas* canvas) override;
    bool animate(double nanos) override;
    void load(SkScalar winWidth, SkScalar winHeight) override;
    void unload() override;
    void gpuTeardown() override;

private:
    std::unique_ptr<SkStreamSeekable> fStream;
    std::unique_ptr<MSKPPlayer>       fPlayer;

    int    fFrame         = 0;
    int    fFPS           = 15;
    bool   fPaused        = false;
    double fLastFrameTime = -1;

    bool fShowFrameBounds = false;

    // Default to transparent black, which is correct for Android MSKPS.
    float fBackgroundColor[4] = {0, 0, 0, 0};

    using INHERITED = Slide;
};

#endif
