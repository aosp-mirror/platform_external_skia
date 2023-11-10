/*
 * Copyright 2022 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef skgpu_graphite_VulkanResourceProvider_DEFINED
#define skgpu_graphite_VulkanResourceProvider_DEFINED

#include "src/gpu/graphite/ResourceProvider.h"

#include "include/gpu/vk/VulkanTypes.h"
#include "src/gpu/graphite/DescriptorTypes.h"

namespace skgpu::graphite {

class VulkanCommandBuffer;
class VulkanDescriptorSet;
class VulkanFramebuffer;
class VulkanRenderPass;
class VulkanSharedContext;
class VulkanSamplerYcbcrConversion;

class VulkanResourceProvider final : public ResourceProvider {
public:
    static constexpr size_t kIntrinsicConstantSize = sizeof(float) * 4;

    VulkanResourceProvider(SharedContext* sharedContext,
                           SingleOwner*,
                           uint32_t recorderID,
                           size_t resourceBudget,
                           sk_sp<Buffer> intrinsicConstantUniformBuffer);

    ~VulkanResourceProvider() override;

    sk_sp<Texture> createWrappedTexture(const BackendTexture&) override;

    sk_sp<Buffer> refIntrinsicConstantBuffer() const;

    sk_sp<VulkanSamplerYcbcrConversion> findOrCreateCompatibleSamplerYcbcrConversion(
            const VulkanYcbcrConversionInfo& ycbcrInfo) const;

private:
    const VulkanSharedContext* vulkanSharedContext() const;

    sk_sp<GraphicsPipeline> createGraphicsPipeline(const RuntimeEffectDictionary*,
                                                   const GraphicsPipelineDesc&,
                                                   const RenderPassDesc&) override;
    sk_sp<ComputePipeline> createComputePipeline(const ComputePipelineDesc&) override;

    sk_sp<Texture> createTexture(SkISize, const TextureInfo&, skgpu::Budgeted) override;
    sk_sp<Buffer> createBuffer(size_t size, BufferType type, AccessPattern) override;

    sk_sp<Sampler> createSampler(const SkSamplingOptions&,
                                 SkTileMode xTileMode,
                                 SkTileMode yTileMode) override;
    sk_sp<VulkanFramebuffer> createFramebuffer(
            const VulkanSharedContext*,
            const skia_private::TArray<VkImageView>& attachmentViews,
            const VulkanRenderPass&,
            const int width,
            const int height);

    BackendTexture onCreateBackendTexture(SkISize dimensions, const TextureInfo&) override;
    void onDeleteBackendTexture(const BackendTexture&) override;

    sk_sp<VulkanDescriptorSet> findOrCreateDescriptorSet(SkSpan<DescriptorData>);
    // Find or create a compatible (needed when creating a framebuffer and graphics pipeline) or
    // full (needed when beginning a render pass from the command buffer) RenderPass.
    sk_sp<VulkanRenderPass> findOrCreateRenderPass(const RenderPassDesc&,
                                                   bool compatibleOnly);

    VkPipelineCache pipelineCache();

    friend class VulkanCommandBuffer;
    VkPipelineCache fPipelineCache = VK_NULL_HANDLE;

    // Each render pass will need buffer space to record rtAdjust information. To minimize costly
    // allocation calls and searching of the resource cache, we find & store a uniform buffer upon
    // resource provider creation. This way, render passes across all command buffers can simply
    // update the value within this buffer as needed.
    sk_sp<Buffer> fIntrinsicUniformBuffer;
};

} // namespace skgpu::graphite

#endif // skgpu_graphite_VulkanResourceProvider_DEFINED
