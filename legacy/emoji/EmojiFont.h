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

#ifndef android_EmojiFont_DEFINED
#define android_EmojiFont_DEFINED

#include "SkScalar.h"
#include "SkUtils.h"

class SkCanvas;
class SkPaint;

namespace android {

    class EmojiFont {
    public:
        /** Returns true if the underlying emoji font mechanism is available.
         */
        static bool IsAvailable();

        /** Returns index for the corresponding index to the emoji table, or 0
            if there is no matching emoji form.
         */
        static uint16_t UnicharToGlyph(int32_t unichar);

        /** Returns true if the specified glyph is in the emoji range, i.e. was
            returned by UnicharToGlyph or UTF16ToGlyph.
         */
        static bool IsEmojiGlyph(uint16_t index) {
            return index >= kGlyphBase;
        }

        /** Returns the advance width for the specified emoji form.
         */
        static SkScalar GetAdvanceWidth(uint16_t index, const SkPaint& paint);

        /** Draw the specified emoji form, given the x,y origin of the text
            version. The paint is the one associated with the text that has
            the emoji in it.
         */
        static void Draw(SkCanvas*, uint16_t index, SkScalar x, SkScalar y,
                         const SkPaint& paint);

        /** Returns the conver name for Shift_JIS (one of Japanese charset)
         */
        static const char* GetShiftJisConverterName();
    private:
        enum {
            /*  this is our internal trick to embedded private emoji glyph IDs
                along side normal glyphs IDs that come from real fonts. The
                assumption is that normal fonts never will report a glyph ID
                above 20K or 30K, so 64000 should always be a safe starting
                index. We also assume the the number of emoji will not overflow
                16bits starting at 64000 i.e. 65535 - 64000 > total emoji count
             */
            kGlyphBase = 64000
        };
    };
}

#endif
