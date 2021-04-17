/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#ifndef GrVkRenderTarget_DEFINED
#define GrVkRenderTarget_DEFINED

#include "src/gpu/GrRenderTarget.h"
#include "src/gpu/vk/GrVkImage.h"

#include "include/gpu/vk/GrVkTypes.h"
#include "src/gpu/vk/GrVkRenderPass.h"
#include "src/gpu/vk/GrVkResourceProvider.h"

class GrVkFramebuffer;
class GrVkGpu;
class GrVkImageView;
class GrVkAttachment;

struct GrVkImageInfo;

class GrVkRenderTarget : public GrRenderTarget {
public:
    static sk_sp<GrVkRenderTarget> MakeWrappedRenderTarget(GrVkGpu*,
                                                           SkISize,
                                                           int sampleCnt,
                                                           const GrVkImageInfo&,
                                                           sk_sp<GrBackendSurfaceMutableStateImpl>);

    static sk_sp<GrVkRenderTarget> MakeSecondaryCBRenderTarget(GrVkGpu*,
                                                               SkISize,
                                                               const GrVkDrawableInfo& vkInfo);

    ~GrVkRenderTarget() override;

    GrBackendFormat backendFormat() const override;

    using SelfDependencyFlags = GrVkRenderPass::SelfDependencyFlags;
    using LoadFromResolve = GrVkRenderPass::LoadFromResolve;

    const GrVkFramebuffer* getFramebuffer(bool withResolve,
                                          bool withStencil,
                                          SelfDependencyFlags selfDepFlags,
                                          LoadFromResolve);
    const GrVkFramebuffer* getFramebuffer(const GrVkRenderPass& renderPass) {
        return this->getFramebuffer(renderPass.hasResolveAttachment(),
                                    renderPass.hasStencilAttachment(),
                                    renderPass.selfDependencyFlags(),
                                    renderPass.loadFromResolve());
    }

    GrVkAttachment* colorAttachment() const {
        SkASSERT(!this->wrapsSecondaryCommandBuffer());
        return fColorAttachment.get();
    }
    const GrVkImageView* colorAttachmentView() const {
        SkASSERT(!this->wrapsSecondaryCommandBuffer());
        return this->colorAttachment()->framebufferView();
    }

    GrVkAttachment* resolveAttachment() const {
        SkASSERT(!this->wrapsSecondaryCommandBuffer());
        return fResolveAttachment.get();
    }
    const GrVkImageView* resolveAttachmentView() const {
        SkASSERT(!this->wrapsSecondaryCommandBuffer());
        return fResolveAttachment->framebufferView();
    }

    const GrManagedResource* stencilImageResource() const;
    const GrVkImageView* stencilAttachmentView() const;

    // Returns the GrVkAttachment of the non-msaa attachment. If the color attachment has 1 sample,
    // then the color attachment will be returned. Otherwise, the resolve attachment is returned.
    // Note that in this second case the resolve attachment may be null if this was created by
    // wrapping an msaa VkImage.
    GrVkAttachment* nonMSAAAttachment() const;

    // Returns the attachment that is used for all external client facing operations. This will be
    // either a wrapped color attachment or the resolve attachment for created VkImages.
    GrVkAttachment* externalAttachment() const {
        return fResolveAttachment ? fResolveAttachment.get() : fColorAttachment.get();
    }

    std::pair<const GrVkRenderPass*, GrVkResourceProvider::CompatibleRPHandle> getSimpleRenderPass(
            bool withResolve,
            bool withStencil,
            SelfDependencyFlags selfDepFlags,
            LoadFromResolve);
    GrVkResourceProvider::CompatibleRPHandle compatibleRenderPassHandle(
            bool withResolve,
            bool withStencil,
            SelfDependencyFlags selfDepFlags,
            LoadFromResolve);

    bool wrapsSecondaryCommandBuffer() const { return SkToBool(fExternalFramebuffer); }
    sk_sp<GrVkFramebuffer> externalFramebuffer() const;

    bool canAttemptStencilAttachment() const override {
        // We don't know the status of the stencil attachment for wrapped external secondary command
        // buffers so we just assume we don't have one.
        return !this->wrapsSecondaryCommandBuffer();
    }

