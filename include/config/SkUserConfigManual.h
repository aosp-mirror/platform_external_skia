/*
 * Copyright 2017 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkUserConfigManual_DEFINED
#define SkUserConfigManual_DEFINED
  #define GR_TEST_UTILS 1
  #define SK_BUILD_FOR_ANDROID_FRAMEWORK
  #define SK_DEFAULT_FONT_CACHE_LIMIT   (768 * 1024)
  #define SK_DEFAULT_GLOBAL_DISCARDABLE_MEMORY_POOL_SIZE (512 * 1024)
  #define SK_USE_FREETYPE_EMBOLDEN

  // Disable these Ganesh features
  #define SK_DISABLE_REDUCE_OPLIST_SPLITTING
  // Check error is expensive. HWUI historically also doesn't check its allocations
  #define GR_GL_CHECK_ALLOC_WITH_GET_ERROR 0

  // Legacy flags
  #define SK_IGNORE_GPU_DITHER
  #define SK_SUPPORT_DEPRECATED_CLIPOPS
  
  // Staging flags
  #define SK_LEGACY_PATH_ARCTO_ENDPOINT
  #define SK_SUPPORT_EXPERIMENTAL_CANVAS44
 
  // Needed until we fix https://bug.skia.org/2440
  #define SK_SUPPORT_LEGACY_CLIPTOLAYERFLAG
  #define SK_SUPPORT_LEGACY_EMBOSSMASKFILTER
  #define SK_SUPPORT_LEGACY_AA_CHOICE
  #define SK_SUPPORT_LEGACY_AAA_CHOICE

  #define SK_DISABLE_DAA  // skbug.com/6886

#endif // SkUserConfigManual_DEFINED
