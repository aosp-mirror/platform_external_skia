/*
 * Copyright 2022 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef skgpu_graphite_FactoryFunctions_DEFINED
#define skgpu_graphite_FactoryFunctions_DEFINED

#include "include/core/SkTypes.h"

#ifdef SK_ENABLE_PRECOMPILE

#include "include/core/SkBlendMode.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSpan.h"
#include "include/effects/SkRuntimeEffect.h"

namespace skgpu::graphite {

class PrecompileBase;
class PrecompileBlender;
class PrecompileColorFilter;
class PrecompileImageFilter;
class PrecompileMaskFilter;
class PrecompileShader;

// All of these factory functions will be moved elsewhere once the pre-compile API becomes public

//--------------------------------------------------------------------------------------------------
// This will move to be beside SkShaders in include/core/SkShader.h
class PrecompileShaders {
public:
    //TODO: Add Empty? - see skbug.com/12165
    static sk_sp<PrecompileShader> Color();
    static sk_sp<PrecompileShader> Blend(SkSpan<const sk_sp<PrecompileBlender>> blenders,
                                         SkSpan<const sk_sp<PrecompileShader>> dsts,
                                         SkSpan<const sk_sp<PrecompileShader>> srcs);
    static sk_sp<PrecompileShader> Blend(SkSpan<SkBlendMode> blendModes,
                                         SkSpan<const sk_sp<PrecompileShader>> dsts,
                                         SkSpan<const sk_sp<PrecompileShader>> srcs);
    // TODO: add an SkShaders::Image to match this and SkImageFilters (skbug.com/13440)
    static sk_sp<PrecompileShader> Image();

    // TODO: make SkGradientShader match this convention (skbug.com/13438)
    static sk_sp<PrecompileShader> LinearGradient();
    static sk_sp<PrecompileShader> RadialGradient();
    static sk_sp<PrecompileShader> TwoPointConicalGradient();
    static sk_sp<PrecompileShader> SweepGradient();

private:
    PrecompileShaders() = delete;
};

//--------------------------------------------------------------------------------------------------
// Initially this will go next to SkMaskFilter in include/core/SkMaskFilter.h but the
// SkMaskFilter::MakeBlur factory should be split out or removed. This namespace will follow
// where ever that factory goes.
class PrecompileMaskFilters {
public:
    // TODO: change SkMaskFilter::MakeBlur to match this and SkImageFilters::Blur (skbug.com/13441)
    static sk_sp<PrecompileMaskFilter> Blur();

private:
    PrecompileMaskFilters() = delete;
};

//--------------------------------------------------------------------------------------------------
// This will move to be beside SkColorFilters in include/core/SkColorFilter.h
class PrecompileColorFilters {
public:
    static sk_sp<PrecompileColorFilter> Matrix();
    // TODO: Compose, Blend, HSLAMatrix, LinearToSRGBGamma, SRGBToLinearGamma, Lerp

private:
    PrecompileColorFilters() = delete;
};

//--------------------------------------------------------------------------------------------------
// This will move to be beside SkImageFilters in include/effects/SkImageFilters.h
class PrecompileImageFilters {
public:
    static sk_sp<PrecompileImageFilter> Blur();
    static sk_sp<PrecompileImageFilter> Image();
    // TODO: AlphaThreshold, Arithmetic, Blend (2 kinds), ColorFilter, Compose, DisplacementMap,
    // DropShadow, DropShadowOnly, Magnifier, MatrixConvolution, MatrixTransform, Merge, Offset,
    // Picture, Runtime, Shader, Tile, Dilate, Erode, DistantLitDiffuse, PointLitDiffuse,
    // SpotLitDiffuse, DistantLitSpecular, PointLitSpecular, SpotLitSpecular

private:
    PrecompileImageFilters() = delete;
};

//--------------------------------------------------------------------------------------------------
// Object that allows passing a SkPrecompileShader, SkPrecompileColorFilter or
// SkPrecompileBlender as a child
//
// This will moved to be on SkRuntimeEffect
class PrecompileChildPtr {
public:
    PrecompileChildPtr() = default;
    PrecompileChildPtr(sk_sp<PrecompileShader>);
    PrecompileChildPtr(sk_sp<PrecompileColorFilter>);
    PrecompileChildPtr(sk_sp<PrecompileBlender>);

    // Asserts that the SkPrecompileBase is either null, or one of the legal derived types
    PrecompileChildPtr(sk_sp<PrecompileBase>);

    std::optional<SkRuntimeEffect::ChildType> type() const;

    PrecompileShader* shader() const;
    PrecompileColorFilter* colorFilter() const;
    PrecompileBlender* blender() const;
    PrecompileBase* base() const { return fChild.get(); }

private:
    sk_sp<PrecompileBase> fChild;
};

using PrecompileChildOptions = SkSpan<const PrecompileChildPtr>;

// These will move to be on SkRuntimeEffect to parallel makeShader, makeColorFilter and
// makeBlender
sk_sp<PrecompileShader> MakePrecompileShader(
        sk_sp<SkRuntimeEffect> effect,
        SkSpan<const PrecompileChildOptions> childOptions = {});

sk_sp<PrecompileColorFilter> MakePrecompileColorFilter(
        sk_sp<SkRuntimeEffect> effect,
        SkSpan<const PrecompileChildOptions> childOptions = {});

sk_sp<PrecompileBlender> MakePrecompileBlender(
        sk_sp<SkRuntimeEffect> effect,
        SkSpan<const PrecompileChildOptions> childOptions = {});

} // namespace skgpu::graphite

#endif // SK_ENABLE_PRECOMPILE

#endif // skgpu_graphite_FactoryFunctions_DEFINED
