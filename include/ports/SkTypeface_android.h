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

enum FallbackScripts {
    kArabic_FallbackScript,
    kArmenian_FallbackScript,
    kBengali_FallbackScript,
    kDevanagari_FallbackScript,
    kEthiopic_FallbackScript,
    kGeorgian_FallbackScript,
    kHebrewRegular_FallbackScript,
    kHebrewBold_FallbackScript,
    kKannada_FallbackScript,
    kMalayalam_FallbackScript,
    kTamilRegular_FallbackScript,
    kTamilBold_FallbackScript,
    kThai_FallbackScript,
    kTelugu_FallbackScript,
    kFallbackScriptNumber
};


SK_API SkTypeface* SkCreateTypefaceForScript(HB_Script script, SkTypeface::Style style,
        SkPaint::FontVariant fontVariant) { return NULL; }

#endif
