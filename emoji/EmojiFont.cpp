/*
 * Copyright 2009, The Android Open Source Project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"

#include "EmojiFactory.h"
#include "EmojiFont.h"
#include "SkCanvas.h"
#include "SkImageDecoder.h"
#include "SkPaint.h"
#include "SkTSearch.h"
#include "SkUtils.h"

#include "gmoji_pua_table.h"

#include <string.h>

namespace android {

// lazily allocate the factory
static EmojiFactory* get_emoji_factory() {
    static EmojiFactory* gEmojiFactory;
    if (NULL == gEmojiFactory) {
        gEmojiFactory = EmojiFactory::GetAvailableImplementation();
        // we may still be NULL, if there is no impl.
    }
    return gEmojiFactory;
}

#define UNINITIALIZED_ENCODE_SIZE   0   // our array is initialzed with 0s
#define NOT_AVAILABLE_ENCODE_SIZE   -1  // never a legal length for data

struct EncodeDataRec {
    SkBitmap*   fBitmap;
    const void* fData;
    int         fSize;
};

static EncodeDataRec gGmojiEncodeData[GMOJI_PUA_COUNT] = {};

/*  Given a local index, return (initialized if needed) a rec containing the
    encoded data and length. The bitmap field is initialized to 0, and is not
    filled in by this routine per-se.
 */
static EncodeDataRec* get_encoderec(int index) {
    if ((unsigned)index >= GMOJI_PUA_COUNT) {
        SkDebugf("bad index passed to EncodeDataRec& get_encode_data %d\n",
                 index);
        return NULL;
    }

    // lazily fill in the data
    EncodeDataRec* rec = &gGmojiEncodeData[index];

    if (NOT_AVAILABLE_ENCODE_SIZE == rec->fSize) {
        return NULL;
    }
    if (UNINITIALIZED_ENCODE_SIZE == rec->fSize) {
        EmojiFactory* fact = get_emoji_factory();
        if (NULL == fact) {
            return NULL;
        }

        int32_t pua = GMOJI_PUA_MIN + gGmojiPUA[index];
        rec->fData = fact->GetImageBinaryFromAndroidPua(pua, &rec->fSize);
        if (NULL == rec->fData) {
            // flag this entry is not available, so we won't ask again
            rec->fSize = NOT_AVAILABLE_ENCODE_SIZE;
            return NULL;
        }
    }
    return rec;
}

/*  Return the bitmap associated with the local index, or NULL if none is
    available. Note that this will try to cache the bitmap the first time it
    creates it.
 */
static const SkBitmap* get_bitmap(int index) {
    EncodeDataRec* rec = get_encoderec(index);
    SkBitmap* bitmap = NULL;
    if (rec) {
        bitmap = rec->fBitmap;
        if (NULL == bitmap) {
            bitmap = new SkBitmap;
            if (!SkImageDecoder::DecodeMemory(rec->fData, rec->fSize, bitmap)) {
                delete bitmap;
                // we failed, so mark us to not try again
                rec->fSize = NOT_AVAILABLE_ENCODE_SIZE;
                return NULL;
            }
            // cache the answer
            rec->fBitmap = bitmap;
            // todo: we never know if/when to let go of this cached bitmap
            // tho, since the pixels are managed separately, and are purged,
            // the "leak" may not be too important
        }
    }
    return bitmap;
}

///////////////////////////////////////////////////////////////////////////////

bool EmojiFont::IsAvailable() {
    return get_emoji_factory() != NULL;
}

const char *EmojiFont::GetShiftJisConverterName() {
    EmojiFactory* fact = get_emoji_factory();
    if (NULL != fact) {
        if (strcmp(fact->Name(), "kddi") == 0) {
            return "kddi-emoji";
        } else if (strcmp(fact->Name(), "softbank") == 0) {
            return "softbank-emoji";
        }
    }

    // Until Eclair, we have used DoCoMo's Shift_JIS table.
    return "docomo-emoji";
}

uint16_t EmojiFont::UnicharToGlyph(int32_t unichar) {
    // do a quick range check before calling the search routine
    if (unichar >= GMOJI_PUA_MIN && unichar <= GMOJI_PUA_MAX) {
        // our table is stored relative to GMOJI_PUA_MIN to save space (16bits)
        uint16_t relative = unichar - GMOJI_PUA_MIN;
        int index = SkTSearch<uint16_t>(gGmojiPUA, GMOJI_PUA_COUNT, relative,
                                        sizeof(uint16_t));
        // a negative value means it was not found
        if (index >= 0) {
            return index + kGlyphBase;
        }
        // fall through to return 0
    }
    // not a supported emoji pua
    return 0;
}

SkScalar EmojiFont::GetAdvanceWidth(uint16_t glyphID, const SkPaint& paint) {
    if (glyphID < kGlyphBase) {
        SkDebugf("-------- bad glyph passed to EmojiFont::GetAdvanceWidth %d\n",
                 glyphID);
        return 0;
    }

    const SkBitmap* bitmap = get_bitmap(glyphID - kGlyphBase);
    if (NULL == bitmap) {
        return 0;
    }

    // assume that our advance width is always the pointsize
    return paint.getTextSize();
}

/*  This tells us to shift the emoji bounds down by 20% below the baseline,
    to better align with the Kanji characters' placement in the line.
 */
static const SkScalar gBaselinePercentDrop = SkFloatToScalar(0.2f);
    
void EmojiFont::Draw(SkCanvas* canvas, uint16_t glyphID,
                     SkScalar x, SkScalar y, const SkPaint& paint) {
    if (glyphID < kGlyphBase) {
        SkDebugf("-------- bad glyph passed to EmojiFont::Draw %d\n", glyphID);
    }

    const SkBitmap* bitmap = get_bitmap(glyphID - kGlyphBase);
    if (bitmap && !bitmap->empty()) {
        SkRect dst;
        SkScalar size = paint.getTextSize();
        y += SkScalarMul(size, gBaselinePercentDrop);
        dst.set(x, y - size, x + size, y);
        canvas->drawBitmapRect(*bitmap, NULL, dst, &paint);
    }
}

}
