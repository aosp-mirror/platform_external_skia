/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.
 */

#ifndef SkStrike_DEFINED
#define SkStrike_DEFINED

#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontTypes.h"
#include "include/private/base/SkMutex.h"
#include "include/private/base/SkTemplates.h"
#include "src/base/SkArenaAlloc.h"
#include "src/core/SkDescriptor.h"
#include "src/core/SkGlyph.h"
#include "src/core/SkGlyphRunPainter.h"
#include "src/core/SkStrikeSpec.h"
#include "src/core/SkTHash.h"

#include <memory>

class SkScalerContext;
class SkStrikeCache;
class SkTraceMemoryDump;

namespace sktext {
union IDOrPath;
union IDOrDrawable;
}  // namespace sktext

class SkStrikePinner {
public:
    virtual ~SkStrikePinner() = default;
    virtual bool canDelete() = 0;
    virtual void assertValid() {}
};

// This class holds the results of an SkScalerContext, and owns a references to that scaler.
class SkStrike final : public SkRefCnt, public sktext::StrikeForGPU {
public:
    SkStrike(SkStrikeCache* strikeCache,
             const SkStrikeSpec& strikeSpec,
             std::unique_ptr<SkScalerContext> scaler,
             const SkFontMetrics* metrics,
             std::unique_ptr<SkStrikePinner> pinner);

    // Lookup (or create if needed) the returned glyph using toID. If that glyph is not initialized
    // with an image, then use the information in fromGlyph to initialize the width, height top,
    // left, format and image of the glyph. This is mainly used preserving the glyph if it was
    // created by a search of desperation.
    SkGlyph* mergeGlyphAndImage(SkPackedGlyphID toID, const SkGlyph& fromGlyph) SK_EXCLUDES(fMu);

    // If the path has never been set, then add a path to glyph.
    const SkPath* mergePath(SkGlyph* glyph, const SkPath* path, bool hairline) SK_EXCLUDES(fMu);

    // If the drawable has never been set, then add a drawable to glyph.
    const SkDrawable* mergeDrawable(SkGlyph* glyph, sk_sp<SkDrawable> drawable) SK_EXCLUDES(fMu);

    // If the advance axis intersects the glyph's path, append the positions scaled and offset
    // to the array (if non-null), and set the count to the updated array length.
    // TODO: track memory usage.
    void findIntercepts(const SkScalar bounds[2], SkScalar scale, SkScalar xPos,
                        SkGlyph* , SkScalar* array, int* count) SK_EXCLUDES(fMu);

    const SkFontMetrics& getFontMetrics() const {
        return fFontMetrics;
    }

    SkSpan<const SkGlyph*> metrics(
            SkSpan<const SkGlyphID> glyphIDs, const SkGlyph* results[]) SK_EXCLUDES(fMu);

    SkSpan<const SkGlyph*> preparePaths(
            SkSpan<const SkGlyphID> glyphIDs, const SkGlyph* results[]) SK_EXCLUDES(fMu);

    SkSpan<const SkGlyph*> prepareImages(
            SkSpan<const SkPackedGlyphID> glyphIDs, const SkGlyph* results[]) SK_EXCLUDES(fMu);

    SkSpan<const SkGlyph*> prepareDrawables(
            SkSpan<const SkGlyphID> glyphIDs, const SkGlyph* results[]) SK_EXCLUDES(fMu);

    void prepareForDrawingMasksCPU(SkDrawableGlyphBuffer* accepted) SK_EXCLUDES(fMu);

    // SkStrikeForGPU APIs
    const SkDescriptor& getDescriptor() const override {
        return fStrikeSpec.descriptor();
    }

    SkRect prepareForMaskDrawing(SkDrawableGlyphBuffer* accepted,
                                 SkSourceGlyphBuffer* rejected) override SK_EXCLUDES(fMu);

#if !defined(SK_DISABLE_SDF_TEXT)
    SkRect prepareForSDFTDrawing(SkDrawableGlyphBuffer* accepted,
                                 SkSourceGlyphBuffer* rejected) override SK_EXCLUDES(fMu);
#endif

    void prepareForPathDrawing(SkDrawableGlyphBuffer* accepted,
                               SkSourceGlyphBuffer* rejected) override SK_EXCLUDES(fMu);

    void prepareForDrawableDrawing(SkDrawableGlyphBuffer* accepted,
                                   SkSourceGlyphBuffer* rejected) override SK_EXCLUDES(fMu);

    const SkGlyphPositionRoundingSpec& roundingSpec() const override {
        return fRoundingSpec;
    }

    void onAboutToExitScope() override {
        this->unref();
    }

    sktext::SkStrikePromise strikePromise() override {
        return sktext::SkStrikePromise(sk_ref_sp<SkStrike>(this));
    }

