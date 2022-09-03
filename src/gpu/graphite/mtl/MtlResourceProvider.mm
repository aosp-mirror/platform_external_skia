/*
 * Copyright 2021 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/graphite/mtl/MtlResourceProvider.h"

#include "include/gpu/ShaderErrorHandler.h"
#include "include/gpu/graphite/BackendTexture.h"

#include "src/core/SkSLTypeShared.h"
#include "src/gpu/Blend.h"
#include "src/gpu/graphite/ContextUtils.h"
#include "src/gpu/graphite/GlobalCache.h"
#include "src/gpu/graphite/GraphicsPipelineDesc.h"
#include "src/gpu/graphite/Renderer.h"
#include "src/gpu/graphite/mtl/MtlBuffer.h"
#include "src/gpu/graphite/mtl/MtlCommandBuffer.h"
#include "src/gpu/graphite/mtl/MtlComputePipeline.h"
#include "src/gpu/graphite/mtl/MtlGraphicsPipeline.h"
#include "src/gpu/graphite/mtl/MtlSampler.h"
#include "src/gpu/graphite/mtl/MtlSharedContext.h"
#include "src/gpu/graphite/mtl/MtlTexture.h"
#include "src/gpu/graphite/mtl/MtlUtils.h"

#import <Metal/Metal.h>

namespace skgpu::graphite {

MtlResourceProvider::MtlResourceProvider(SharedContext* sharedContext,
                                         SingleOwner* singleOwner)
        : ResourceProvider(sharedContext, singleOwner) {}

const MtlSharedContext* MtlResourceProvider::mtlSharedContext() {
    return static_cast<const MtlSharedContext*>(fSharedContext);
}

sk_sp<MtlGraphicsPipeline> MtlResourceProvider::findOrCreateLoadMSAAPipeline(
        const RenderPassDesc& renderPassDesc) {
    uint64_t renderPassKey =
            this->mtlSharedContext()->mtlCaps().getRenderPassDescKey(renderPassDesc);
    sk_sp<MtlGraphicsPipeline> pipeline = fLoadMSAAPipelines[renderPassKey];
    if (!pipeline) {
        static const char* kLoadMSAAShaderText = R"(
                #include <metal_stdlib>
                #include <simd/simd.h>
                using namespace metal;

                typedef struct {
                    float4 position [[position]];
                } VertexOutput;

                vertex VertexOutput vertexMain(uint vertexID [[vertex_id]]) {
                    VertexOutput out;
                    float2 position = float2(float(vertexID >> 1), float(vertexID & 1));
                    out.position = float4(2.0 * position - 1.0, 0.0, 1.0);
                    return out;
                }

                fragment float4 fragmentMain(VertexOutput in [[stage_in]],
                                             texture2d<half> colorMap [[texture(0)]]) {
                    uint2 coords = uint2(in.position.x, in.position.y);
                    half4 colorSample   = colorMap.read(coords);
                    return float4(colorSample);
                }
        )";

        auto mtlLibrary = MtlCompileShaderLibrary(this->mtlSharedContext(),
                                                  kLoadMSAAShaderText,
                                                  fSharedContext->caps()->shaderErrorHandler());

        BlendInfo noBlend{}; // default is equivalent to kSrc blending
        sk_cfp<id<MTLDepthStencilState>> ignoreDS =
                this->findOrCreateCompatibleDepthStencilState({});
        pipeline = MtlGraphicsPipeline::Make(this->mtlSharedContext(),
                                             "LoadMSAAFromResolve",
                                             {mtlLibrary.get(), "vertexMain"},
                                             /*vertexAttrs=*/{},
                                             /*instanceAttrs=*/{},
                                             {mtlLibrary.get(), "fragmentMain"},
                                             std::move(ignoreDS),
                                             /*stencilRefValue=*/0,
                                             noBlend,
                                             renderPassDesc);
        if (pipeline) {
            fLoadMSAAPipelines.set(renderPassKey, pipeline);
        }
    }

    return pipeline;
}

