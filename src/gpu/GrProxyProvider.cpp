/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/GrProxyProvider.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkImage.h"
#include "include/gpu/GrContext.h"
#include "include/gpu/GrTexture.h"
#include "include/private/GrImageContext.h"
#include "include/private/GrResourceKey.h"
#include "include/private/GrSingleOwner.h"
#include "include/private/SkImageInfoPriv.h"
#include "src/core/SkAutoPixmapStorage.h"
#include "src/core/SkCompressedDataUtils.h"
#include "src/core/SkImagePriv.h"
#include "src/core/SkMipMap.h"
#include "src/core/SkTraceEvent.h"
#include "src/gpu/GrCaps.h"
#include "src/gpu/GrContextPriv.h"
#include "src/gpu/GrImageContextPriv.h"
#include "src/gpu/GrRenderTarget.h"
#include "src/gpu/GrResourceProvider.h"
#include "src/gpu/GrSurfaceProxy.h"
#include "src/gpu/GrSurfaceProxyPriv.h"
#include "src/gpu/GrTextureProxyCacheAccess.h"
#include "src/gpu/GrTextureRenderTargetProxy.h"
#include "src/gpu/SkGr.h"
#include "src/image/SkImage_Base.h"

#define ASSERT_SINGLE_OWNER \
    SkDEBUGCODE(GrSingleOwner::AutoEnforce debug_SingleOwner(fImageContext->priv().singleOwner());)

GrProxyProvider::GrProxyProvider(GrImageContext* imageContext) : fImageContext(imageContext) {}

GrProxyProvider::~GrProxyProvider() {
    if (this->renderingDirectly()) {
        // In DDL-mode a proxy provider can still have extant uniquely keyed proxies (since
        // they need their unique keys to, potentially, find a cached resource when the
        // DDL is played) but, in non-DDL-mode they should all have been cleaned up by this point.
        SkASSERT(!fUniquelyKeyedProxies.count());
    }
}

bool GrProxyProvider::assignUniqueKeyToProxy(const GrUniqueKey& key, GrTextureProxy* proxy) {
    ASSERT_SINGLE_OWNER
    SkASSERT(key.isValid());
    if (this->isAbandoned() || !proxy) {
        return false;
    }

#ifdef SK_DEBUG
    {
        GrContext* direct = fImageContext->priv().asDirectContext();
        if (direct) {
            GrResourceCache* resourceCache = direct->priv().getResourceCache();
            // If there is already a GrResource with this key then the caller has violated the
            // normal usage pattern of uniquely keyed resources (e.g., they have created one w/o
            // first seeing if it already existed in the cache).
            SkASSERT(!resourceCache->findAndRefUniqueResource(key));
        }
    }
#endif

    SkASSERT(!fUniquelyKeyedProxies.find(key));     // multiple proxies can't get the same key

    proxy->cacheAccess().setUniqueKey(this, key);
    SkASSERT(proxy->getUniqueKey() == key);
    fUniquelyKeyedProxies.add(proxy);
    return true;
}

void GrProxyProvider::adoptUniqueKeyFromSurface(GrTextureProxy* proxy, const GrSurface* surf) {
    SkASSERT(surf->getUniqueKey().isValid());
    proxy->cacheAccess().setUniqueKey(this, surf->getUniqueKey());
    SkASSERT(proxy->getUniqueKey() == surf->getUniqueKey());
    // multiple proxies can't get the same key
    SkASSERT(!fUniquelyKeyedProxies.find(surf->getUniqueKey()));
    fUniquelyKeyedProxies.add(proxy);
}

void GrProxyProvider::removeUniqueKeyFromProxy(GrTextureProxy* proxy) {
    ASSERT_SINGLE_OWNER
    SkASSERT(proxy);
    SkASSERT(proxy->getUniqueKey().isValid());

    if (this->isAbandoned()) {
        return;
    }

    this->processInvalidUniqueKey(proxy->getUniqueKey(), proxy, InvalidateGPUResource::kYes);
}

