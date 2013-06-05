/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#ifndef SkTypeface_android_DEFINED
#define SkTypeface_android_DEFINED

#include "SkTypeface.h"
#include "SkPaint.h"

#include "../harfbuzz/src/harfbuzz-shaper.h"
#include "../harfbuzz_ng/src/hb.h"

/**
 *  Return a new typeface for a fallback script. If the script is
 *  not valid, or can not map to a font, returns null.
 *  @param  script   The harfbuzz script id.
 *  @param  style    The font style, for example bold
 *  @param  elegant  true if we want the web friendly elegant version of the font
 *  @return          reference to the matching typeface. Caller must call
 *                   unref() when they are done.
 */
SK_API SkTypeface* SkCreateTypefaceForScriptNG(hb_script_t script, SkTypeface::Style style,
        SkPaint::FontVariant fontVariant = SkPaint::kDefault_Variant);

SK_API SkTypeface* SkCreateTypefaceForScript(HB_Script script, SkTypeface::Style style,
        SkPaint::FontVariant fontVariant = SkPaint::kDefault_Variant);

SK_API SkTypeface* SkCreateTypefaceForScript(HB_Script script, SkTypeface::Style style,
			        SkPaintOptionsAndroid::FontVariant fontVariant) {
    return SkCreateTypefaceForScript(script, style, (SkPaint::FontVariant)fontVariant);
}
#endif