    GrBackendRenderTarget getBackendRenderTarget() const override;

    void getAttachmentsDescriptor(GrVkRenderPass::AttachmentsDescriptor* desc,
                                  GrVkRenderPass::AttachmentFlags* flags,
                                  bool withResolve,
                                  bool withStencil);

    // Reconstruct the render target attachment information from the programInfo. This includes
    // which attachments the render target will have (color, stencil) and the attachments' formats
    // and sample counts - cf. getAttachmentsDescriptor.
    static void ReconstructAttachmentsDescriptor(const GrVkCaps& vkCaps,
                                                 const GrProgramInfo& programInfo,
                                                 GrVkRenderPass::AttachmentsDescriptor* desc,
                                                 GrVkRenderPass::AttachmentFlags* flags);

    // So that we don't need to rewrite descriptor sets each time, we keep a cached input descriptor
    // set on the the RT and simply reuse that descriptor set for this render target only. This call
    // will not ref the GrVkDescriptorSet so the caller must manually ref it if it wants to keep it
    // alive.
    const GrVkDescriptorSet* inputDescSet(GrVkGpu*, bool forResolve);

protected:
    enum class CreateType {
        kDirectlyWrapped, // We need to register this in the ctor
        kFromTextureRT,   // Skip registering this to cache since TexRT will handle it
    };

    GrVkRenderTarget(GrVkGpu* gpu,
                     SkISize dimensions,
                     sk_sp<GrVkAttachment> colorAttachment,
                     sk_sp<GrVkAttachment> resolveAttachment,
                     CreateType createType);

    void onAbandon() override;
    void onRelease() override;

    // This returns zero since the memory should all be handled by the attachments
    size_t onGpuMemorySize() const override { return 0; }

private:
    // For external framebuffers that wrap a secondary command buffer
    GrVkRenderTarget(GrVkGpu* gpu,
                     SkISize dimensions,
                     sk_sp<GrVkFramebuffer> externalFramebuffer);

    void setFlags();

    GrVkGpu* getVkGpu() const;

    GrVkAttachment* dynamicMSAAAttachment();
    GrVkAttachment* msaaAttachment();

    const GrVkRenderPass* createSimpleRenderPass(bool withResolve,
                                                 bool withStencil,
                                                 SelfDependencyFlags selfDepFlags,
                                                 LoadFromResolve);
    const GrVkFramebuffer* createFramebuffer(bool withResolve,
                                             bool withStencil,
                                             SelfDependencyFlags selfDepFlags,
                                             LoadFromResolve);

    bool completeStencilAttachment() override;

    // In Vulkan we call the release proc after we are finished with the underlying
    // GrVkImage::Resource object (which occurs after the GPU has finished all work on it).
    void onSetRelease(sk_sp<GrRefCntedCallback> releaseHelper) override {
        // Forward the release proc on to the GrVkImage of the release attachment if we have one,
        // otherwise the color attachment.
        GrVkAttachment* attachment =
                fResolveAttachment ? fResolveAttachment.get() : fColorAttachment.get();
        attachment->setResourceRelease(std::move(releaseHelper));
    }

    void releaseInternalObjects();

    sk_sp<GrVkAttachment> fColorAttachment;
    sk_sp<GrVkAttachment> fResolveAttachment;
    sk_sp<GrVkAttachment> fDynamicMSAAAttachment;

    // We can have a renderpass with and without resolve attachment, stencil attachment,
    // input attachment dependency, advanced blend dependency, and loading from resolve. All 5 of
    // these being completely orthogonal. Thus we have a total of 32 types of render passes.
    static constexpr int kNumCachedRenderPasses = 32;

    const GrVkFramebuffer*                   fCachedFramebuffers[kNumCachedRenderPasses];
    const GrVkRenderPass*                    fCachedRenderPasses[kNumCachedRenderPasses];
    GrVkResourceProvider::CompatibleRPHandle fCompatibleRPHandles[kNumCachedRenderPasses];

    const GrVkDescriptorSet* fCachedInputDescriptorSet = nullptr;

    sk_sp<GrVkFramebuffer> fExternalFramebuffer;
};

#endif
