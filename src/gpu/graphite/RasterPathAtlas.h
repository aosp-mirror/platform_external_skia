/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef skgpu_graphite_RasterPathAtlas_DEFINED
#define skgpu_graphite_RasterPathAtlas_DEFINED

#include "src/base/SkTInternalLList.h"
#include "src/core/SkTHash.h"
#include "src/gpu/AtlasTypes.h"
#include "src/gpu/ResourceKey.h"
#include "src/gpu/graphite/PathAtlas.h"

namespace skgpu::graphite {

/**
 * PathAtlas class that rasterizes coverage masks on the CPU.
 *
 * When a new shape gets added, its path is rasterized in preparation for upload. These
 * uploads are recorded by `recordUploads()` and subsequently added to an UploadTask.
 *
 * Shapes are cached for future frames to avoid the cost of raster pipeline rendering. Multiple
 * textures (or Pages) are used to cache masks, so if the atlas is full we can reset a Page and
 * start adding new shapes for a future atlas render.
 */
class RasterPathAtlas : public PathAtlas {
public:
    explicit RasterPathAtlas(Recorder* recorder);
    ~RasterPathAtlas() override {}
    void recordUploads(DrawContext*);

protected:
    const TextureProxy* onAddShape(const Shape&,
                                   const Transform& transform,
                                   const SkStrokeRec&,
                                   skvx::half2 maskSize,
                                   skvx::half2* outPos) override;
private:
    // TODO: select atlas size dynamically? Take ContextOptions::fMaxTextureAtlasSize into account?
    static constexpr int kDefaultAtlasDim = 4096;

    struct Page {
        Page(int width, int height, uint16_t identifier)
                : fRectanizer(width, height)
                , fIdentifier(identifier) {}
        bool initializeTextureIfNeeded(Recorder* recorder, uint16_t identifier);

        // A Page lazily requests a texture from the AtlasProvider when the first shape gets added
        // to it and references the same texture for the duration of its lifetime. A reference to
        // this texture is stored here, which is used by CoverageMaskRenderStep when encoding the
        // render pass.
        sk_sp<TextureProxy> fTexture;
        // Tracks placement of paths in a Page
        skgpu::RectanizerSkyline fRectanizer;
        // Rendered data that gets uploaded
        SkAutoPixmapStorage fPixels;
        // Area that's needed to be uploaded
        SkIRect fDirtyRect;
        // Tracks whether a path is already in this Page, and its location in the atlas
        struct UniqueKeyHash {
            uint32_t operator()(const skgpu::UniqueKey& key) const { return key.hash(); }
        };
        skia_private::THashMap<skgpu::UniqueKey, skvx::half2, UniqueKeyHash> fCachedShapes;
        // Tracks current state relative to last flush
        AtlasToken fLastUse = AtlasToken::InvalidToken();

        uint16_t fIdentifier;

        SK_DECLARE_INTERNAL_LLIST_INTERFACE(Page);
    };

    // Free up atlas allocations, if necessary. After this call the atlas can be considered
    // available for new shape insertions. However this method does not have any bearing on the
    // contents of any atlas textures themselves, which may be in use by GPU commands that are
    // in-flight or yet to be submitted.
    void reset(Page*);

    // Investigation shows that eight pages helps with some of the more complex skps, and
    // since we're using less complex vertex setups with the RPA, we have more GPU memory
    // to take advantage of.
    static constexpr int kMaxCachedPages = 6;
    static constexpr int kMaxUncachedPages = 2;
    typedef SkTInternalLList<Page> PageList;
    // LRU lists of Pages (MRU at head - LRU at tail)
    // We have two lists, one for cached paths and one for uncached
    PageList fCachedPageList;
    PageList fUncachedPageList;
    // Allocated array of pages (backing data for lists)
    std::unique_ptr<std::unique_ptr<Page>[]> fPageArray;

    Page* addRect(PageList* pageList,
                  skvx::half2 maskSize,
                  SkIPoint16* outPos);
    void makeMRU(Page*, PageList*);
    void uploadPages(DrawContext* dc, PageList* pageList);
};

}  // namespace skgpu::graphite

#endif  // skgpu_graphite_RasterPathAtlas_DEFINED
