/*
 * Copyright 2024 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/graphite/geom/RectBlurData.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkRRect.h"
#include "include/gpu/graphite/Recorder.h"
#include "src/core/SkRRectPriv.h"
#include "src/gpu/BlurUtils.h"
#include "src/gpu/graphite/Caps.h"
#include "src/gpu/graphite/RecorderPriv.h"
#include "src/gpu/graphite/geom/Transform_graphite.h"
#include "src/sksl/SkSLUtil.h"

namespace skgpu::graphite {

std::optional<RectBlurData> RectBlurData::Make(Recorder* recorder,
                                               const Transform& localToDeviceTransform,
                                               float deviceSigma,
                                               const SkRRect& srcRRect) {
    // TODO: Implement SkMatrix functionality used below for Transform.
    SkMatrix localToDevice = localToDeviceTransform;

    SkRRect devRRect;
    const bool devRRectIsValid = srcRRect.transform(localToDevice, &devRRect);
    const bool devRRectIsCircle = devRRectIsValid && SkRRectPriv::IsCircle(devRRect);
    const bool canBeRect = srcRRect.isRect() && localToDevice.preservesRightAngles();
    const bool canBeCircle = (SkRRectPriv::IsCircle(srcRRect) && localToDevice.isSimilarity()) ||
                             devRRectIsCircle;

    if (canBeRect) {
        return MakeRect(recorder, localToDevice, deviceSigma, srcRRect.rect());
    } else if (canBeCircle) {
        // TODO(b/238762890) Support analytic blurring with circles.
        return std::nullopt;
    } else {  // RRect
        // TODO(b/238762890) Support analytic blurring with rrects.
        return std::nullopt;
    }
}

std::optional<RectBlurData> RectBlurData::MakeRect(Recorder* recorder,
                                                   const SkMatrix& localToDevice,
                                                   float devSigma,
                                                   const SkRect& srcRect) {
    SkASSERT(srcRect.isSorted());

    SkRect devRect;
    SkMatrix devToScaledShape;
    if (localToDevice.rectStaysRect()) {
        // We can do everything in device space when the src rect projects to a rect in device
        // space.
        SkAssertResult(localToDevice.mapRect(&devRect, srcRect));

    } else {
        // The view matrix may scale, perhaps anisotropically. But we want to apply our device space
        // sigma to the delta of frag coord from the rect edges. Factor out the scaling to define a
        // space that is purely rotation / translation from device space (and scale from src space).
        // We'll meet in the middle: pre-scale the src rect to be in this space and then apply the
        // inverse of the rotation / translation portion to the frag coord.
        SkMatrix m;
        SkSize scale;
        if (!localToDevice.decomposeScale(&scale, &m)) {
            return std::nullopt;
        }
        if (!m.invert(&devToScaledShape)) {
            return std::nullopt;
        }
        devRect = {srcRect.left() * scale.width(),
                   srcRect.top() * scale.height(),
                   srcRect.right() * scale.width(),
                   srcRect.bottom() * scale.height()};
    }

    if (!recorder->priv().caps()->shaderCaps()->fFloatIs32Bits) {
        // We promote the math that gets us into the Gaussian space to full float when the rect
        // coords are large. If we don't have full float then fail. We could probably clip the rect
        // to an outset device bounds instead.
        if (std::fabs(devRect.left()) > 16000.0f || std::fabs(devRect.top()) > 16000.0f ||
            std::fabs(devRect.right()) > 16000.0f || std::fabs(devRect.bottom()) > 16000.0f) {
            return std::nullopt;
        }
    }

    const float sixSigma = 6.0f * devSigma;
    SkBitmap integralBitmap = skgpu::CreateIntegralTable(sixSigma);
    if (integralBitmap.empty()) {
        return std::nullopt;
    }

    sk_sp<TextureProxy> integral = RecorderPriv::CreateCachedProxy(recorder, integralBitmap);
    if (!integral) {
        return std::nullopt;
    }

    // In the fast variant we think of the midpoint of the integral texture as aligning with the
    // closest rect edge both in x and y. To simplify texture coord calculation we inset the rect so
    // that the edge of the inset rect corresponds to t = 0 in the texture. It actually simplifies
    // things a bit in the !isFast case, too.
    const float threeSigma = 3.0f * devSigma;
    const Rect shapeData = Rect(devRect.left() + threeSigma,
                                devRect.top() + threeSigma,
                                devRect.right() - threeSigma,
                                devRect.bottom() - threeSigma);

    // In our fast variant we find the nearest horizontal and vertical edges and for each do a
    // lookup in the integral texture for each and multiply them. When the rect is less than 6*sigma
    // wide then things aren't so simple and we have to consider both the left and right edge of the
    // rectangle (and similar in y).
    const bool isFast = shapeData.left() <= shapeData.right() && shapeData.top() <= shapeData.bot();

    const float invSixSigma = 1.0f / sixSigma;

    // Determine how much to outset the draw bounds to ensure we hit pixels within 3*sigma.
    float outsetX = 3.0f * devSigma;
    float outsetY = 3.0f * devSigma;
    if (localToDevice.isScaleTranslate()) {
        outsetX /= std::fabs(localToDevice.getScaleX());
        outsetY /= std::fabs(localToDevice.getScaleY());
    } else {
        SkSize scale;
        if (!localToDevice.decomposeScale(&scale, nullptr)) {
            return std::nullopt;
        }
        outsetX /= scale.width();
        outsetY /= scale.height();
    }
    const Rect drawBounds = srcRect.makeOutset(outsetX, outsetY);

    return RectBlurData(drawBounds,
                        SkM44(devToScaledShape),
                        shapeData,
                        ShapeType::kRect,
                        isFast,
                        invSixSigma,
                        integral);
}

}  // namespace skgpu::graphite
