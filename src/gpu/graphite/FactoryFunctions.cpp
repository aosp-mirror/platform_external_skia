/*
 * Copyright 2022 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkTypes.h"

#ifdef SK_ENABLE_PRECOMPILE

#include "src/gpu/graphite/FactoryFunctions.h"

#include "src/gpu/graphite/KeyContext.h"
#include "src/gpu/graphite/KeyHelpers.h"
#include "src/gpu/graphite/PaintParamsKey.h"
#include "src/gpu/graphite/Precompile.h"
#include "src/gpu/graphite/PrecompileBasePriv.h"
#include "src/shaders/SkShaderBase.h"

namespace skgpu::graphite {

//--------------------------------------------------------------------------------------------------
class PrecompileBlendModeBlender : public PrecompileBlender {
public:
    PrecompileBlendModeBlender(SkBlendMode blendMode) : fBlendMode(blendMode) {}

    std::optional<SkBlendMode> asBlendMode() const final { return fBlendMode; }

private:
    void addToKey(const KeyContext& keyContext,
                  int desiredCombination,
                  PaintParamsKeyBuilder* builder) const override {
        SkASSERT(desiredCombination == 0); // The blend mode blender only ever has one combination

        // The blend mode is used in this BeginBlock! It is used to choose between fixed function
        // and shader-based blending
        BlendModeBlock::BeginBlock(keyContext, builder, /* gatherer= */ nullptr, fBlendMode);
        builder->endBlock();
    }


    SkBlendMode fBlendMode;
};

sk_sp<PrecompileBlender> PrecompileBlender::Mode(SkBlendMode blendMode) {
    return sk_make_sp<PrecompileBlendModeBlender>(blendMode);
}

//--------------------------------------------------------------------------------------------------
class PrecompileColorShader : public PrecompileShader {
public:
    PrecompileColorShader() {}

private:
    void addToKey(const KeyContext& keyContext,
                  int desiredCombination,
                  PaintParamsKeyBuilder* builder) const override {

        SkASSERT(desiredCombination == 0); // The color shader only ever has one combination

        constexpr SkPMColor4f kUnusedColor = { 1, 0, 0, 1 };

        SolidColorShaderBlock::BeginBlock(keyContext, builder, /* gatherer= */ nullptr,
                                          kUnusedColor); // color isn't used w/o a gatherer
        builder->endBlock();
    }

};

sk_sp<PrecompileShader> PrecompileShaders::Color() {
    return sk_make_sp<PrecompileColorShader>();
}

//--------------------------------------------------------------------------------------------------
class PrecompileBlendShader : public PrecompileShader {
public:
    PrecompileBlendShader(SkSpan<const sk_sp<PrecompileBlender>> blenders,
                          SkSpan<const sk_sp<PrecompileShader>> dsts,
                          SkSpan<const sk_sp<PrecompileShader>> srcs)
            : fBlenders(blenders.begin(), blenders.end())
            , fDsts(dsts.begin(), dsts.end())
            , fSrcs(srcs.begin(), srcs.end()) {
    }

private:
    int numChildCombinations() const override {
        // TODO (robertphillips): This computation for blender combinations isn't quite correct but
        // good enough for now. In particular, the 'fBlenders' array could contain a bunch of
        // mode-based blenders that would all reduce to just one or two combinations
        // (PorterDuff and full shader-based blending). Please see the PrecompileBlendShader in
        // https://skia-review.googlesource.com/c/skia/+/606897/ for how I intend to solve this.
        int numBlenderCombos = 0;
        for (auto b : fBlenders) {
            numBlenderCombos += b->numCombinations();
        }
        if (!numBlenderCombos) {
            numBlenderCombos = 1; // fallback to kSrcOver
        }

        int numDstCombos = 0;
        for (auto d : fDsts) {
            numDstCombos += d->numCombinations();
        }

        int numSrcCombos = 0;
        for (auto s : fSrcs) {
            numSrcCombos += s->numCombinations();
        }

        return numBlenderCombos * numDstCombos * numSrcCombos;
    }

    void addToKey(const KeyContext& keyContext,
                  int desiredCombination,
                  PaintParamsKeyBuilder* builder) const override {
    }

    std::vector<sk_sp<PrecompileBlender>> fBlenders;
    std::vector<sk_sp<PrecompileShader>> fDsts;
    std::vector<sk_sp<PrecompileShader>> fSrcs;
};

