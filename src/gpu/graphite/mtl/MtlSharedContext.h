/*
 * Copyright 2021 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef skgpu_graphite_MtlSharedContext_DEFINED
#define skgpu_graphite_MtlSharedContext_DEFINED

#include "src/gpu/graphite/SharedContext.h"

#include "include/ports/SkCFObject.h"

#include "include/gpu/graphite/mtl/MtlBackendContext.h"
#include "src/gpu/graphite/mtl/MtlCaps.h"

#import <Metal/Metal.h>

namespace skgpu::graphite {
struct ContextOptions;

class MtlSharedContext final : public SharedContext {
public:
    static sk_sp<SharedContext> Make(const MtlBackendContext&, const ContextOptions&);
    ~MtlSharedContext() override;

    id<MTLDevice> device() const { return fDevice.get(); }
    id<MTLCommandQueue> queue() const { return fQueue.get(); }

    const MtlCaps& mtlCaps() const { return static_cast<const MtlCaps&>(*this->caps()); }

    std::unique_ptr<ResourceProvider> makeResourceProvider(
            sk_sp<GlobalCache>, SingleOwner*) const override;

private:
    MtlSharedContext(sk_cfp<id<MTLDevice>>, sk_cfp<id<MTLCommandQueue>>, sk_sp<const MtlCaps>);

    BackendTexture onCreateBackendTexture(SkISize dimensions, const TextureInfo&) override;
    void onDeleteBackendTexture(BackendTexture&) override;

#if GRAPHITE_TEST_UTILS
    void testingOnly_startCapture() override;
    void testingOnly_endCapture() override;
#endif

    sk_cfp<id<MTLDevice>> fDevice;
    sk_cfp<id<MTLCommandQueue>> fQueue;
};

} // namespace skgpu::graphite

#endif // skgpu_graphite_MtlSharedContext_DEFINED
