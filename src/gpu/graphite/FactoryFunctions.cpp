/*
 * Copyright 2022 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/graphite/FactoryFunctions.h"

#include "include/gpu/graphite/precompile/PrecompileBase.h"
#include "include/gpu/graphite/precompile/PrecompileBlender.h"
#include "include/gpu/graphite/precompile/PrecompileColorFilter.h"
#include "include/gpu/graphite/precompile/PrecompileShader.h"
#include "src/gpu/graphite/KeyContext.h"
#include "src/gpu/graphite/KeyHelpers.h"
#include "src/gpu/graphite/PaintParams.h"
#include "src/gpu/graphite/PaintParamsKey.h"
#include "src/gpu/graphite/precompile/PrecompileBaseComplete.h"
#include "src/gpu/graphite/precompile/PrecompileBasePriv.h"
#include "src/gpu/graphite/precompile/PrecompileBlenderPriv.h"
#include "src/gpu/graphite/precompile/PrecompileShaderPriv.h"

namespace skgpu::graphite {

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

//--------------------------------------------------------------------------------------------------
class PrecompileYUVImageShader : public PrecompileShader {
public:
    PrecompileYUVImageShader() {}

private:
    // non-cubic and cubic sampling
    inline static constexpr int kNumIntrinsicCombinations = 2;

    int numIntrinsicCombinations() const override { return kNumIntrinsicCombinations; }

    void addToKey(const KeyContext& keyContext,
                  PaintParamsKeyBuilder* builder,
                  PipelineDataGatherer* gatherer,
                  int desiredCombination) const override {
        SkASSERT(desiredCombination < kNumIntrinsicCombinations);

        static constexpr SkSamplingOptions kDefaultCubicSampling(SkCubicResampler::Mitchell());
        static constexpr SkSamplingOptions kDefaultSampling;

        YUVImageShaderBlock::ImageData imgData(desiredCombination == 1 ? kDefaultCubicSampling
                                                                       : kDefaultSampling,
                                               SkTileMode::kClamp, SkTileMode::kClamp,
                                               SkISize::MakeEmpty(), SkRect::MakeEmpty());

        YUVImageShaderBlock::AddBlock(keyContext, builder, gatherer, imgData);
    }
};

sk_sp<PrecompileShader> PrecompileShaders::YUVImage() {
    return sk_make_sp<PrecompileYUVImageShader>();
}

//--------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------
namespace {

int num_options_in_set(const SkSpan<const sk_sp<PrecompileBase>>& optionSet) {
    int numOptions = 0;
    for (const sk_sp<PrecompileBase>& childOption : optionSet) {
        // A missing child will fall back to a passthrough object
        if (childOption) {
            numOptions += childOption->priv().numCombinations();
        } else {
            ++numOptions;
        }
    }

    return numOptions;
}

} // anonymous namespace

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

        fNumSlotCombinations.reserve(childOptions.size());
        fNumChildCombinations = 1;
        for (const std::vector<sk_sp<PrecompileBase>>& optionSet : fChildOptions) {
            fNumSlotCombinations.push_back(num_options_in_set(optionSet));
            fNumChildCombinations *= fNumSlotCombinations.back();
        }

        SkASSERT(fChildOptions.size() == fEffect->children().size());
    }

private:
    int numChildCombinations() const override { return fNumChildCombinations; }

    void addToKey(const KeyContext& keyContext,
                  PaintParamsKeyBuilder* builder,
                  PipelineDataGatherer* gatherer,
                  int desiredCombination) const override {

        SkASSERT(desiredCombination < this->numCombinations());

        SkSpan<const SkRuntimeEffect::Child> childInfo = fEffect->children();

        RuntimeEffectBlock::BeginBlock(keyContext, builder, gatherer, { fEffect });

        KeyContextWithScope childContext(keyContext, KeyContext::Scope::kRuntimeEffect);

        int remainingCombinations = desiredCombination;

        for (size_t rowIndex = 0; rowIndex < fChildOptions.size(); ++rowIndex) {
            const std::vector<sk_sp<PrecompileBase>>& slotOptions = fChildOptions[rowIndex];
            int numSlotCombinations = fNumSlotCombinations[rowIndex];

            const int slotOption = remainingCombinations % numSlotCombinations;
            remainingCombinations /= numSlotCombinations;

            auto [option, childOptions] = PrecompileBase::SelectOption(
                    SkSpan<const sk_sp<PrecompileBase>>(slotOptions),
                    slotOption);

            SkASSERT(precompilebase_is_valid_as_child(option.get()));
            if (option) {
                option->priv().addToKey(keyContext, builder, gatherer, childOptions);
            } else {
                SkASSERT(childOptions == 0);

                // We don't have a child effect. Substitute in a no-op effect.
                switch (childInfo[rowIndex].type) {
                    case SkRuntimeEffect::ChildType::kShader:
                        // A missing shader returns transparent black
                        SolidColorShaderBlock::AddBlock(childContext, builder, gatherer,
                                                        SK_PMColor4fTRANSPARENT);
                        break;

                    case SkRuntimeEffect::ChildType::kColorFilter:
                        // A "passthrough" shader returns the input color as-is.
                        builder->addBlock(BuiltInCodeSnippetID::kPriorOutput);
                        break;

                    case SkRuntimeEffect::ChildType::kBlender:
                        // A "passthrough" blender performs `blend_src_over(src, dest)`.
                        AddKnownModeBlend(childContext, builder, gatherer, SkBlendMode::kSrcOver);
                        break;
                }
            }
        }

        builder->endBlock();
    }

    sk_sp<SkRuntimeEffect> fEffect;
    std::vector<std::vector<sk_sp<PrecompileBase>>> fChildOptions;
    skia_private::TArray<int> fNumSlotCombinations;
    int fNumChildCombinations;
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
    return sk_make_sp<PrecompileRTEffect<PrecompileColorFilter>>(std::move(effect), childOptions);
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