sk_sp<PrecompileShader> PrecompileShaders::Blend(
        SkSpan<const sk_sp<PrecompileBlender>> blenders,
        SkSpan<const sk_sp<PrecompileShader>> dsts,
        SkSpan<const sk_sp<PrecompileShader>> srcs) {
    return sk_make_sp<PrecompileBlendShader>(std::move(blenders),
                                             std::move(dsts), std::move(srcs));
}

sk_sp<PrecompileShader> PrecompileShaders::Blend(
        SkSpan<SkBlendMode> blendModes,
        SkSpan<const sk_sp<PrecompileShader>> dsts,
        SkSpan<const sk_sp<PrecompileShader>> srcs) {
    std::vector<sk_sp<PrecompileBlender>> tmp;
    tmp.reserve(blendModes.size());
    for (SkBlendMode bm : blendModes) {
        tmp.emplace_back(PrecompileBlender::Mode(bm));
    }

    return sk_make_sp<PrecompileBlendShader>(tmp, std::move(dsts), std::move(srcs));
}

//--------------------------------------------------------------------------------------------------
class PrecompileImageShader : public PrecompileShader {
public:
    PrecompileImageShader() {}

private:
    void addToKey(const KeyContext& keyContext,
                  int desiredCombination,
                  PaintParamsKeyBuilder* builder) const override {
        SkASSERT(desiredCombination == 0);

        ImageShaderBlock::BeginBlock(keyContext, builder,
                                     /* gatherer= */ nullptr, /* imgData= */ nullptr);
        builder->endBlock();
    }
};

sk_sp<PrecompileShader> PrecompileShaders::Image() {
    return sk_make_sp<PrecompileImageShader>();
}

//--------------------------------------------------------------------------------------------------
class PrecompileGradientShader : public PrecompileShader {
public:
    PrecompileGradientShader(SkShaderBase::GradientType type) : fType(type) {}

private:
    /*
     * The gradients currently have two specializations based on the number of stops.
     */
    inline static constexpr int kNumStopVariants = 2;
    inline static constexpr int kStopVariants[kNumStopVariants] = { 4, 8 };

    int numIntrinsicCombinations() const override {
        return kNumStopVariants;
    }

    void addToKey(const KeyContext& keyContext,
                  int desiredCombination,
                  PaintParamsKeyBuilder* builder) const override {
        const int intrinsicCombination = desiredCombination / this->numChildCombinations();
        SkDEBUGCODE(int childCombination = desiredCombination % this->numChildCombinations();)
        SkASSERT(intrinsicCombination < kNumStopVariants);
        SkASSERT(childCombination == 0);

        // Only the type and number of stops are accessed when there is no gatherer
        GradientShaderBlocks::GradientData gradData(fType, kStopVariants[intrinsicCombination]);

        // TODO: we may need SkLocalMatrixShader-wrapped versions too
        GradientShaderBlocks::BeginBlock(keyContext, builder, /* gatherer= */ nullptr, gradData);
        builder->endBlock();
    }

    SkShaderBase::GradientType fType;
};

sk_sp<PrecompileShader> PrecompileShaders::LinearGradient() {
    return sk_make_sp<PrecompileGradientShader>(SkShaderBase::GradientType::kLinear);
}

sk_sp<PrecompileShader> PrecompileShaders::RadialGradient() {
    return sk_make_sp<PrecompileGradientShader>(SkShaderBase::GradientType::kRadial);
}

sk_sp<PrecompileShader> PrecompileShaders::SweepGradient() {
    return sk_make_sp<PrecompileGradientShader>(SkShaderBase::GradientType::kSweep);
}

sk_sp<PrecompileShader> PrecompileShaders::TwoPointConicalGradient() {
    return sk_make_sp<PrecompileGradientShader>(SkShaderBase::GradientType::kConical);
}

//--------------------------------------------------------------------------------------------------
class PrecompileLocalMatrixShader : public PrecompileShader {
public:
    PrecompileLocalMatrixShader(sk_sp<PrecompileShader> wrapped) : fWrapped(std::move(wrapped)) {}

private:
    bool isALocalMatrixShader() const override { return true; }

    int numChildCombinations() const override {
        return fWrapped->numChildCombinations();
    }

    void addToKey(const KeyContext& keyContext,
                  int desiredCombination,
                  PaintParamsKeyBuilder* builder) const override {
        SkASSERT(desiredCombination < fWrapped->numCombinations());

        LocalMatrixShaderBlock::BeginBlock(keyContext, builder,
                                           /* gatherer= */ nullptr, /* lmShaderData= */ nullptr);

        fWrapped->priv().addToKey(keyContext, desiredCombination, builder);

        builder->endBlock();
    }

