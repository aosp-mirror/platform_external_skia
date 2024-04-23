/*
 * Copyright 2024 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/graphite/render/RectBlurRenderStep.h"

#include "src/gpu/graphite/ContextUtils.h"
#include "src/gpu/graphite/DrawParams.h"
#include "src/gpu/graphite/DrawWriter.h"
#include "src/gpu/graphite/PipelineData.h"
#include "src/gpu/graphite/render/CommonDepthStencilSettings.h"

namespace skgpu::graphite {

RectBlurRenderStep::RectBlurRenderStep()
        : RenderStep("RectBlurRenderStep",
                     "",
                     Flags::kPerformsShading | Flags::kHasTextures | Flags::kEmitsCoverage,
                     /*uniforms=*/
                     {{"localToDevice", SkSLType::kFloat4x4},
                      {"deviceToScaledShape", SkSLType::kFloat3x3},
                      {"shapeData", SkSLType::kFloat4},
                      {"depth", SkSLType::kFloat},
                      {"shapeType", SkSLType::kInt},
                      {"isFast", SkSLType::kInt},
                      {"invSixSigma", SkSLType::kHalf}},
                     PrimitiveType::kTriangleStrip,
                     kDirectDepthGreaterPass,
                     /*vertexAttrs=*/
                     {{"position", VertexAttribType::kFloat2, SkSLType::kFloat2},
                      {"ssboIndices", VertexAttribType::kUShort2, SkSLType::kUShort2}},
                     /*instanceAttrs=*/{},
                     /*varyings=*/
                     // scaledShapeCoords are the fragment coordinates in local shape space, where
                     // the shape has been scaled to device space but not translated or rotated.
                     {{"scaledShapeCoords", SkSLType::kFloat2}}) {}

std::string RectBlurRenderStep::vertexSkSL() const {
    return R"(
        float4 devPosition = localToDevice * float4(position, depth, 1.0);
        stepLocalCoords = position;
        scaledShapeCoords = (deviceToScaledShape * devPosition.xy1).xy;
    )";
}

std::string RectBlurRenderStep::texturesAndSamplersSkSL(
        const ResourceBindingRequirements& bindingReqs, int* nextBindingIndex) const {
    return EmitSamplerLayout(bindingReqs, nextBindingIndex) + " sampler2D s;";
}

const char* RectBlurRenderStep::fragmentCoverageSkSL() const {
    return "outputCoverage = blur_coverage_fn(scaledShapeCoords, "
                                             "shapeData, "
                                             "shapeType, "
                                             "isFast, "
                                             "invSixSigma, "
                                             "s);";
}

void RectBlurRenderStep::writeVertices(DrawWriter* writer,
                                       const DrawParams& params,
                                       skvx::ushort2 ssboIndices) const {
    const Rect& r = params.geometry().rectBlurData().drawBounds();
    DrawWriter::Vertices verts{*writer};
    verts.append(4) << skvx::float2(r.left(), r.top()) << ssboIndices
                    << skvx::float2(r.right(), r.top()) << ssboIndices
                    << skvx::float2(r.left(), r.bot()) << ssboIndices
                    << skvx::float2(r.right(), r.bot()) << ssboIndices;
}

void RectBlurRenderStep::writeUniformsAndTextures(const DrawParams& params,
                                                  PipelineDataGatherer* gatherer) const {
    SkDEBUGCODE(UniformExpectationsValidator uev(gatherer, this->uniforms());)

    gatherer->write(params.transform().matrix());

    const RectBlurData& blur = params.geometry().rectBlurData();
    gatherer->write(blur.deviceToScaledShape().asM33());
    gatherer->write(blur.shapeData().asSkRect());
    gatherer->write(params.order().depthAsFloat());
    gatherer->write(static_cast<int>(blur.shapeType()));
    gatherer->write(blur.isFast());
    gatherer->writeHalf(blur.invSixSigma());

    SkSamplingOptions samplingOptions = blur.shapeType() == RectBlurData::ShapeType::kRect
                                                ? SkFilterMode::kLinear
                                                : SkFilterMode::kNearest;
    constexpr SkTileMode kTileModes[2] = {SkTileMode::kClamp, SkTileMode::kClamp};
    gatherer->add(samplingOptions, kTileModes, blur.refProxy());
}

}  // namespace skgpu::graphite