    SkScalar findMaximumGlyphDimension(SkSpan<const SkGlyphID> glyphs) override SK_EXCLUDES(fMu);

    // Convert all the IDs into SkPaths in the span.
    void glyphIDsToPaths(SkSpan<sktext::IDOrPath> idsOrPaths) SK_EXCLUDES(fMu);

    // Convert all the IDs into SkDrawables in the span.
    void glyphIDsToDrawables(SkSpan<sktext::IDOrDrawable> idsOrDrawables) SK_EXCLUDES(fMu);

    SkScalerContext* getScalerContext() const {
        return fScalerContext.get();
    }

    const SkStrikeSpec& strikeSpec() const {
        return fStrikeSpec;
    }

    void verifyPinnedStrike() const {
        if (fPinner != nullptr) {
            fPinner->assertValid();
        }
    }

    void dump() const SK_EXCLUDES(fMu);
    void dumpMemoryStatistics(SkTraceMemoryDump* dump) const SK_EXCLUDES(fMu);

#if SK_SUPPORT_GPU
    sk_sp<sktext::gpu::TextStrike> findOrCreateTextStrike(
            sktext::gpu::StrikeCache* gpuStrikeCache) const;
#endif

private:
    friend class SkStrikeCache;
    template <typename Fn>
    size_t commonFilterLoop(SkDrawableGlyphBuffer* accepted, Fn&& fn) SK_REQUIRES(fMu);

    // Return a glyph. Create it if it doesn't exist, and initialize the glyph with metrics and
    // advances using a scaler.
    std::tuple<SkGlyph*, size_t> glyph(SkPackedGlyphID) SK_REQUIRES(fMu);

    std::tuple<SkGlyphDigest, size_t> digest(SkPackedGlyphID) SK_REQUIRES(fMu);

    // Generate the glyph digest information and update structures to add the glyph.
    SkGlyphDigest addGlyph(SkGlyph* glyph) SK_REQUIRES(fMu);

    std::tuple<const void*, size_t> prepareImage(SkGlyph* glyph) SK_REQUIRES(fMu);

    // If the path has never been set, then use the scaler context to add the glyph.
    size_t preparePath(SkGlyph*) SK_REQUIRES(fMu);

    // If the drawable has never been set, then use the scaler context to add the glyph.
    size_t prepareDrawable(SkGlyph*) SK_REQUIRES(fMu);

    // Maintain memory use statistics.
    void updateDelta(size_t increase) SK_EXCLUDES(fMu);

    enum PathDetail {
        kMetricsOnly,
        kMetricsAndPath
    };

    // internalPrepare will only be called with a mutex already held.
    std::tuple<SkSpan<const SkGlyph*>, size_t> internalPrepare(
            SkSpan<const SkGlyphID> glyphIDs,
            PathDetail pathDetail,
            const SkGlyph** results) SK_REQUIRES(fMu);

    // The following are const and need no mutex protection.
    const std::unique_ptr<SkScalerContext> fScalerContext;
    const SkFontMetrics                    fFontMetrics;
    const SkGlyphPositionRoundingSpec      fRoundingSpec;
    const SkStrikeSpec                     fStrikeSpec;
    SkStrikeCache* const                   fStrikeCache;

    // This mutex provides protection for this specific SkStrike.
    mutable SkMutex fMu;
    // Map from a combined GlyphID and sub-pixel position to a SkGlyphDigest. The actual glyph is
    // stored in the fAlloc. The pointer to the glyph is stored fGlyphForIndex. The
    // SkGlyphDigest's fIndex field stores the index. This pointer provides an unchanging
    // reference to the SkGlyph as long as the strike is alive, and fGlyphForIndex
    // provides a dense index for glyphs.
    SkTHashMap<SkPackedGlyphID, SkGlyphDigest, SkPackedGlyphID::Hash>
            fDigestForPackedGlyphID SK_GUARDED_BY(fMu);
    std::vector<SkGlyph*> fGlyphForIndex SK_GUARDED_BY(fMu);

    // so we don't grow our arrays a lot
    inline static constexpr size_t kMinGlyphCount = 8;
    inline static constexpr size_t kMinGlyphImageSize = 16 /* height */ * 8 /* width */;
    inline static constexpr size_t kMinAllocAmount = kMinGlyphImageSize * kMinGlyphCount;

    SkArenaAlloc            fAlloc SK_GUARDED_BY(fMu) {kMinAllocAmount};

    // The following are protected by the SkStrikeCache's mutex.
    SkStrike*                       fNext{nullptr};
    SkStrike*                       fPrev{nullptr};
    std::unique_ptr<SkStrikePinner> fPinner;
    size_t                          fMemoryUsed{sizeof(SkStrike)};
    bool                            fRemoved{false};
};

#endif  // SkStrike_DEFINED
