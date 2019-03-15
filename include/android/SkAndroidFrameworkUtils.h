/*
 * Copyright 2017 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkAndroidFrameworkUtils_DEFINED
#define SkAndroidFrameworkUtils_DEFINED

#include "SkTypes.h"

#ifdef SK_BUILD_FOR_ANDROID_FRAMEWORK

/**
 *  SkAndroidFrameworkUtils expose private APIs used only by Android framework.
 */
class SkAndroidFrameworkUtils {
public:
    static void SafetyNetLog(const char*);
};

#endif // SK_BUILD_FOR_ANDROID_FRAMEWORK

#endif // SkAndroidFrameworkUtils_DEFINED
