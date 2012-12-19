/*
 * Copyright 2011 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef FONTHOSTCONFIGURATION_ANDROID_H_
#define FONTHOSTCONFIGURATION_ANDROID_H_

#include "SkTypes.h"

#include "SkLanguage.h"
#include "SkPaint.h"
#include "SkTDArray.h"

struct FontFileInfo {
    FontFileInfo() : fFileName(NULL), fVariant(SkPaint::kDefault_Variant),
            fLanguage() {
    }

    const char*          fFileName;
    SkPaint::FontVariant fVariant;
    SkLanguage           fLanguage;
};

/**
 * The FontFamily data structure is created during parsing and handed back to
 * Skia to fold into its representation of font families. fNames is the list of
 * font names that alias to a font family. fontFileArray is the list of information
 * about each file.  Order is the priority order for the font. This is
 * used internally to determine the order in which to place fallback fonts as
 * they are read from the configuration files.
 */
struct FontFamily {
    SkTDArray<const char*>   fNames;
    SkTDArray<FontFileInfo*> fFontFileArray;
    int order;
};

/**
 * Parses all system font configuration files and returns the results in an
 * array of FontFamily structures.
 */
void getFontFamilies(SkTDArray<FontFamily*> &fontFamilies);

/**
 * Parse only the core system font configuration file and return the results in
 * an array of FontFamily structures.
 */
void getSystemFontFamilies(SkTDArray<FontFamily*> &fontFamilies);

/**
 * Parse the fallback and vendor system font configuration files and return the
 * results in an array of FontFamily structures.
 */
void getFallbackFontFamilies(SkTDArray<FontFamily*> &fallbackFonts);

#endif /* FONTHOSTCONFIGURATION_ANDROID_H_ */