sk_sp<GrTextureProxy> GrProxyProvider::findProxyByUniqueKey(const GrUniqueKey& key) {
    ASSERT_SINGLE_OWNER

    if (this->isAbandoned()) {
        return nullptr;
    }

    GrTextureProxy* proxy = fUniquelyKeyedProxies.find(key);
    if (proxy) {
        return sk_ref_sp(proxy);
    }
    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////

#if GR_TEST_UTILS
sk_sp<GrTextureProxy> GrProxyProvider::testingOnly_createInstantiatedProxy(
        SkISize dimensions,
        GrColorType colorType,
        const GrBackendFormat& format,
        GrRenderable renderable,
        int renderTargetSampleCnt,
        SkBackingFit fit,
        SkBudgeted budgeted,
        GrProtected isProtected) {
    ASSERT_SINGLE_OWNER
    if (this->isAbandoned()) {
        return nullptr;
    }
    GrContext* direct = fImageContext->priv().asDirectContext();
    if (!direct) {
        return nullptr;
    }

    if (this->caps()->isFormatCompressed(format)) {
        // TODO: Allow this to go to GrResourceProvider::createCompressedTexture() once we no longer
        // rely on GrColorType to get a swizzle for the proxy.
        return nullptr;
    }

    GrResourceProvider* resourceProvider = direct->priv().resourceProvider();
    sk_sp<GrTexture> tex;

    if (SkBackingFit::kApprox == fit) {
        tex = resourceProvider->createApproxTexture(dimensions, format, renderable,
                                                    renderTargetSampleCnt, isProtected);
    } else {
        tex = resourceProvider->createTexture(dimensions, format, renderable, renderTargetSampleCnt,
                                              GrMipMapped::kNo, budgeted, isProtected);
    }
    if (!tex) {
        return nullptr;
    }

    return this->createWrapped(std::move(tex), colorType, UseAllocator::kYes);
}

sk_sp<GrTextureProxy> GrProxyProvider::testingOnly_createInstantiatedProxy(
        SkISize dimensions,
        GrColorType colorType,
        GrRenderable renderable,
        int renderTargetSampleCnt,
        SkBackingFit fit,
        SkBudgeted budgeted,
        GrProtected isProtected) {
    ASSERT_SINGLE_OWNER
    if (this->isAbandoned()) {
        return nullptr;
    }
    auto format = this->caps()->getDefaultBackendFormat(colorType, renderable);
    return this->testingOnly_createInstantiatedProxy(dimensions,
                                                     colorType,
                                                     format,
                                                     renderable,
                                                     renderTargetSampleCnt,
                                                     fit,
                                                     budgeted,
                                                     isProtected);
}

sk_sp<GrTextureProxy> GrProxyProvider::testingOnly_createWrapped(sk_sp<GrTexture> tex,
                                                                 GrColorType colorType) {
    return this->createWrapped(std::move(tex), colorType, UseAllocator::kYes);
}
#endif

sk_sp<GrTextureProxy> GrProxyProvider::createWrapped(sk_sp<GrTexture> tex,
                                                     GrColorType colorType,
                                                     UseAllocator useAllocator) {
#ifdef SK_DEBUG
    if (tex->getUniqueKey().isValid()) {
        SkASSERT(!this->findProxyByUniqueKey(tex->getUniqueKey()));
    }
#endif
    GrSwizzle readSwizzle = this->caps()->getReadSwizzle(tex->backendFormat(), colorType);

    if (tex->asRenderTarget()) {
        return sk_sp<GrTextureProxy>(
                new GrTextureRenderTargetProxy(std::move(tex), readSwizzle, useAllocator));
    } else {
        return sk_sp<GrTextureProxy>(new GrTextureProxy(std::move(tex), readSwizzle, useAllocator));
    }
}

sk_sp<GrTextureProxy> GrProxyProvider::findOrCreateProxyByUniqueKey(const GrUniqueKey& key,
                                                                    GrColorType colorType,
                                                                    UseAllocator useAllocator) {
    ASSERT_SINGLE_OWNER

    if (this->isAbandoned()) {
        return nullptr;
    }

    sk_sp<GrTextureProxy> result = this->findProxyByUniqueKey(key);
    if (result) {
        return result;
    }

    GrContext* direct = fImageContext->priv().asDirectContext();
    if (!direct) {
        return nullptr;
    }

    GrResourceCache* resourceCache = direct->priv().getResourceCache();

    GrGpuResource* resource = resourceCache->findAndRefUniqueResource(key);
    if (!resource) {
        return nullptr;
    }

    sk_sp<GrTexture> texture(static_cast<GrSurface*>(resource)->asTexture());
    SkASSERT(texture);

    result = this->createWrapped(std::move(texture), colorType, useAllocator);
    SkASSERT(result->getUniqueKey() == key);
    // createWrapped should've added this for us
    SkASSERT(fUniquelyKeyedProxies.find(key));
    SkASSERT(result->textureSwizzleDoNotUse() ==
             this->caps()->getReadSwizzle(result->backendFormat(), colorType));
    return result;
}

sk_sp<GrTextureProxy> GrProxyProvider::createProxyFromBitmap(const SkBitmap& bitmap,
                                                             GrMipMapped mipMapped,
                                                             SkBackingFit fit) {
    ASSERT_SINGLE_OWNER
    SkASSERT(fit == SkBackingFit::kExact || mipMapped == GrMipMapped::kNo);

    if (this->isAbandoned()) {
        return nullptr;
    }

    if (!SkImageInfoIsValid(bitmap.info())) {
        return nullptr;
    }

    ATRACE_ANDROID_FRAMEWORK("Upload %sTexture [%ux%u]",
                             GrMipMapped::kYes == mipMapped ? "MipMap " : "",
                             bitmap.width(), bitmap.height());

    // In non-ddl we will always instantiate right away. Thus we never want to copy the SkBitmap
    // even if its mutable. In ddl, if the bitmap is mutable then we must make a copy since the
    // upload of the data to the gpu can happen at anytime and the bitmap may change by then.
    SkBitmap copyBitmap = bitmap;
    if (!this->renderingDirectly() && !bitmap.isImmutable()) {
        copyBitmap.allocPixels();
        if (!bitmap.readPixels(copyBitmap.pixmap())) {
            return nullptr;
        }
        copyBitmap.setImmutable();
    }

    GrColorType grCT = SkColorTypeToGrColorType(copyBitmap.info().colorType());
    GrBackendFormat format = this->caps()->getDefaultBackendFormat(grCT, GrRenderable::kNo);
    if (!format.isValid()) {
        return nullptr;
    }

    sk_sp<GrTextureProxy> proxy;
    if (mipMapped == GrMipMapped::kNo ||
        0 == SkMipMap::ComputeLevelCount(copyBitmap.width(), copyBitmap.height())) {
        proxy = this->createNonMippedProxyFromBitmap(copyBitmap, fit, format, grCT);
    } else {
        proxy = this->createMippedProxyFromBitmap(copyBitmap, format, grCT);
    }

    if (!proxy) {
        return nullptr;
    }

    GrContext* direct = fImageContext->priv().asDirectContext();
    if (direct) {
        GrResourceProvider* resourceProvider = direct->priv().resourceProvider();

        // In order to reuse code we always create a lazy proxy. When we aren't in DDL mode however
        // we're better off instantiating the proxy immediately here.
        if (!proxy->priv().doLazyInstantiation(resourceProvider)) {
            return nullptr;
        }
    }
    return proxy;
}

sk_sp<GrTextureProxy> GrProxyProvider::createNonMippedProxyFromBitmap(const SkBitmap& bitmap,
                                                                      SkBackingFit fit,
                                                                      const GrBackendFormat& format,
                                                                      GrColorType colorType) {
    GrSwizzle swizzle = this->caps()->getReadSwizzle(format, colorType);
    auto dims = bitmap.dimensions();

    sk_sp<GrTextureProxy> proxy = this->createLazyProxy(
            [dims, format, bitmap, fit, colorType](GrResourceProvider* resourceProvider) {
                GrMipLevel mipLevel = { bitmap.getPixels(), bitmap.rowBytes() };

                return LazyCallbackResult(resourceProvider->createTexture(
                        dims, format, colorType, GrRenderable::kNo, 1, SkBudgeted::kYes, fit,
                        GrProtected::kNo, mipLevel));
            },
            format, dims, swizzle, GrRenderable::kNo, 1, GrMipMapped::kNo,
            GrMipMapsStatus::kNotAllocated, GrInternalSurfaceFlags::kNone, fit, SkBudgeted::kYes,
            GrProtected::kNo, UseAllocator::kYes);

    if (!proxy) {
        return nullptr;
    }
    SkASSERT(proxy->dimensions() == bitmap.dimensions());
    return proxy;
}

sk_sp<GrTextureProxy> GrProxyProvider::createMippedProxyFromBitmap(const SkBitmap& bitmap,
                                                                   const GrBackendFormat& format,
                                                                   GrColorType colorType) {
    SkASSERT(this->caps()->mipMapSupport());


    sk_sp<SkMipMap> mipmaps(SkMipMap::Build(bitmap.pixmap(), nullptr));
    if (!mipmaps) {
        return nullptr;
    }

    GrSwizzle readSwizzle = this->caps()->getReadSwizzle(format, colorType);
    auto dims = bitmap.dimensions();

    sk_sp<GrTextureProxy> proxy = this->createLazyProxy(
            [dims, format, bitmap, mipmaps](GrResourceProvider* resourceProvider) {
                const int mipLevelCount = mipmaps->countLevels() + 1;
                std::unique_ptr<GrMipLevel[]> texels(new GrMipLevel[mipLevelCount]);

                texels[0].fPixels = bitmap.getPixels();
                texels[0].fRowBytes = bitmap.rowBytes();

                auto colorType = SkColorTypeToGrColorType(bitmap.colorType());
                for (int i = 1; i < mipLevelCount; ++i) {
                    SkMipMap::Level generatedMipLevel;
                    mipmaps->getLevel(i - 1, &generatedMipLevel);
                    texels[i].fPixels = generatedMipLevel.fPixmap.addr();
                    texels[i].fRowBytes = generatedMipLevel.fPixmap.rowBytes();
                    SkASSERT(texels[i].fPixels);
                    SkASSERT(generatedMipLevel.fPixmap.colorType() == bitmap.colorType());
                }
                return LazyCallbackResult(resourceProvider->createTexture(
                        dims, format, colorType, GrRenderable::kNo, 1, SkBudgeted::kYes,
                        GrProtected::kNo, texels.get(), mipLevelCount));
            },
            format, dims, readSwizzle, GrRenderable::kNo, 1, GrMipMapped::kYes,
            GrMipMapsStatus::kValid, GrInternalSurfaceFlags::kNone, SkBackingFit::kExact,
            SkBudgeted::kYes, GrProtected::kNo, UseAllocator::kYes);

    if (!proxy) {
        return nullptr;
    }

    SkASSERT(proxy->dimensions() == bitmap.dimensions());

    return proxy;
}

sk_sp<GrTextureProxy> GrProxyProvider::createProxy(const GrBackendFormat& format,
                                                   SkISize dimensions,
                                                   GrSwizzle readSwizzle,
                                                   GrRenderable renderable,
                                                   int renderTargetSampleCnt,
                                                   GrMipMapped mipMapped,
                                                   SkBackingFit fit,
                                                   SkBudgeted budgeted,
                                                   GrProtected isProtected,
                                                   GrInternalSurfaceFlags surfaceFlags,
                                                   GrSurfaceProxy::UseAllocator useAllocator) {
    ASSERT_SINGLE_OWNER
    if (this->isAbandoned()) {
        return nullptr;
    }

    const GrCaps* caps = this->caps();

    if (caps->isFormatCompressed(format)) {
        // Deferred proxies for compressed textures are not supported.
        return nullptr;
    }

    if (GrMipMapped::kYes == mipMapped) {
        // SkMipMap doesn't include the base level in the level count so we have to add 1
        int mipCount = SkMipMap::ComputeLevelCount(dimensions.fWidth, dimensions.fHeight) + 1;
        if (1 == mipCount) {
            mipMapped = GrMipMapped::kNo;
        }
    }

    if (!caps->validateSurfaceParams(dimensions, format, renderable, renderTargetSampleCnt,
                                     mipMapped)) {
        return nullptr;
    }
    GrMipMapsStatus mipMapsStatus = (GrMipMapped::kYes == mipMapped)
            ? GrMipMapsStatus::kDirty
            : GrMipMapsStatus::kNotAllocated;
    if (renderable == GrRenderable::kYes) {
        renderTargetSampleCnt =
                caps->getRenderTargetSampleCount(renderTargetSampleCnt, format);
        SkASSERT(renderTargetSampleCnt);
        // We know anything we instantiate later from this deferred path will be
        // both texturable and renderable
        return sk_sp<GrTextureProxy>(new GrTextureRenderTargetProxy(
                *caps, format, dimensions, renderTargetSampleCnt, mipMapped, mipMapsStatus,
                readSwizzle, fit, budgeted, isProtected, surfaceFlags, useAllocator));
    }

    return sk_sp<GrTextureProxy>(new GrTextureProxy(format, dimensions, mipMapped, mipMapsStatus,
                                                    readSwizzle, fit, budgeted, isProtected,
                                                    surfaceFlags, useAllocator));
}

sk_sp<GrTextureProxy> GrProxyProvider::createCompressedTextureProxy(
        SkISize dimensions, SkBudgeted budgeted, GrMipMapped mipMapped, GrProtected isProtected,
        SkImage::CompressionType compressionType, sk_sp<SkData> data) {
    ASSERT_SINGLE_OWNER
    if (this->isAbandoned()) {
        return nullptr;
    }

    GrBackendFormat format = this->caps()->getBackendFormatFromCompressionType(compressionType);

    if (!this->caps()->isFormatTexturable(format)) {
        return nullptr;
    }

    GrMipMapsStatus mipMapsStatus = (GrMipMapped::kYes == mipMapped)
                                                                ? GrMipMapsStatus::kValid
                                                                : GrMipMapsStatus::kNotAllocated;

    sk_sp<GrTextureProxy> proxy = this->createLazyProxy(
            [dimensions, format, budgeted, mipMapped, isProtected,
             data](GrResourceProvider* resourceProvider) {
                return LazyCallbackResult(resourceProvider->createCompressedTexture(
                    dimensions, format, budgeted, mipMapped, isProtected, data.get()));
            },
            format, dimensions, GrSwizzle(), GrRenderable::kNo, 1, mipMapped, mipMapsStatus,
            GrInternalSurfaceFlags::kReadOnly, SkBackingFit::kExact, SkBudgeted::kYes,
            GrProtected::kNo, UseAllocator::kYes);

    if (!proxy) {
        return nullptr;
    }

    GrContext* direct = fImageContext->priv().asDirectContext();
    if (direct) {
        GrResourceProvider* resourceProvider = direct->priv().resourceProvider();
        // In order to reuse code we always create a lazy proxy. When we aren't in DDL mode however
        // we're better off instantiating the proxy immediately here.
        if (!proxy->priv().doLazyInstantiation(resourceProvider)) {
            return nullptr;
        }
    }
    return proxy;
}

sk_sp<GrTextureProxy> GrProxyProvider::wrapBackendTexture(const GrBackendTexture& backendTex,
                                                          GrColorType grColorType,
                                                          GrWrapOwnership ownership,
                                                          GrWrapCacheable cacheable,
                                                          GrIOType ioType,
                                                          ReleaseProc releaseProc,
                                                          ReleaseContext releaseCtx) {
    SkASSERT(ioType != kWrite_GrIOType);
    if (this->isAbandoned()) {
        return nullptr;
    }

    // This is only supported on a direct GrContext.
    GrContext* direct = fImageContext->priv().asDirectContext();
    if (!direct) {
        return nullptr;
    }

    const GrCaps* caps = this->caps();

    GrResourceProvider* resourceProvider = direct->priv().resourceProvider();

    sk_sp<GrTexture> tex =
            resourceProvider->wrapBackendTexture(backendTex, grColorType,
                                                 ownership, cacheable, ioType);
    if (!tex) {
        return nullptr;
    }

    if (releaseProc) {
        tex->setRelease(releaseProc, releaseCtx);
    }

    SkASSERT(!tex->asRenderTarget());  // Strictly a GrTexture
    // Make sure we match how we created the proxy with SkBudgeted::kNo
    SkASSERT(GrBudgetedType::kBudgeted != tex->resourcePriv().budgetedType());

    GrSwizzle readSwizzle = caps->getReadSwizzle(tex->backendFormat(), grColorType);

    return sk_sp<GrTextureProxy>(
            new GrTextureProxy(std::move(tex), readSwizzle, UseAllocator::kNo));
}

sk_sp<GrTextureProxy> GrProxyProvider::wrapCompressedBackendTexture(const GrBackendTexture& beTex,
                                                                    GrWrapOwnership ownership,
                                                                    GrWrapCacheable cacheable,
                                                                    ReleaseProc releaseProc,
                                                                    ReleaseContext releaseCtx) {
    if (this->isAbandoned()) {
        return nullptr;
    }

    // This is only supported on a direct GrContext.
    GrContext* direct = fImageContext->priv().asDirectContext();
    if (!direct) {
        return nullptr;
    }

    const GrCaps* caps = this->caps();

    GrResourceProvider* resourceProvider = direct->priv().resourceProvider();

    sk_sp<GrTexture> tex = resourceProvider->wrapCompressedBackendTexture(beTex, ownership,
                                                                          cacheable);
    if (!tex) {
        return nullptr;
    }

    if (releaseProc) {
        tex->setRelease(releaseProc, releaseCtx);
    }

    SkASSERT(!tex->asRenderTarget());  // Strictly a GrTexture
    // Make sure we match how we created the proxy with SkBudgeted::kNo
    SkASSERT(GrBudgetedType::kBudgeted != tex->resourcePriv().budgetedType());

    SkImage::CompressionType compressionType = caps->compressionType(beTex.getBackendFormat());

    GrSwizzle texSwizzle = SkCompressionTypeIsOpaque(compressionType) ? GrSwizzle::RGB1()
                                                                      : GrSwizzle::RGBA();

    return sk_sp<GrTextureProxy>(new GrTextureProxy(std::move(tex), texSwizzle, UseAllocator::kNo));
}

sk_sp<GrTextureProxy> GrProxyProvider::wrapRenderableBackendTexture(
        const GrBackendTexture& backendTex, int sampleCnt, GrColorType colorType,
        GrWrapOwnership ownership, GrWrapCacheable cacheable, ReleaseProc releaseProc,
        ReleaseContext releaseCtx) {
    if (this->isAbandoned()) {
        return nullptr;
    }

    // This is only supported on a direct GrContext.
    GrContext* direct = fImageContext->priv().asDirectContext();
    if (!direct) {
        return nullptr;
    }

    const GrCaps* caps = this->caps();

    GrResourceProvider* resourceProvider = direct->priv().resourceProvider();

    // TODO: This should have been checked and validated before getting into GrProxyProvider.
    if (!caps->isFormatAsColorTypeRenderable(colorType, backendTex.getBackendFormat(), sampleCnt)) {
        return nullptr;
    }

    sampleCnt = caps->getRenderTargetSampleCount(sampleCnt, backendTex.getBackendFormat());
    SkASSERT(sampleCnt);

    sk_sp<GrTexture> tex = resourceProvider->wrapRenderableBackendTexture(backendTex, sampleCnt,
                                                                          colorType, ownership,
                                                                          cacheable);
    if (!tex) {
        return nullptr;
    }

    if (releaseProc) {
        tex->setRelease(releaseProc, releaseCtx);
    }

    SkASSERT(tex->asRenderTarget());  // A GrTextureRenderTarget
    // Make sure we match how we created the proxy with SkBudgeted::kNo
    SkASSERT(GrBudgetedType::kBudgeted != tex->resourcePriv().budgetedType());

    GrSwizzle readSwizzle = caps->getReadSwizzle(tex->backendFormat(), colorType);

    return sk_sp<GrTextureProxy>(
            new GrTextureRenderTargetProxy(std::move(tex), readSwizzle, UseAllocator::kNo));
}

sk_sp<GrSurfaceProxy> GrProxyProvider::wrapBackendRenderTarget(
        const GrBackendRenderTarget& backendRT, GrColorType grColorType, ReleaseProc releaseProc,
        ReleaseContext releaseCtx) {
    if (this->isAbandoned()) {
        return nullptr;
    }

    // This is only supported on a direct GrContext.
    GrContext* direct = fImageContext->priv().asDirectContext();
    if (!direct) {
        return nullptr;
    }

    const GrCaps* caps = this->caps();

    GrResourceProvider* resourceProvider = direct->priv().resourceProvider();

    sk_sp<GrRenderTarget> rt = resourceProvider->wrapBackendRenderTarget(backendRT, grColorType);
    if (!rt) {
        return nullptr;
    }

    if (releaseProc) {
        rt->setRelease(releaseProc, releaseCtx);
    }

    SkASSERT(!rt->asTexture());  // A GrRenderTarget that's not textureable
    SkASSERT(!rt->getUniqueKey().isValid());
    // Make sure we match how we created the proxy with SkBudgeted::kNo
    SkASSERT(GrBudgetedType::kBudgeted != rt->resourcePriv().budgetedType());

    GrSwizzle readSwizzle = caps->getReadSwizzle(rt->backendFormat(), grColorType);

    return sk_sp<GrRenderTargetProxy>(
            new GrRenderTargetProxy(std::move(rt), readSwizzle, UseAllocator::kNo));
}

sk_sp<GrSurfaceProxy> GrProxyProvider::wrapBackendTextureAsRenderTarget(
        const GrBackendTexture& backendTex, GrColorType grColorType, int sampleCnt) {
    if (this->isAbandoned()) {
        return nullptr;
    }

    // This is only supported on a direct GrContext.
    GrContext* direct = fImageContext->priv().asDirectContext();
    if (!direct) {
        return nullptr;
    }

    const GrCaps* caps = this->caps();

    GrResourceProvider* resourceProvider = direct->priv().resourceProvider();

    sk_sp<GrRenderTarget> rt =
            resourceProvider->wrapBackendTextureAsRenderTarget(backendTex, sampleCnt, grColorType);
    if (!rt) {
        return nullptr;
    }
    SkASSERT(!rt->asTexture());  // A GrRenderTarget that's not textureable
    SkASSERT(!rt->getUniqueKey().isValid());
    // This proxy should be unbudgeted because we're just wrapping an external resource
    SkASSERT(GrBudgetedType::kBudgeted != rt->resourcePriv().budgetedType());

    GrSwizzle readSwizzle = caps->getReadSwizzle(rt->backendFormat(), grColorType);

    return sk_sp<GrSurfaceProxy>(
            new GrRenderTargetProxy(std::move(rt), readSwizzle, UseAllocator::kNo));
}

sk_sp<GrRenderTargetProxy> GrProxyProvider::wrapVulkanSecondaryCBAsRenderTarget(
        const SkImageInfo& imageInfo, const GrVkDrawableInfo& vkInfo) {
    if (this->isAbandoned()) {
        return nullptr;
    }

    // This is only supported on a direct GrContext.
    GrContext* direct = fImageContext->priv().asDirectContext();
    if (!direct) {
        return nullptr;
    }

    GrResourceProvider* resourceProvider = direct->priv().resourceProvider();

    sk_sp<GrRenderTarget> rt = resourceProvider->wrapVulkanSecondaryCBAsRenderTarget(imageInfo,
                                                                                     vkInfo);
    if (!rt) {
        return nullptr;
    }

    SkASSERT(!rt->asTexture());  // A GrRenderTarget that's not textureable
    SkASSERT(!rt->getUniqueKey().isValid());
    // This proxy should be unbudgeted because we're just wrapping an external resource
    SkASSERT(GrBudgetedType::kBudgeted != rt->resourcePriv().budgetedType());

    GrColorType colorType = SkColorTypeToGrColorType(imageInfo.colorType());
    GrSwizzle readSwizzle = this->caps()->getReadSwizzle(rt->backendFormat(), colorType);

    if (!this->caps()->isFormatAsColorTypeRenderable(colorType, rt->backendFormat(),
                                                     rt->numSamples())) {
        return nullptr;
    }

    return sk_sp<GrRenderTargetProxy>(
            new GrRenderTargetProxy(std::move(rt), readSwizzle, UseAllocator::kNo,
                                    GrRenderTargetProxy::WrapsVkSecondaryCB::kYes));
}

sk_sp<GrTextureProxy> GrProxyProvider::createLazyProxy(LazyInstantiateCallback&& callback,
                                                       const GrBackendFormat& format,
                                                       SkISize dimensions,
                                                       GrSwizzle readSwizzle,
                                                       GrRenderable renderable,
                                                       int renderTargetSampleCnt,
                                                       GrMipMapped mipMapped,
                                                       GrMipMapsStatus mipMapsStatus,
                                                       GrInternalSurfaceFlags surfaceFlags,
                                                       SkBackingFit fit,
                                                       SkBudgeted budgeted,
                                                       GrProtected isProtected,
                                                       GrSurfaceProxy::UseAllocator useAllocator) {
    ASSERT_SINGLE_OWNER
    if (this->isAbandoned()) {
        return nullptr;
    }
    SkASSERT((dimensions.fWidth <= 0 && dimensions.fHeight <= 0) ||
             (dimensions.fWidth >  0 && dimensions.fHeight >  0));

    if (!format.isValid()) {
        return nullptr;
    }

    if (dimensions.fWidth > this->caps()->maxTextureSize() ||
        dimensions.fHeight > this->caps()->maxTextureSize()) {
        return nullptr;
    }

    if (renderable == GrRenderable::kYes) {
        return sk_sp<GrTextureProxy>(new GrTextureRenderTargetProxy(*this->caps(),
                                                                    std::move(callback),
                                                                    format,
                                                                    dimensions,
                                                                    renderTargetSampleCnt,
                                                                    mipMapped,
                                                                    mipMapsStatus,
                                                                    readSwizzle,
                                                                    fit,
                                                                    budgeted,
                                                                    isProtected,
                                                                    surfaceFlags,
                                                                    useAllocator));
    } else {
        return sk_sp<GrTextureProxy>(new GrTextureProxy(std::move(callback),
                                                        format,
                                                        dimensions,
                                                        mipMapped,
                                                        mipMapsStatus,
                                                        readSwizzle,
                                                        fit,
                                                        budgeted,
                                                        isProtected,
                                                        surfaceFlags,
                                                        useAllocator));
    }
}

sk_sp<GrRenderTargetProxy> GrProxyProvider::createLazyRenderTargetProxy(
        LazyInstantiateCallback&& callback,
        const GrBackendFormat& format,
        SkISize dimensions,
        GrSwizzle readSwizzle,
        int sampleCnt,
        GrInternalSurfaceFlags surfaceFlags,
        const TextureInfo* textureInfo,
        GrMipMapsStatus mipMapsStatus,
        SkBackingFit fit,
        SkBudgeted budgeted,
        GrProtected isProtected,
        bool wrapsVkSecondaryCB,
        UseAllocator useAllocator) {
    ASSERT_SINGLE_OWNER
    if (this->isAbandoned()) {
        return nullptr;
    }
    SkASSERT((dimensions.fWidth <= 0 && dimensions.fHeight <= 0) ||
             (dimensions.fWidth >  0 && dimensions.fHeight >  0));

    if (dimensions.fWidth > this->caps()->maxRenderTargetSize() ||
        dimensions.fHeight > this->caps()->maxRenderTargetSize()) {
        return nullptr;
    }

    if (textureInfo) {
        // Wrapped vulkan secondary command buffers don't support texturing since we won't have an
        // actual VkImage to texture from.
        SkASSERT(!wrapsVkSecondaryCB);
        return sk_sp<GrRenderTargetProxy>(new GrTextureRenderTargetProxy(
                *this->caps(), std::move(callback), format, dimensions, sampleCnt,
                textureInfo->fMipMapped, mipMapsStatus, readSwizzle, fit, budgeted, isProtected,
                surfaceFlags, useAllocator));
    }

    GrRenderTargetProxy::WrapsVkSecondaryCB vkSCB =
            wrapsVkSecondaryCB ? GrRenderTargetProxy::WrapsVkSecondaryCB::kYes
                               : GrRenderTargetProxy::WrapsVkSecondaryCB::kNo;

    return sk_sp<GrRenderTargetProxy>(
            new GrRenderTargetProxy(std::move(callback), format, dimensions, sampleCnt, readSwizzle,
                                    fit, budgeted, isProtected, surfaceFlags, useAllocator, vkSCB));
}

sk_sp<GrTextureProxy> GrProxyProvider::MakeFullyLazyProxy(LazyInstantiateCallback&& callback,
                                                          const GrBackendFormat& format,
                                                          GrSwizzle readSwizzle,
                                                          GrRenderable renderable,
                                                          int renderTargetSampleCnt,
                                                          GrProtected isProtected,
                                                          const GrCaps& caps,
                                                          UseAllocator useAllocator) {
    if (!format.isValid()) {
        return nullptr;
    }

    SkASSERT(renderTargetSampleCnt == 1 || renderable == GrRenderable::kYes);
    GrInternalSurfaceFlags surfaceFlags = GrInternalSurfaceFlags::kNone;

    static constexpr SkISize kLazyDims = {-1, -1};
    if (GrRenderable::kYes == renderable) {
        return sk_sp<GrTextureProxy>(new GrTextureRenderTargetProxy(
                caps, std::move(callback), format, kLazyDims, renderTargetSampleCnt,
                GrMipMapped::kNo, GrMipMapsStatus::kNotAllocated, readSwizzle,
                SkBackingFit::kApprox, SkBudgeted::kYes, isProtected, surfaceFlags, useAllocator));
    } else {
        return sk_sp<GrTextureProxy>(new GrTextureProxy(
                std::move(callback), format, kLazyDims, GrMipMapped::kNo,
                GrMipMapsStatus::kNotAllocated, readSwizzle, SkBackingFit::kApprox,
                SkBudgeted::kYes, isProtected, surfaceFlags, useAllocator));
    }
}

void GrProxyProvider::processInvalidUniqueKey(const GrUniqueKey& key, GrTextureProxy* proxy,
                                              InvalidateGPUResource invalidateGPUResource) {
    SkASSERT(key.isValid());

    if (!proxy) {
        proxy = fUniquelyKeyedProxies.find(key);
    }
    SkASSERT(!proxy || proxy->getUniqueKey() == key);

    // Locate the corresponding GrGpuResource (if it needs to be invalidated) before clearing the
    // proxy's unique key. We must do it in this order because 'key' may alias the proxy's key.
    sk_sp<GrGpuResource> invalidGpuResource;
    if (InvalidateGPUResource::kYes == invalidateGPUResource) {
        GrContext* direct = fImageContext->priv().asDirectContext();
        if (direct) {
            GrResourceProvider* resourceProvider = direct->priv().resourceProvider();
            invalidGpuResource = resourceProvider->findByUniqueKey<GrGpuResource>(key);
        }
        SkASSERT(!invalidGpuResource || invalidGpuResource->getUniqueKey() == key);
    }

    // Note: this method is called for the whole variety of GrGpuResources so often 'key'
    // will not be in 'fUniquelyKeyedProxies'.
    if (proxy) {
        fUniquelyKeyedProxies.remove(key);
        proxy->cacheAccess().clearUniqueKey();
    }

    if (invalidGpuResource) {
        invalidGpuResource->resourcePriv().removeUniqueKey();
    }
}

uint32_t GrProxyProvider::contextID() const {
    return fImageContext->priv().contextID();
}

const GrCaps* GrProxyProvider::caps() const {
    return fImageContext->priv().caps();
}

sk_sp<const GrCaps> GrProxyProvider::refCaps() const {
    return fImageContext->priv().refCaps();
}

bool GrProxyProvider::isAbandoned() const {
    return fImageContext->priv().abandoned();
}

void GrProxyProvider::orphanAllUniqueKeys() {
    UniquelyKeyedProxyHash::Iter iter(&fUniquelyKeyedProxies);
    for (UniquelyKeyedProxyHash::Iter iter(&fUniquelyKeyedProxies); !iter.done(); ++iter) {
        GrTextureProxy& tmp = *iter;

        tmp.fProxyProvider = nullptr;
    }
}

void GrProxyProvider::removeAllUniqueKeys() {
    UniquelyKeyedProxyHash::Iter iter(&fUniquelyKeyedProxies);
    for (UniquelyKeyedProxyHash::Iter iter(&fUniquelyKeyedProxies); !iter.done(); ++iter) {
        GrTextureProxy& tmp = *iter;

        this->processInvalidUniqueKey(tmp.getUniqueKey(), &tmp, InvalidateGPUResource::kNo);
    }
    SkASSERT(!fUniquelyKeyedProxies.count());
}

bool GrProxyProvider::renderingDirectly() const {
    return fImageContext->priv().asDirectContext();
}