sk_sp<GraphicsPipeline> MtlResourceProvider::createGraphicsPipeline(
        const SkRuntimeEffectDictionary* runtimeDict,
        const GraphicsPipelineDesc& pipelineDesc,
        const RenderPassDesc& renderPassDesc) {
    std::string vsMSL, fsMSL;
    SkSL::Program::Inputs vsInputs, fsInputs;
    SkSL::ProgramSettings settings;

    settings.fForceNoRTFlip = true;

    auto skslCompiler = this->skslCompiler();
    ShaderErrorHandler* errorHandler = fSharedContext->caps()->shaderErrorHandler();

    const RenderStep* step =
            fSharedContext->rendererProvider()->lookup(pipelineDesc.renderStepID());

    bool useShadingSsboIndex =
            fSharedContext->caps()->storageBufferPreferred() && step->performsShading();

    BlendInfo blendInfo;
    bool localCoordsNeeded = false;
    if (!SkSLToMSL(skslCompiler,
                   GetSkSLFS(fSharedContext->shaderCodeDictionary(),
                             runtimeDict,
                             step,
                             pipelineDesc.paintParamsID(),
                             useShadingSsboIndex,
                             &blendInfo,
                             &localCoordsNeeded),
                   SkSL::ProgramKind::kGraphiteFragment,
                   settings,
                   &fsMSL,
                   &fsInputs,
                   errorHandler)) {
        return nullptr;
    }

    if (!SkSLToMSL(skslCompiler,
                   GetSkSLVS(step, useShadingSsboIndex, localCoordsNeeded),
                   SkSL::ProgramKind::kGraphiteVertex,
                   settings,
                   &vsMSL,
                   &vsInputs,
                   errorHandler)) {
        return nullptr;
    }

    auto vsLibrary = MtlCompileShaderLibrary(this->mtlSharedContext(), vsMSL, errorHandler);
    auto fsLibrary = MtlCompileShaderLibrary(this->mtlSharedContext(), fsMSL, errorHandler);

    sk_cfp<id<MTLDepthStencilState>> dss =
            this->findOrCreateCompatibleDepthStencilState(step->depthStencilSettings());

    return MtlGraphicsPipeline::Make(this->mtlSharedContext(),
                                     step->name(),
                                     {vsLibrary.get(), "vertexMain"},
                                     step->vertexAttributes(),
                                     step->instanceAttributes(),
                                     {fsLibrary.get(), "fragmentMain"},
                                     std::move(dss),
                                     step->depthStencilSettings().fStencilReferenceValue,
                                     blendInfo,
                                     renderPassDesc);
}

sk_sp<ComputePipeline> MtlResourceProvider::createComputePipeline(
        const ComputePipelineDesc& pipelineDesc) {
    return MtlComputePipeline::Make(this, this->mtlSharedContext(), pipelineDesc);
}

sk_sp<Texture> MtlResourceProvider::createTexture(SkISize dimensions,
                                                  const TextureInfo& info,
                                                  SkBudgeted budgeted) {
    return MtlTexture::Make(this->mtlSharedContext(), dimensions, info, budgeted);
}

sk_sp<Texture> MtlResourceProvider::createWrappedTexture(const BackendTexture& texture) {
    MtlHandle mtlHandleTexture = texture.getMtlTexture();
    if (!mtlHandleTexture) {
        return nullptr;
    }
    sk_cfp<id<MTLTexture>> mtlTexture = sk_ret_cfp((id<MTLTexture>)mtlHandleTexture);
    return MtlTexture::MakeWrapped(this->mtlSharedContext(),
                                   texture.dimensions(),
                                   texture.info(),
                                   std::move(mtlTexture));
}

sk_sp<Buffer> MtlResourceProvider::createBuffer(size_t size,
                                                BufferType type,
                                                PrioritizeGpuReads prioritizeGpuReads) {
    return MtlBuffer::Make(this->mtlSharedContext(), size, type, prioritizeGpuReads);
}

sk_sp<Sampler> MtlResourceProvider::createSampler(const SkSamplingOptions& samplingOptions,
                                                  SkTileMode xTileMode,
                                                  SkTileMode yTileMode) {
    return MtlSampler::Make(this->mtlSharedContext(), samplingOptions, xTileMode, yTileMode);
}

