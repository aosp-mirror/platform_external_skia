/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkRasterPipeline.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkWriteBuffer.h"

#ifdef SK_ENABLE_SKSL
#include "src/core/SkKeyHelpers.h"
#include "src/core/SkPaintParamsKey.h"
#endif

#include "src/shaders/gradients/SkGradientShaderBase.h"

class SkShaderCodeDictionary;

namespace {

SkMatrix rad_to_unit_matrix(const SkPoint& center, SkScalar radius) {
    SkScalar    inv = SkScalarInvert(radius);

    SkMatrix matrix;
    matrix.setTranslate(-center.fX, -center.fY);
    matrix.postScale(inv, inv);
    return matrix;
}

}  // namespace

/////////////////////////////////////////////////////////////////////
class SkRadialGradient final : public SkGradientShaderBase {
public:
    SkRadialGradient(const SkPoint& center, SkScalar radius, const Descriptor&);

    GradientType asAGradient(GradientInfo* info) const override;
#if SK_SUPPORT_GPU
    std::unique_ptr<GrFragmentProcessor> asFragmentProcessor(const GrFPArgs&) const override;
#endif
#ifdef SK_ENABLE_SKSL
    void addToKey(const SkKeyContext&,
                  SkPaintParamsKeyBuilder*,
                  SkPipelineDataGatherer*) const override;
#endif
protected:
    SkRadialGradient(SkReadBuffer& buffer);
    void flatten(SkWriteBuffer& buffer) const override;

    void appendGradientStages(SkArenaAlloc* alloc, SkRasterPipeline* tPipeline,
                              SkRasterPipeline* postPipeline) const override;

    skvm::F32 transformT(skvm::Builder*, skvm::Uniforms*,
                         skvm::Coord coord, skvm::I32* mask) const final;

private:
    friend void ::SkRegisterRadialGradientShaderFlattenable();
    SK_FLATTENABLE_HOOKS(SkRadialGradient)

    const SkPoint fCenter;
    const SkScalar fRadius;
};

SkRadialGradient::SkRadialGradient(const SkPoint& center, SkScalar radius, const Descriptor& desc)
    : SkGradientShaderBase(desc, rad_to_unit_matrix(center, radius))
    , fCenter(center)
    , fRadius(radius) {
}

SkShader::GradientType SkRadialGradient::asAGradient(GradientInfo* info) const {
    if (info) {
        commonAsAGradient(info);
        info->fPoint[0] = fCenter;
        info->fRadius[0] = fRadius;
    }
    return kRadial_GradientType;
}

sk_sp<SkFlattenable> SkRadialGradient::CreateProc(SkReadBuffer& buffer) {
    DescriptorScope desc;
    if (!desc.unflatten(buffer)) {
        return nullptr;
    }
    const SkPoint center = buffer.readPoint();
    const SkScalar radius = buffer.readScalar();
    return SkGradientShader::MakeRadial(center, radius, desc.fColors, std::move(desc.fColorSpace),
                                        desc.fPos, desc.fCount, desc.fTileMode, desc.fGradFlags,
                                        desc.fLocalMatrix);
}

void SkRadialGradient::flatten(SkWriteBuffer& buffer) const {
    this->SkGradientShaderBase::flatten(buffer);
    buffer.writePoint(fCenter);
    buffer.writeScalar(fRadius);
}

void SkRadialGradient::appendGradientStages(SkArenaAlloc*, SkRasterPipeline* p,
                                            SkRasterPipeline*) const {
    p->append(SkRasterPipeline::xy_to_radius);
}

skvm::F32 SkRadialGradient::transformT(skvm::Builder* p, skvm::Uniforms*,
                                       skvm::Coord coord, skvm::I32* mask) const {
    return sqrt(coord.x*coord.x + coord.y*coord.y);
}

/////////////////////////////////////////////////////////////////////

#if SK_SUPPORT_GPU

#include "src/core/SkRuntimeEffectPriv.h"
#include "src/gpu/ganesh/effects/GrSkSLFP.h"
#include "src/gpu/ganesh/gradients/GrGradientShader.h"


