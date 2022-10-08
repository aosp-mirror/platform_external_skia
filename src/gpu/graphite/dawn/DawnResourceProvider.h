/*
 * Copyright 2022 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef skgpu_graphite_DawnResourceProvider_DEFINED
#define skgpu_graphite_DawnResourceProvider_DEFINED

#include "src/gpu/graphite/ResourceProvider.h"

namespace skgpu::graphite {

class DawnResourceProvider final : public ResourceProvider {
public:
    DawnResourceProvider(SharedContext* sharedContext, SingleOwner*);
    ~DawnResourceProvider() override;

    sk_sp<Texture> createWrappedTexture(const BackendTexture&) override;

private:
    sk_sp<GraphicsPipeline> createGraphicsPipeline(const SkRuntimeEffectDictionary*,
                                                   const GraphicsPipelineDesc&,
                                                   const RenderPassDesc&) override;
    sk_sp<ComputePipeline> createComputePipeline(const ComputePipelineDesc&) override;

    sk_sp<Texture> createTexture(SkISize, const TextureInfo&, SkBudgeted) override;
    sk_sp<Buffer> createBuffer(size_t size, BufferType type, PrioritizeGpuReads) override;

    sk_sp<Sampler> createSampler(const SkSamplingOptions&,
                                 SkTileMode xTileMode,
                                 SkTileMode yTileMode) override;

    BackendTexture onCreateBackendTexture(SkISize dimensions, const TextureInfo&) override;
    void onDeleteBackendTexture(BackendTexture&) override {}
};

} // namespace skgpu::graphite

#endif // skgpu_graphite_DawnResourceProvider_DEFINED
