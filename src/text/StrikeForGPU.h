/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef sktext_StrikeForGPU_DEFINED
#define sktext_StrikeForGPU_DEFINED

#include "include/core/SkImageInfo.h"
#include "include/core/SkPoint.h"
#include "include/core/SkSpan.h"
#include "include/core/SkTypes.h"
#include "src/core/SkGlyph.h"

#include <memory>
#include <optional>
#include <variant>

class SkDescriptor;
class SkDrawableGlyphBuffer;
class SkReadBuffer;
class SkSourceGlyphBuffer;
class SkStrike;
class SkStrikeCache;
class SkStrikeClient;
class SkStrikeSpec;
class SkWriteBuffer;
struct SkGlyphPositionRoundingSpec;
struct SkScalerContextEffects;

namespace sktext {
// -- SkStrikePromise ------------------------------------------------------------------------------
// SkStrikePromise produces an SkStrike when needed by GPU glyph rendering. In ordinary
// operation, it just wraps an SkStrike. When used for remote glyph cache operation, the promise is
// serialized to an SkDescriptor. When SkStrikePromise is deserialized, it uses the descriptor to
// look up the SkStrike.
//
// When deserializing some care must be taken; if the needed SkStrike is removed from the cache,
// then looking up using the descriptor will fail resulting in a deserialization failure. The
// Renderer/GPU system solves this problem by pinning all the strikes needed into the cache.
class SkStrikePromise {
public:
    SkStrikePromise() = delete;
    SkStrikePromise(const SkStrikePromise&) = delete;
    SkStrikePromise& operator=(const SkStrikePromise&) = delete;
    SkStrikePromise(SkStrikePromise&&);
    SkStrikePromise& operator=(SkStrikePromise&&);

    explicit SkStrikePromise(sk_sp<SkStrike>&& strike);
    explicit SkStrikePromise(const SkStrikeSpec& spec);

    static std::optional<SkStrikePromise> MakeFromBuffer(SkReadBuffer& buffer,
                                                         const SkStrikeClient* client,
                                                         SkStrikeCache* strikeCache);
    void flatten(SkWriteBuffer& buffer) const;

    // Do what is needed to return a strike.
    SkStrike* strike();

    // Reset the sk_sp<SkStrike> to nullptr.
    void resetStrike();

    // Return a descriptor used to look up the SkStrike.
    const SkDescriptor& descriptor() const;

private:
    std::variant<sk_sp<SkStrike>, std::unique_ptr<SkStrikeSpec>> fStrikeOrSpec;
};

// -- StrikeForGPU ---------------------------------------------------------------------------------
class StrikeForGPU {
public:
    virtual ~StrikeForGPU() = default;
    virtual const SkDescriptor& getDescriptor() const = 0;

    // Returns the bounding rectangle of the accepted glyphs. Remember for device masks this
    // rectangle will be in device space, and for transformed masks this rectangle will be in
    // source space.
    virtual SkRect prepareForMaskDrawing(
                SkDrawableGlyphBuffer* accepted,
                SkSourceGlyphBuffer* rejected) = 0;

    virtual SkRect prepareForSDFTDrawing(
                SkScalar strikeToSourceScale,
                SkDrawableGlyphBuffer* accepted,
                SkSourceGlyphBuffer* rejected) = 0;

    virtual void prepareForPathDrawing(
            SkDrawableGlyphBuffer* accepted, SkSourceGlyphBuffer* rejected) = 0;

    virtual void prepareForDrawableDrawing(
            SkDrawableGlyphBuffer* accepted, SkSourceGlyphBuffer* rejected) = 0;

    virtual const SkGlyphPositionRoundingSpec& roundingSpec() const = 0;

    // Used with SkScopedStrikeForGPU to take action at the end of a scope.
    virtual void onAboutToExitScope() = 0;

    // Return underlying SkStrike for building SubRuns while processing glyph runs.
    virtual sk_sp<SkStrike> getUnderlyingStrike() const = 0;

    // Return a strike promise.
    virtual SkStrikePromise strikePromise() = 0;

    // Return the maximum dimension of a span of glyphs.
    virtual SkScalar findMaximumGlyphDimension(SkSpan<const SkGlyphID> glyphs) = 0;

    struct Deleter {
        void operator()(StrikeForGPU* ptr) const {
            ptr->onAboutToExitScope();
        }
    };
};

// -- ScopedStrikeForGPU ---------------------------------------------------------------------------
using ScopedStrikeForGPU = std::unique_ptr<StrikeForGPU, StrikeForGPU::Deleter>;

// prepareForPathDrawing uses this union to convert glyph ids to paths.
union IDOrPath {
    IDOrPath() {}

    // PathOpSubmitter takes care of destroying the paths.
    ~IDOrPath() {}
    SkGlyphID fGlyphID;
    SkPath fPath;
};

// prepareForDrawableDrawing uses this union to convert glyph ids to drawables.
union IDOrDrawable {
    SkGlyphID fGlyphID;
    SkDrawable* fDrawable;
};

// -- StrikeForGPUCacheInterface -------------------------------------------------------------------
class StrikeForGPUCacheInterface {
public:
    virtual ~StrikeForGPUCacheInterface() = default;
    virtual ScopedStrikeForGPU findOrCreateScopedStrike(const SkStrikeSpec& strikeSpec) = 0;
};
}  // namespace sktext
#endif  // sktext_StrikeForGPU_DEFINED
