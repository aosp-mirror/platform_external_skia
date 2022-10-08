/*
 * Copyright 2021 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/gpu/graphite/Recorder.h"

#include "include/effects/SkRuntimeEffect.h"
#include "include/gpu/graphite/BackendTexture.h"
#include "include/gpu/graphite/GraphiteTypes.h"
#include "include/gpu/graphite/ImageProvider.h"
#include "include/gpu/graphite/Recording.h"
#include "src/core/SkPipelineData.h"
#include "src/core/SkRuntimeEffectDictionary.h"
#include "src/gpu/AtlasTypes.h"
#include "src/gpu/graphite/Caps.h"
#include "src/gpu/graphite/CommandBuffer.h"
#include "src/gpu/graphite/ContextPriv.h"
#include "src/gpu/graphite/Device.h"
#include "src/gpu/graphite/DrawBufferManager.h"
#include "src/gpu/graphite/GlobalCache.h"
#include "src/gpu/graphite/PipelineDataCache.h"
#include "src/gpu/graphite/RecorderPriv.h"
#include "src/gpu/graphite/ResourceProvider.h"
#include "src/gpu/graphite/SharedContext.h"
#include "src/gpu/graphite/TaskGraph.h"
#include "src/gpu/graphite/Texture.h"
#include "src/gpu/graphite/UploadBufferManager.h"
#include "src/gpu/graphite/UploadTask.h"
#include "src/gpu/graphite/text/AtlasManager.h"
#include "src/image/SkImage_Base.h"
#include "src/text/gpu/StrikeCache.h"
#include "src/text/gpu/TextBlobRedrawCoordinator.h"

namespace skgpu::graphite {

#define ASSERT_SINGLE_OWNER SKGPU_ASSERT_SINGLE_OWNER(this->singleOwner())
#define ASSERT_SINGLE_OWNER_PRIV SKGPU_ASSERT_SINGLE_OWNER(fRecorder->singleOwner())

/*
 * The default image provider doesn't perform any conversion so, by default, Graphite won't
 * draw any non-Graphite-backed images.
 */
class DefaultImageProvider final : public ImageProvider {
public:
    static sk_sp<DefaultImageProvider> Make() {
        return sk_ref_sp(new DefaultImageProvider);
    }

    sk_sp<SkImage> findOrCreate(Recorder* recorder,
                                const SkImage* image,
                                SkImage::RequiredImageProperties) override {
        SkASSERT(!as_IB(image)->isGraphiteBacked());

        return nullptr;
    }

private:
    DefaultImageProvider() {}
};

/**************************************************************************************************/
RecorderOptions::RecorderOptions() = default;
RecorderOptions::RecorderOptions(const RecorderOptions&) = default;
RecorderOptions::~RecorderOptions() = default;

/**************************************************************************************************/
static int32_t next_id() {
    static std::atomic<int32_t> nextID{1};
    int32_t id;
    do {
        id = nextID.fetch_add(1, std::memory_order_relaxed);
    } while (id == SK_InvalidGenID);
    return id;
}

Recorder::Recorder(sk_sp<SharedContext> sharedContext,
                   const RecorderOptions& options)
        : fSharedContext(std::move(sharedContext))
        , fRuntimeEffectDict(std::make_unique<SkRuntimeEffectDictionary>())
        , fGraph(new TaskGraph)
        , fUniformDataCache(new UniformDataCache)
        , fTextureDataCache(new TextureDataCache)
        , fRecorderID(next_id())
        , fAtlasManager(std::make_unique<AtlasManager>(this))
        , fTokenTracker(std::make_unique<TokenTracker>())
        , fStrikeCache(std::make_unique<sktext::gpu::StrikeCache>())
        , fTextBlobCache(std::make_unique<sktext::gpu::TextBlobRedrawCoordinator>(fRecorderID)) {

    fClientImageProvider = options.fImageProvider;
    if (!fClientImageProvider) {
        fClientImageProvider = DefaultImageProvider::Make();
    }

    fResourceProvider = fSharedContext->makeResourceProvider(this->singleOwner());
    fDrawBufferManager.reset(
            new DrawBufferManager(fResourceProvider.get(),
                                  fSharedContext->caps()->requiredUniformBufferAlignment(),
                                  fSharedContext->caps()->requiredStorageBufferAlignment()));
    fUploadBufferManager.reset(new UploadBufferManager(fResourceProvider.get()));
    SkASSERT(fResourceProvider);
}

Recorder::~Recorder() {
    ASSERT_SINGLE_OWNER
    for (auto& device : fTrackedDevices) {
        device->abandonRecorder();
    }

    // TODO: needed?
    fStrikeCache->freeAll();
}

BackendApi Recorder::backend() const { return fSharedContext->backend(); }

