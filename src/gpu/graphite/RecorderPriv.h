/*
 * Copyright 2022 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef skgpu_graphite_RecorderPriv_DEFINED
#define skgpu_graphite_RecorderPriv_DEFINED

#include "include/gpu/graphite/Recorder.h"
#include "src/gpu/graphite/SharedContext.h"

class SkShaderCodeDictionary;

namespace skgpu::graphite {

class TextureProxy;

class RecorderPriv {
public:
    void add(sk_sp<Task>);
    void flushTrackedDevices();

    const Caps* caps() const { return fRecorder->fSharedContext->caps(); }

    ResourceProvider* resourceProvider() { return fRecorder->fResourceProvider.get(); }

    const SkRuntimeEffectDictionary* runtimeEffectDictionary() const {
        return fRecorder->fRuntimeEffectDict.get();
    }
    SkRuntimeEffectDictionary* runtimeEffectDictionary() {
        return fRecorder->fRuntimeEffectDict.get();
    }
    const SkShaderCodeDictionary* shaderCodeDictionary() const {
        return fRecorder->fSharedContext->shaderCodeDictionary();
    }
    SkShaderCodeDictionary* shaderCodeDictionary() {
        return fRecorder->fSharedContext->shaderCodeDictionary();
    }

    const RendererProvider* rendererProvider() const {
        return fRecorder->fSharedContext->rendererProvider();
    }

    UniformDataCache* uniformDataCache() { return fRecorder->fUniformDataCache.get(); }
    TextureDataCache* textureDataCache() { return fRecorder->fTextureDataCache.get(); }
    DrawBufferManager* drawBufferManager() { return fRecorder->fDrawBufferManager.get(); }
    UploadBufferManager* uploadBufferManager() { return fRecorder->fUploadBufferManager.get(); }

    AtlasManager* atlasManager() { return fRecorder->fAtlasManager.get(); }
    TokenTracker* tokenTracker() { return fRecorder->fTokenTracker.get(); }
    sktext::gpu::StrikeCache* strikeCache() { return fRecorder->fStrikeCache.get(); }
    sktext::gpu::TextBlobRedrawCoordinator* textBlobCache() {
        return fRecorder->fTextBlobCache.get();
    }

private:
    explicit RecorderPriv(Recorder* recorder) : fRecorder(recorder) {}
    RecorderPriv& operator=(const RecorderPriv&) = delete;

    // No taking addresses of this type.
    const RecorderPriv* operator&() const = delete;
    RecorderPriv* operator&() = delete;

    Recorder* fRecorder;

    friend class Recorder;  // to construct/copy this type.
};

inline RecorderPriv Recorder::priv() {
    return RecorderPriv(this);
}

inline const RecorderPriv Recorder::priv() const {  // NOLINT(readability-const-return-type)
    return RecorderPriv(const_cast<Recorder*>(this));
}

} // namespace skgpu::graphite

#endif // skgpu_graphite_RecorderPriv_DEFINED