namespace {
MTLCompareFunction compare_op_to_mtl(CompareOp op) {
    switch (op) {
        case CompareOp::kAlways:
            return MTLCompareFunctionAlways;
        case CompareOp::kNever:
            return MTLCompareFunctionNever;
        case CompareOp::kGreater:
            return MTLCompareFunctionGreater;
        case CompareOp::kGEqual:
            return MTLCompareFunctionGreaterEqual;
        case CompareOp::kLess:
            return MTLCompareFunctionLess;
        case CompareOp::kLEqual:
            return MTLCompareFunctionLessEqual;
        case CompareOp::kEqual:
            return MTLCompareFunctionEqual;
        case CompareOp::kNotEqual:
            return MTLCompareFunctionNotEqual;
    }
}

MTLStencilOperation stencil_op_to_mtl(StencilOp op) {
    switch (op) {
        case StencilOp::kKeep:
            return MTLStencilOperationKeep;
        case StencilOp::kZero:
            return MTLStencilOperationZero;
        case StencilOp::kReplace:
            return MTLStencilOperationReplace;
        case StencilOp::kInvert:
            return MTLStencilOperationInvert;
        case StencilOp::kIncWrap:
            return MTLStencilOperationIncrementWrap;
        case StencilOp::kDecWrap:
            return MTLStencilOperationDecrementWrap;
        case StencilOp::kIncClamp:
            return MTLStencilOperationIncrementClamp;
        case StencilOp::kDecClamp:
            return MTLStencilOperationDecrementClamp;
    }
}

MTLStencilDescriptor* stencil_face_to_mtl(DepthStencilSettings::Face face) {
    MTLStencilDescriptor* result = [[MTLStencilDescriptor alloc] init];
    result.stencilCompareFunction = compare_op_to_mtl(face.fCompareOp);
    result.readMask = face.fReadMask;
    result.writeMask = face.fWriteMask;
    result.depthStencilPassOperation = stencil_op_to_mtl(face.fDepthStencilPassOp);
    result.stencilFailureOperation = stencil_op_to_mtl(face.fStencilFailOp);
    return result;
}
}  // anonymous namespace

sk_cfp<id<MTLDepthStencilState>> MtlResourceProvider::findOrCreateCompatibleDepthStencilState(
            const DepthStencilSettings& depthStencilSettings) {
    sk_cfp<id<MTLDepthStencilState>>* depthStencilState;
    depthStencilState = fDepthStencilStates.find(depthStencilSettings);
    if (!depthStencilState) {
        MTLDepthStencilDescriptor* desc = [[MTLDepthStencilDescriptor alloc] init];
        SkASSERT(depthStencilSettings.fDepthTestEnabled ||
                 depthStencilSettings.fDepthCompareOp == CompareOp::kAlways);
        desc.depthCompareFunction = compare_op_to_mtl(depthStencilSettings.fDepthCompareOp);
        if (depthStencilSettings.fDepthTestEnabled) {
            desc.depthWriteEnabled = depthStencilSettings.fDepthWriteEnabled;
        }
        if (depthStencilSettings.fStencilTestEnabled) {
            desc.frontFaceStencil = stencil_face_to_mtl(depthStencilSettings.fFrontStencil);
            desc.backFaceStencil = stencil_face_to_mtl(depthStencilSettings.fBackStencil);
        }

        sk_cfp<id<MTLDepthStencilState>> dss(
                [this->mtlSharedContext()->device() newDepthStencilStateWithDescriptor: desc]);
        depthStencilState = fDepthStencilStates.set(depthStencilSettings, std::move(dss));
    }

    SkASSERT(depthStencilState);
    return *depthStencilState;
}

BackendTexture MtlResourceProvider::onCreateBackendTexture(SkISize dimensions,
                                                           const TextureInfo& info) {
    sk_cfp<id<MTLTexture>> texture = MtlTexture::MakeMtlTexture(this->mtlSharedContext(),
                                                                dimensions,
                                                                info);
    if (!texture) {
        return {};
    }
    return BackendTexture(dimensions, (Handle)texture.release());
}

void MtlResourceProvider::onDeleteBackendTexture(BackendTexture& texture) {
    SkASSERT(texture.backend() == BackendApi::kMetal);
    MtlHandle texHandle = texture.getMtlTexture();
    SkCFSafeRelease(texHandle);
}

} // namespace skgpu::graphite
