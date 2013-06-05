/*
 * Copyright 2012 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#ifndef SkPaintOptionsAndroid_DEFINED
#define SkPaintOptionsAndroid_DEFINED

class SkPaintOptionsAndroid {
public:
    void setLanguage(const SkLanguage& language) {}
    void setUseFontFallbacks(bool useFontFallbacks) {}
    enum FontVariant { kElegant_Variant };
};

#endif // #ifndef SkPaintOptionsAndroid_DEFINED
