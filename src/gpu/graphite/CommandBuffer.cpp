/*
 * Copyright 2021 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/graphite/CommandBuffer.h"

#include "src/core/SkTraceEvent.h"
#include "src/gpu/RefCntedCallback.h"
#include "src/gpu/graphite/Buffer.h"
#include "src/gpu/graphite/ComputePipeline.h"
#include "src/gpu/graphite/GraphicsPipeline.h"
#include "src/gpu/graphite/Sampler.h"
#include "src/gpu/graphite/Texture.h"
#include "src/gpu/graphite/TextureProxy.h"

namespace skgpu::graphite {

CommandBuffer::CommandBuffer() {}

CommandBuffer::~CommandBuffer() {
    this->releaseResources();
}

void CommandBuffer::releaseResources() {
    TRACE_EVENT0("skia.gpu", TRACE_FUNC);

    fTrackedResources.reset();
}

void CommandBuffer::resetCommandBuffer() {
    TRACE_EVENT0("skia.gpu", TRACE_FUNC);

    this->releaseResources();
    this->onResetCommandBuffer();
}

void CommandBuffer::trackResource(sk_sp<Resource> resource) {
    fTrackedResources.push_back(std::move(resource));
}

void CommandBuffer::addFinishedProc(sk_sp<RefCntedCallback> finishedProc) {
    fFinishedProcs.push_back(std::move(finishedProc));
}

void CommandBuffer::callFinishedProcs(bool success) {
    if (!success) {
        for (int i = 0; i < fFinishedProcs.count(); ++i) {
            fFinishedProcs[i]->setFailureResult();
        }
    }
    fFinishedProcs.reset();
}

bool CommandBuffer::addRenderPass(const RenderPassDesc& renderPassDesc,
                                  sk_sp<Texture> colorTexture,
                                  sk_sp<Texture> resolveTexture,
                                  sk_sp<Texture> depthStencilTexture,
                                  const std::vector<std::unique_ptr<DrawPass>>& drawPasses) {
    if (!this->onAddRenderPass(renderPassDesc,
                               colorTexture.get(),
                               resolveTexture.get(),
                               depthStencilTexture.get(),
                               drawPasses)) {
        return false;
    }

    if (colorTexture) {
        this->trackResource(std::move(colorTexture));
    }
    if (resolveTexture) {
        this->trackResource(std::move(resolveTexture));
    }
    if (depthStencilTexture) {
        this->trackResource(std::move(depthStencilTexture));
    }
    // We just assume if you are adding a render pass that the render pass will actually do work. In
    // theory we could have a discard load that doesn't submit any draws, clears, etc. But hopefully
    // something so trivial would be caught before getting here.
    SkDEBUGCODE(fHasWork = true;)

    return true;
}

bool CommandBuffer::addComputePass(const ComputePassDesc& computePassDesc,
                                   sk_sp<ComputePipeline> pipeline,
                                   const std::vector<ResourceBinding>& bindings) {
    if (!this->onAddComputePass(computePassDesc, pipeline.get(), bindings)) {
        return false;
    }

    this->trackResource(std::move(pipeline));

    for (const auto& binding : bindings) {
        this->trackResource(binding.fResource.fBuffer);
    }

    SkDEBUGCODE(fHasWork = true;)

    return true;
}

bool CommandBuffer::copyTextureToBuffer(sk_sp<Texture> texture,
                                        SkIRect srcRect,
                                        sk_sp<Buffer> buffer,
                                        size_t bufferOffset,
                                        size_t bufferRowBytes) {
    SkASSERT(texture);
    SkASSERT(buffer);

    if (!this->onCopyTextureToBuffer(texture.get(), srcRect, buffer.get(), bufferOffset,
                                     bufferRowBytes)) {
        return false;
    }

    this->trackResource(std::move(texture));
    this->trackResource(std::move(buffer));

    SkDEBUGCODE(fHasWork = true;)

    return true;
}

bool CommandBuffer::copyBufferToTexture(const Buffer* buffer,
                                        sk_sp<Texture> texture,
                                        const BufferTextureCopyData* copyData,
                                        int count) {
    SkASSERT(buffer);
    SkASSERT(texture);
    SkASSERT(count > 0 && copyData);

    if (!this->onCopyBufferToTexture(buffer, texture.get(), copyData, count)) {
        return false;
    }

    this->trackResource(std::move(texture));

    SkDEBUGCODE(fHasWork = true;)

    return true;
}

bool CommandBuffer::copyTextureToTexture(sk_sp<Texture> src,
                                         SkIRect srcRect,
                                         sk_sp<Texture> dst,
                                         SkIPoint dstPoint) {
    SkASSERT(src);
    SkASSERT(dst);

    if (!this->onCopyTextureToTexture(src.get(), srcRect, dst.get(), dstPoint)) {
        return false;
    }

    this->trackResource(std::move(src));
    this->trackResource(std::move(dst));

    SkDEBUGCODE(fHasWork = true;)

    return true;
}

bool CommandBuffer::synchronizeBufferToCpu(sk_sp<Buffer> buffer) {
    SkASSERT(buffer);

    bool didResultInWork = false;
    if (!this->onSynchronizeBufferToCpu(buffer.get(), &didResultInWork)) {
        return false;
    }

    if (didResultInWork) {
        this->trackResource(std::move(buffer));
        SkDEBUGCODE(fHasWork = true;)
    }

    return true;
}

#ifdef SK_ENABLE_PIET_GPU
void CommandBuffer::renderPietScene(const skgpu::piet::Scene& scene, sk_sp<Texture> target) {
    this->onRenderPietScene(scene, target.get());
    this->trackResource(std::move(target));
}
#endif

} // namespace skgpu::graphite
