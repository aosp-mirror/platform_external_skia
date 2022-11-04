
/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrTexture_DEFINED
#define GrTexture_DEFINED

#include "include/core/SkImage.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRefCnt.h"
#include "include/gpu/GrBackendSurface.h"
#include "include/private/gpu/ganesh/GrTypesPriv.h"
#include "src/gpu/ganesh/GrSurface.h"

#if defined(SK_DEBUG)
class GrTextureEffect;
#endif

class GrTexture : virtual public GrSurface {
public:
    GrTexture* asTexture() override { return this; }
    const GrTexture* asTexture() const override { return this; }

    virtual GrBackendTexture getBackendTexture() const = 0;

    /**
     * This function indicates that the texture parameters (wrap mode, filtering, ...) have been
     * changed externally to Skia.
     */
    virtual void textureParamsModified() = 0;

    /**
     * This function steals the backend texture from a uniquely owned GrTexture with no pending
     * IO, passing it out to the caller. The GrTexture is deleted in the process.
     *
     * Note that if the GrTexture is not uniquely owned (no other refs), or has pending IO, this
     * function will fail.
     */
    static bool StealBackendTexture(sk_sp<GrTexture>,
                                    GrBackendTexture*,
                                    SkImage::BackendTextureReleaseProc*);

    GrTextureType textureType() const { return fTextureType; }
    bool hasRestrictedSampling() const {
        return GrTextureTypeHasRestrictedSampling(this->textureType());
    }

    void markMipmapsDirty(const char* reason);

    void markMipmapsClean();
    GrMipmapped mipmapped() const {
        return GrMipmapped(fMipmapStatus != GrMipmapStatus::kNotAllocated);
    }
    bool mipmapsAreDirty() const { return fMipmapStatus != GrMipmapStatus::kValid; }
    GrMipmapStatus mipmapStatus() const { return fMipmapStatus; }
    int maxMipmapLevel() const { return fMaxMipmapLevel; }

    SkDEBUGCODE(void assertMipmapsNotDirty(const GrTextureEffect& effect);)
    SkDEBUGCODE(void setMipmapRegenFailureReason(const char* s) { fMipmapRegenFailureReason = s;})

    static void ComputeScratchKey(const GrCaps& caps,
                                  const GrBackendFormat& format,
                                  SkISize dimensions,
                                  GrRenderable,
                                  int sampleCnt,
                                  GrMipmapped,
                                  GrProtected,
                                  skgpu::ScratchKey* key);

protected:
    GrTexture(GrGpu*,
              const SkISize&,
              GrProtected,
              GrTextureType,
              GrMipmapStatus,
              std::string_view label);

    virtual bool onStealBackendTexture(GrBackendTexture*, SkImage::BackendTextureReleaseProc*) = 0;

    void computeScratchKey(skgpu::ScratchKey*) const override;

private:
    size_t onGpuMemorySize() const override;

    GrTextureType                 fTextureType;
    GrMipmapStatus                fMipmapStatus;
#if defined(SK_DEBUG)
    const char*                   fMipmapDirtyReason        = "creation";
    const char*                   fMipmapRegenFailureReason = "did not fail";
    int                           fMipmapDirtyFlushNum      = 1;
    bool                          fMipmapDirtyWasFlushing   = false;
#endif
    int                           fMaxMipmapLevel;
    friend class GrTextureResource;

    using INHERITED = GrSurface;
};

#endif