    sk_sp<PrecompileShader> fWrapped;
};

sk_sp<PrecompileShader> PrecompileShaders::LocalMatrix(sk_sp<PrecompileShader> wrapped) {
    return sk_make_sp<PrecompileLocalMatrixShader>(std::move(wrapped));
}

//--------------------------------------------------------------------------------------------------
class PrecompileColorFilterShader : public PrecompileShader {
public:
    PrecompileColorFilterShader(sk_sp<PrecompileShader> shader, sk_sp<PrecompileColorFilter> cf)
            : fShader(std::move(shader))
            , fColorFilter(std::move(cf)) {}

private:
    int numChildCombinations() const override {
        const int numShaderCombos = fShader->numCombinations();
        const int numColorFilterCombos = fColorFilter->numCombinations();

        return numShaderCombos * numColorFilterCombos;
    }

    void addToKey(const KeyContext& keyContext,
                  int desiredCombination,
                  PaintParamsKeyBuilder* builder) const override {

        SkASSERT(desiredCombination < this->numCombinations());

        const int numShaderCombos = fShader->numCombinations();
        SkDEBUGCODE(int numColorFilterCombos = fColorFilter->numCombinations();)

        int desiredShaderCombination = desiredCombination % numShaderCombos;
        int desiredColorFilterCombination = desiredCombination / numShaderCombos;
        SkASSERT(desiredColorFilterCombination < numColorFilterCombos);

        ColorFilterShaderBlock::BeginBlock(keyContext, builder, /* gatherer= */ nullptr);

        fShader->priv().addToKey(keyContext, desiredShaderCombination, builder);
        fColorFilter->priv().addToKey(keyContext, desiredColorFilterCombination, builder);

        builder->endBlock();
    }

    sk_sp<PrecompileShader> fShader;
    sk_sp<PrecompileColorFilter> fColorFilter;
};

sk_sp<PrecompileShader> PrecompileShaders::ColorFilter(sk_sp<PrecompileShader> shader,
                                                       sk_sp<PrecompileColorFilter> cf) {
    return sk_make_sp<PrecompileColorFilterShader>(std::move(shader), std::move(cf));
}

//--------------------------------------------------------------------------------------------------
class PrecompileBlurMaskFilter : public PrecompileMaskFilter {
public:
    PrecompileBlurMaskFilter() {}

private:
    void addToKey(const KeyContext& keyContext,
                  int desiredCombination,
                  PaintParamsKeyBuilder* builder) const override {
        SkASSERT(desiredCombination == 0);

        // TODO: need to add a BlurMaskFilter Block. This is somewhat blocked on figuring out
        // what we're going to do with the Blur system.
    }
};

sk_sp<PrecompileMaskFilter> PrecompileMaskFilters::Blur() {
    return sk_make_sp<PrecompileBlurMaskFilter>();
}

//--------------------------------------------------------------------------------------------------
class PrecompileMatrixColorFilter : public PrecompileColorFilter {
public:
    PrecompileMatrixColorFilter() {}

private:
    void addToKey(const KeyContext& keyContext,
                  int desiredCombination,
                  PaintParamsKeyBuilder* builder) const override {
        SkASSERT(desiredCombination == 0);

        MatrixColorFilterBlock::BeginBlock(keyContext, builder,
                                           /* gatherer= */ nullptr,
                                           /* matrixCFData= */ nullptr);
        builder->endBlock();
    }
};

sk_sp<PrecompileColorFilter> PrecompileColorFilters::Matrix() {
    return sk_make_sp<PrecompileMatrixColorFilter>();
}

//--------------------------------------------------------------------------------------------------
// TODO: need to figure out how we're going to decompose ImageFilters
sk_sp<PrecompileImageFilter> PrecompileImageFilters::Blur() {
    return nullptr; // sk_make_sp<PrecompileImageFilter>();
}

sk_sp<PrecompileImageFilter> PrecompileImageFilters::Image() {
    return nullptr; // sk_make_sp<PrecompileImageFilter>();
}

//--------------------------------------------------------------------------------------------------
PrecompileChildPtr::PrecompileChildPtr(sk_sp<PrecompileShader> s) : fChild(std::move(s)) {}
PrecompileChildPtr::PrecompileChildPtr(sk_sp<PrecompileColorFilter> cf)
        : fChild(std::move(cf)) {
}
PrecompileChildPtr::PrecompileChildPtr(sk_sp<PrecompileBlender> b) : fChild(std::move(b)) {}

