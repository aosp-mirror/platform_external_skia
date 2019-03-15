/*
 * Copyright 2017 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkAndroidFrameworkUtils.h"

#ifdef SK_BUILD_FOR_ANDROID_FRAMEWORK

#include <log/log.h>

void SkAndroidFrameworkUtils::SafetyNetLog(const char* bugNumber) {
    android_errorWriteLog(0x534e4554, bugNumber);
}

#endif // SK_BUILD_FOR_ANDROID_FRAMEWORK