std::unique_ptr<Recording> Recorder::snap() {
    ASSERT_SINGLE_OWNER
    for (auto& device : fTrackedDevices) {
        device->flushPendingWorkToRecorder();
    }

    // TODO: fulfill all promise images in the TextureDataCache here
    // TODO: create all the samplers needed in the TextureDataCache here

    if (!fGraph->prepareResources(fResourceProvider.get(), fRuntimeEffectDict.get())) {
        // Leaving 'fTrackedDevices' alone since they were flushed earlier and could still be
        // attached to extant SkSurfaces.
        fDrawBufferManager.reset(
                new DrawBufferManager(fResourceProvider.get(),
                                      fSharedContext->caps()->requiredUniformBufferAlignment(),
                                      fSharedContext->caps()->requiredStorageBufferAlignment()));
        fTextureDataCache = std::make_unique<TextureDataCache>();
        // We leave the UniformDataCache alone
        fGraph->reset();
        fRuntimeEffectDict->reset();
        return nullptr;
    }

    std::unique_ptr<Recording> recording(new Recording(std::move(fGraph)));
    fDrawBufferManager->transferToRecording(recording.get());
    fUploadBufferManager->transferToRecording(recording.get());

    fGraph = std::make_unique<TaskGraph>();
    fRuntimeEffectDict->reset();
    fTextureDataCache = std::make_unique<TextureDataCache>();
    fAtlasManager->evictAtlases();
    return recording;
}

void Recorder::registerDevice(Device* device) {
    ASSERT_SINGLE_OWNER
    fTrackedDevices.push_back(device);
}

void Recorder::deregisterDevice(const Device* device) {
    ASSERT_SINGLE_OWNER
    for (auto it = fTrackedDevices.begin(); it != fTrackedDevices.end(); it++) {
        if (*it == device) {
            fTrackedDevices.erase(it);
            return;
        }
    }
}

#if GR_TEST_UTILS
bool Recorder::deviceIsRegistered(Device* device) {
    ASSERT_SINGLE_OWNER
    for (auto& currentDevice : fTrackedDevices) {
        if (device == currentDevice) {
            return true;
        }
    }
    return false;
}
#endif

BackendTexture Recorder::createBackendTexture(SkISize dimensions, const TextureInfo& info) {
    ASSERT_SINGLE_OWNER

    if (!info.isValid() || info.backend() != this->backend()) {
        return {};
    }
    return fResourceProvider->createBackendTexture(dimensions, info);
}

bool Recorder::updateBackendTexture(const BackendTexture& backendTex,
                                    const SkPixmap srcData[],
                                    int numLevels) {
    ASSERT_SINGLE_OWNER

    if (!backendTex.isValid() || backendTex.backend() != this->backend()) {
        return false;
    }

    if (!srcData || numLevels <= 0) {
        return false;
    }

    // If the texture has MIP levels then we require that the full set is overwritten.
    int numExpectedLevels = 1;
    if (backendTex.info().numMipLevels() > 1) {
        numExpectedLevels = SkMipmap::ComputeLevelCount(backendTex.dimensions().width(),
                                                        backendTex.dimensions().height()) + 1;
    }
    if (numLevels != numExpectedLevels) {
        return false;
    }

    SkColorType ct = srcData[0].colorType();

    if (!this->priv().caps()->areColorTypeAndTextureInfoCompatible(ct, backendTex.info())) {
        return false;
    }

    sk_sp<Texture> texture = this->priv().resourceProvider()->createWrappedTexture(backendTex);
    if (!texture) {
        return false;
    }

    sk_sp<TextureProxy> proxy(new TextureProxy(std::move(texture)));

    std::vector<MipLevel> mipLevels;
    mipLevels.resize(numLevels);

    for (int i = 0; i < numLevels; ++i) {
        SkASSERT(srcData[i].addr());
        SkASSERT(srcData[i].colorType() == ct);

        mipLevels[i].fPixels = srcData[i].addr();
        mipLevels[i].fRowBytes = srcData[i].rowBytes();
    }

    UploadInstance upload = UploadInstance::Make(this,
                                                 std::move(proxy),
                                                 ct,
                                                 mipLevels,
                                                 SkIRect::MakeSize(backendTex.dimensions()));

    sk_sp<Task> uploadTask = UploadTask::Make(upload);

    this->priv().add(std::move(uploadTask));

    return true;
}

void Recorder::deleteBackendTexture(BackendTexture& texture) {
    ASSERT_SINGLE_OWNER

    if (!texture.isValid() || texture.backend() != this->backend()) {
        return;
    }
    fResourceProvider->deleteBackendTexture(texture);
}

void RecorderPriv::add(sk_sp<Task> task) {
    ASSERT_SINGLE_OWNER_PRIV
    fRecorder->fGraph->add(std::move(task));
}

void RecorderPriv::flushTrackedDevices() {
    ASSERT_SINGLE_OWNER_PRIV
    for (Device* device : fRecorder->fTrackedDevices) {
        device->flushPendingWorkToRecorder();
    }
}

} // namespace skgpu::graphite
