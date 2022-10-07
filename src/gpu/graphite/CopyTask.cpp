/*
 * Copyright 2021 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/graphite/CopyTask.h"

#include "src/gpu/graphite/Buffer.h"
#include "src/gpu/graphite/CommandBuffer.h"
#include "src/gpu/graphite/Log.h"
#include "src/gpu/graphite/Texture.h"
#include "src/gpu/graphite/TextureProxy.h"

namespace skgpu::graphite {

sk_sp<CopyTextureToBufferTask> CopyTextureToBufferTask::Make(sk_sp<TextureProxy> textureProxy,
                                                             SkIRect srcRect,
                                                             sk_sp<Buffer> buffer,
                                                             size_t bufferOffset,
                                                             size_t bufferRowBytes) {
    return sk_sp<CopyTextureToBufferTask>(new CopyTextureToBufferTask(std::move(textureProxy),
                                                                      srcRect,
                                                                      std::move(buffer),
                                                                      bufferOffset,
                                                                      bufferRowBytes));
}

CopyTextureToBufferTask::CopyTextureToBufferTask(sk_sp<TextureProxy> textureProxy,
                                                 SkIRect srcRect,
                                                 sk_sp<Buffer> buffer,
                                                 size_t bufferOffset,
                                                 size_t bufferRowBytes)
        : fTextureProxy(std::move(textureProxy))
        , fSrcRect(srcRect)
        , fBuffer(std::move(buffer))
        , fBufferOffset(bufferOffset)
        , fBufferRowBytes(bufferRowBytes) {
}

CopyTextureToBufferTask::~CopyTextureToBufferTask() {}

bool CopyTextureToBufferTask::prepareResources(ResourceProvider* resourceProvider,
                                                const SkRuntimeEffectDictionary*) {
    if (!fTextureProxy) {
        SKGPU_LOG_E("No texture proxy specified for CopyTextureToBufferTask");
        return false;
    }
    if (!fTextureProxy->instantiate(resourceProvider)) {
        SKGPU_LOG_E("Could not instantiate texture proxy for CopyTextureToBufferTask!");
        return false;
    }
    return true;
}

bool CopyTextureToBufferTask::addCommands(ResourceProvider*, CommandBuffer* commandBuffer) {
    return commandBuffer->copyTextureToBuffer(fTextureProxy->refTexture(),
                                              fSrcRect,
                                              std::move(fBuffer),
                                              fBufferOffset,
                                              fBufferRowBytes);
}

//--------------------------------------------------------------------------------------------------

sk_sp<CopyTextureToTextureTask> CopyTextureToTextureTask::Make(sk_sp<TextureProxy> srcProxy,
                                                               SkIRect srcRect,
                                                               sk_sp<TextureProxy> dstProxy,
                                                               SkIPoint dstPoint) {
    return sk_sp<CopyTextureToTextureTask>(new CopyTextureToTextureTask(std::move(srcProxy),
                                                                        srcRect,
                                                                        std::move(dstProxy),
                                                                        dstPoint));
}

CopyTextureToTextureTask::CopyTextureToTextureTask(sk_sp<TextureProxy> srcProxy,
                                                   SkIRect srcRect,
                                                   sk_sp<TextureProxy> dstProxy,
                                                   SkIPoint dstPoint)
        : fSrcProxy(std::move(srcProxy))
        , fSrcRect(srcRect)
        , fDstProxy(std::move(dstProxy))
        , fDstPoint(dstPoint) {
}

CopyTextureToTextureTask::~CopyTextureToTextureTask() {}

bool CopyTextureToTextureTask::prepareResources(ResourceProvider* resourceProvider,
                                                const SkRuntimeEffectDictionary*) {
    if (!fSrcProxy) {
        SKGPU_LOG_E("No src texture proxy specified for CopyTextureToTextureTask");
        return false;
    }
    if (!fSrcProxy->instantiate(resourceProvider)) {
        SKGPU_LOG_E("Could not instantiate src texture proxy for CopyTextureToTextureTask!");
        return false;
    }
    if (!fDstProxy) {
        SKGPU_LOG_E("No dst texture proxy specified for CopyTextureToTextureTask");
        return false;
    }
    if (!fDstProxy->instantiate(resourceProvider)) {
        SKGPU_LOG_E("Could not instantiate dst texture proxy for CopyTextureToTextureTask!");
        return false;
    }
    return true;
}

bool CopyTextureToTextureTask::addCommands(ResourceProvider*, CommandBuffer* commandBuffer) {
    return commandBuffer->copyTextureToTexture(fSrcProxy->refTexture(),
                                               fSrcRect,
                                               fDstProxy->refTexture(),
                                               fDstPoint);
}

} // namespace skgpu::graphite