namespace {

#ifdef SK_DEBUG

bool precompilebase_is_valid_as_child(const PrecompileBase *child) {
    if (!child) {
        return true;
    }

    switch (child->type()) {
        case PrecompileBase::Type::kShader:
        case PrecompileBase::Type::kColorFilter:
        case PrecompileBase::Type::kBlender:
            return true;
        default:
            return false;
    }
}

#endif // SK_DEBUG

} // anonymous namespace

PrecompileChildPtr::PrecompileChildPtr(sk_sp<PrecompileBase> child)
        : fChild(std::move(child)) {
    SkASSERT(precompilebase_is_valid_as_child(fChild.get()));
}

std::optional<SkRuntimeEffect::ChildType> PrecompileChildPtr::type() const {
    if (fChild) {
        switch (fChild->type()) {
            case PrecompileBase::Type::kShader:
                return SkRuntimeEffect::ChildType::kShader;
            case PrecompileBase::Type::kColorFilter:
                return SkRuntimeEffect::ChildType::kColorFilter;
            case PrecompileBase::Type::kBlender:
                return SkRuntimeEffect::ChildType::kBlender;
            default:
                break;
        }
    }
    return std::nullopt;
}

PrecompileShader* PrecompileChildPtr::shader() const {
    return (fChild && fChild->type() == PrecompileBase::Type::kShader)
           ? static_cast<PrecompileShader*>(fChild.get())
           : nullptr;
}

PrecompileColorFilter* PrecompileChildPtr::colorFilter() const {
    return (fChild && fChild->type() == PrecompileBase::Type::kColorFilter)
           ? static_cast<PrecompileColorFilter*>(fChild.get())
           : nullptr;
}

PrecompileBlender* PrecompileChildPtr::blender() const {
    return (fChild && fChild->type() == PrecompileBase::Type::kBlender)
           ? static_cast<PrecompileBlender*>(fChild.get())
           : nullptr;
}

//--------------------------------------------------------------------------------------------------
template<typename T>
class PrecompileRTEffect : public T {
public:
    PrecompileRTEffect(sk_sp<SkRuntimeEffect> effect,
                       SkSpan<const PrecompileChildOptions> childOptions)
            : fEffect(std::move(effect)) {
        fChildOptions.reserve(childOptions.size());
        for (PrecompileChildOptions c : childOptions) {
            fChildOptions.push_back({ c.begin(), c.end() });
        }
    }

private:
    int numChildCombinations() const override {
        return fChildOptions.size();
    }

    void addToKey(const KeyContext& keyContext,
                  int desiredCombination,
                  PaintParamsKeyBuilder* builder) const override {
    }

    sk_sp<SkRuntimeEffect> fEffect;
    std::vector<std::vector<PrecompileChildPtr>> fChildOptions;
};

sk_sp<PrecompileShader> MakePrecompileShader(
        sk_sp<SkRuntimeEffect> effect,
        SkSpan<const PrecompileChildOptions> childOptions) {
    // TODO: check that 'effect' has the kAllowShader_Flag bit set and:
    //  for each entry in childOptions:
    //    all the SkPrecompileChildPtrs have the same type as the corresponding child in the effect
    return sk_make_sp<PrecompileRTEffect<PrecompileShader>>(std::move(effect), childOptions);
}

sk_sp<PrecompileColorFilter> MakePrecompileColorFilter(
        sk_sp<SkRuntimeEffect> effect,
        SkSpan<const PrecompileChildOptions> childOptions) {
    // TODO: check that 'effect' has the kAllowColorFilter_Flag bit set and:
    //  for each entry in childOptions:
    //    all the SkPrecompileChildPtrs have the same type as the corresponding child in the effect
    return sk_make_sp<PrecompileRTEffect<PrecompileColorFilter>>(std::move(effect),
                                                                 childOptions);
}

sk_sp<PrecompileBlender> MakePrecompileBlender(
        sk_sp<SkRuntimeEffect> effect,
        SkSpan<const PrecompileChildOptions> childOptions) {
    // TODO: check that 'effect' has the kAllowBlender_Flag bit set and:
    //  for each entry in childOptions:
    //    all the SkPrecompileChildPtrs have the same type as the corresponding child in the effect
    return sk_make_sp<PrecompileRTEffect<PrecompileBlender>>(std::move(effect), childOptions);
}

} // namespace skgpu::graphite

//--------------------------------------------------------------------------------------------------

#endif // SK_ENABLE_PRECOMPILE
