/*
 * Copyright 2008 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkFontMgr.h"

#if !defined(SK_DISABLE_LEGACY_FONTMGR_FACTORY)
sk_sp<SkFontMgr> SkFontMgr::Factory() {
    // Always return nullptr, an empty SkFontMgr will be used.
    return nullptr;
}
#endif
