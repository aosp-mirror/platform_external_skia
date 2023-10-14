/*
 * Copyright 2017 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkUserConfigManual_DEFINED
#define SkUserConfigManual_DEFINED
  #include <android/log.h>
  #define GR_TEST_UTILS 1
  #define SK_BUILD_FOR_ANDROID_FRAMEWORK
  #define SK_DEFAULT_FONT_CACHE_LIMIT   (768 * 1024)
  #define SK_DEFAULT_GLOBAL_DISCARDABLE_MEMORY_POOL_SIZE (512 * 1024)
  #define SK_PRINT_CODEC_MESSAGES
  #define SK_USE_FREETYPE_EMBOLDEN

  // Disable these Ganesh features
  #define SK_DISABLE_REDUCE_OPLIST_SPLITTING
  // Check error is expensive. HWUI historically also doesn't check its allocations
  #define GR_GL_CHECK_ALLOC_WITH_GET_ERROR 0

  // Staging flags
  #define SK_SUPPORT_STROKEANDFILL
  #define SK_DISABLE_LEGACY_SKSURFACE_FLUSH
  #define SK_DISABLE_LEGACY_CANVAS_FLUSH
  #define SK_LEGACY_GPU_GETTERS_CONST
  #define SK_RESOLVE_FILTERS_BEFORE_RESTORE

  // Needed until we fix https://bug.skia.org/2440
  #define SK_SUPPORT_LEGACY_CLIPTOLAYERFLAG
  #define SK_SUPPORT_LEGACY_EMBOSSMASKFILTER
  #define SK_SUPPORT_LEGACY_AAA_CHOICE

  #define SK_ABORT(fmt, ...) __android_log_assert(nullptr, "skia", "[skia] \"" fmt "\" in {%s}",  \
                                                  ##__VA_ARGS__, __PRETTY_FUNCTION__)

  // TODO (b/239048372): Remove this flag when we can safely migrate apps to the
  // new behavior.
  #define SK_SUPPORT_LEGACY_ALPHA_BITMAP_AS_COVERAGE
#endif // SkUserConfigManual_DEFINED
