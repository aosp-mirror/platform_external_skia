/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkRefCnt_android_DEFINED
#define SkRefCnt_android_DEFINED

/**
 *  Android's version of SkRefCnt.
 *
 *  Needed so that Chromium Webview, based on a release version of Chromium
 *  that calls SkRefCnt::deref(), can link against the system's copy of Skia.
 */
class SK_API SkRefCnt : public SkRefCntBase {
public:
  void deref() const { SkRefCntBase::unref(); }
};
#endif  // SkRefCnt_android_DEFINED