std::unique_ptr<GrFragmentProcessor> SkRadialGradient::asFragmentProcessor(
        const GrFPArgs& args) const {
    static const SkRuntimeEffect* effect = SkMakeRuntimeEffect(SkRuntimeEffect::MakeForShader, R"(
        half4 main(float2 coord) {
            return half4(half(length(coord)), 1, 0, 0); // y = 1 for always valid
        }
    )");
    // The radial gradient never rejects a pixel so it doesn't change opacity
    auto fp = GrSkSLFP::Make(effect, "RadialLayout", /*inputFP=*/nullptr,
                             GrSkSLFP::OptFlags::kPreservesOpaqueInput);
    return GrGradientShader::MakeGradientFP(*this, args, std::move(fp));
}

#endif

#ifdef SK_ENABLE_SKSL
void SkRadialGradient::addToKey(const SkKeyContext& keyContext,
                                SkPaintParamsKeyBuilder* builder,
                                SkPipelineDataGatherer* gatherer) const {
    GradientShaderBlocks::GradientData data(kRadial_GradientType,
                                            SkM44(this->getLocalMatrix()),
                                            fCenter, { 0.0f, 0.0f },
                                            fRadius, 0.0f,
                                            0.0f, 0.0f,
                                            fTileMode,
                                            fColorCount,
                                            fOrigColors4f,
                                            fOrigPos);

    GradientShaderBlocks::BeginBlock(keyContext, builder, gatherer, data);
    builder->endBlock();
}
#endif

sk_sp<SkShader> SkGradientShader::MakeRadial(const SkPoint& center, SkScalar radius,
                                             const SkColor4f colors[],
                                             sk_sp<SkColorSpace> colorSpace,
                                             const SkScalar pos[],
                                             int colorCount,
                                             SkTileMode mode,
                                             uint32_t flags,
                                             const SkMatrix* localMatrix) {
    if (radius < 0) {
        return nullptr;
    }
    if (!SkGradientShaderBase::ValidGradient(colors, pos, colorCount, mode)) {
        return nullptr;
    }
    if (1 == colorCount) {
        return SkShaders::Color(colors[0], std::move(colorSpace));
    }
    if (localMatrix && !localMatrix->invert(nullptr)) {
        return nullptr;
    }

    if (SkScalarNearlyZero(radius, SkGradientShaderBase::kDegenerateThreshold)) {
        // Degenerate gradient optimization, and no special logic needed for clamped radial gradient
        return SkGradientShaderBase::MakeDegenerateGradient(colors, pos, colorCount,
                                                            std::move(colorSpace), mode);
    }

    SkGradientShaderBase::ColorStopOptimizer opt(colors, pos, colorCount, mode);

    SkGradientShaderBase::Descriptor desc(opt.fColors, std::move(colorSpace), opt.fPos,
                                          opt.fCount, mode, flags, localMatrix);
    return sk_make_sp<SkRadialGradient>(center, radius, desc);
}

sk_sp<SkShader> SkGradientShader::MakeRadial(const SkPoint& center, SkScalar radius,
                                             const SkColor colors[],
                                             const SkScalar pos[],
                                             int colorCount,
                                             SkTileMode mode,
                                             uint32_t flags,
                                             const SkMatrix* localMatrix) {
    SkColorConverter converter(colors, colorCount);
    return MakeRadial(center, radius, converter.fColors4f.begin(), nullptr, pos, colorCount, mode,
                      flags, localMatrix);
}

sk_sp<SkShader> SkGradientShader::MakeRadial(const SkPoint& center, SkScalar radius,
                                             const SkColor4f colors[],
                                             sk_sp<SkColorSpace> colorSpace,
                                             const SkScalar pos[],
                                             int count,
                                             SkTileMode mode) {
    return MakeRadial(center, radius, colors, std::move(colorSpace), pos, count, mode, 0, nullptr);
}

void SkRegisterRadialGradientShaderFlattenable() {
    SK_REGISTER_FLATTENABLE(SkRadialGradient);
}
