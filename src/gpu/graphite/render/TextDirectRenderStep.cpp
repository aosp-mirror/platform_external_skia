/*
 * Copyright 2022 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/graphite/render/TextDirectRenderStep.h"

#include "src/core/SkPipelineData.h"

#include "include/gpu/graphite/Recorder.h"
#include "include/private/SkSLString.h"
#include "src/gpu/graphite/DrawParams.h"
#include "src/gpu/graphite/DrawWriter.h"
#include "src/gpu/graphite/RecorderPriv.h"
#include "src/gpu/graphite/text/AtlasManager.h"
#include "src/text/gpu/SubRunContainer.h"

using AtlasSubRun = sktext::gpu::AtlasSubRun;

namespace skgpu::graphite {

namespace {
static constexpr DepthStencilSettings kDirectShadingPass = {
        /*frontStencil=*/{},
        /*backStencil=*/ {},
        /*refValue=*/    0,
        /*stencilTest=*/ false,
        /*depthCompare=*/CompareOp::kGEqual,
        /*depthTest=*/   true,
        /*depthWrite=*/  true
};

// We are expecting to sample from up to 4 textures
constexpr int kNumTextAtlasTextures = 4;
}  // namespace

TextDirectRenderStep::TextDirectRenderStep(bool isA8)
        : RenderStep("TextDirectRenderStep",
                     "",
                     Flags::kPerformsShading | Flags::kHasTextures | Flags::kEmitsCoverage,
                     /*uniforms=*/{{"atlasSizeInv", SkSLType::kFloat2}},
                     PrimitiveType::kTriangleStrip,
                     kDirectShadingPass,
                     /*vertexAttrs=*/ {},
                     /*instanceAttrs=*/
                     {{"mat0", VertexAttribType::kFloat3, SkSLType::kFloat3},
                      {"mat1", VertexAttribType::kFloat3, SkSLType::kFloat3},
                      {"mat2", VertexAttribType::kFloat3, SkSLType::kFloat3},
                      {"uvScale", VertexAttribType::kUShort2, SkSLType::kUShort2},
                      {"uvPos", VertexAttribType::kUShort2, SkSLType::kUShort2},
                      {"depth", VertexAttribType::kUShort_norm, SkSLType::kFloat}},
                     /*varyings=*/
                     {{"textureCoords", SkSLType::kFloat2},
                      {"texIndex", SkSLType::kFloat}})
        , fIsA8(isA8) {}

TextDirectRenderStep::~TextDirectRenderStep() {}

const char* TextDirectRenderStep::vertexSkSL() const {
    return R"(
        float2 baseCoords = float2(float(sk_VertexID >> 1), float(sk_VertexID & 1));
        baseCoords *= float2(uvScale);
        float3 position = baseCoords.x*mat0 + baseCoords.y*mat1 + mat2;
        int texIdx = (int)(uvPos.x >> 13);
        float2 unormTexCoords = baseCoords + float2(uvPos.x & 0x1fff, uvPos.y);

        textureCoords = unormTexCoords * atlasSizeInv;
        texIndex = float(texIdx);

        float4 devPosition = float4(position.xy, depth, position.z);
    )";
}

std::string TextDirectRenderStep::texturesAndSamplersSkSL(int binding) const {
    std::string result;

    for (unsigned int i = 0; i < kNumTextAtlasTextures; ++i) {
        SkSL::String::appendf(&result,
                              "layout(binding=%d) uniform sampler2D text_atlas_%d;\n", binding, i);
        binding++;
    }

    return result;
}

const char* TextDirectRenderStep::fragmentCoverageSkSL() const {
    if (fIsA8) {
        return R"(
            half4 texColor;
            if (texIndex == 0) {
               texColor = sample(text_atlas_0, textureCoords).rrrr;
            } else if (texIndex == 1) {
               texColor = sample(text_atlas_1, textureCoords).rrrr;
            } else if (texIndex == 2) {
               texColor = sample(text_atlas_2, textureCoords).rrrr;
            } else if (texIndex == 3) {
               texColor = sample(text_atlas_3, textureCoords).rrrr;
            } else {
               texColor = sample(text_atlas_0, textureCoords).rrrr;
            }
            outputCoverage = texColor;
        )";
    } else {
        return R"(
            half4 texColor;
            if (texIndex == 0) {
               texColor = sample(text_atlas_0, textureCoords);
            } else if (texIndex == 1) {
               texColor = sample(text_atlas_1, textureCoords);
            } else if (texIndex == 2) {
               texColor = sample(text_atlas_2, textureCoords);
            } else if (texIndex == 3) {
               texColor = sample(text_atlas_3, textureCoords);
            } else {
               texColor = sample(text_atlas_0, textureCoords);
            }
            outputCoverage = texColor;
        )";
    }
}

void TextDirectRenderStep::writeVertices(DrawWriter* dw, const DrawParams& params) const {
    const SubRunData& subRunData = params.geometry().subRunData();
    // TODO: pass through the color from the SkPaint via the SubRunData
    uint16_t unormDepth = params.order().depth().bits();

    subRunData.subRun()->fillInstanceData(dw, subRunData.startGlyphIndex(), subRunData.glyphCount(),
                                          unormDepth, params.transform());
}

void TextDirectRenderStep::writeUniformsAndTextures(const DrawParams& params,
                                                    SkPipelineDataGatherer* gatherer) const {
    SkDEBUGCODE(UniformExpectationsValidator uev(gatherer, this->uniforms());)

    const SubRunData& subRunData = params.geometry().subRunData();
    unsigned int numProxies;
    Recorder* recorder = subRunData.recorder();
    const sk_sp<TextureProxy>* proxies =
            recorder->priv().atlasManager()->getProxies(subRunData.subRun()->maskFormat(),
                                                        &numProxies);
    SkASSERT(proxies && numProxies > 0);

    // write uniforms
    skvx::float2 atlasDimensionsInverse = {1.f/proxies[0]->dimensions().width(),
                                           1.f/proxies[0]->dimensions().height()};
    gatherer->write(atlasDimensionsInverse);

    // write textures and samplers
    const SkSamplingOptions kSamplingOptions(SkFilterMode::kNearest);
    constexpr SkTileMode kTileModes[2] = { SkTileMode::kClamp, SkTileMode::kClamp };
    for (unsigned int i = 0; i < numProxies; ++i) {
        gatherer->add(kSamplingOptions, kTileModes, proxies[i]);
    }
    // If the atlas has less than 4 active proxies we still need to set up samplers for the shader.
    for (unsigned int i = numProxies; i < kNumTextAtlasTextures; ++i) {
        gatherer->add(kSamplingOptions, kTileModes, proxies[0]);
    }
}

}  // namespace skgpu::graphite
