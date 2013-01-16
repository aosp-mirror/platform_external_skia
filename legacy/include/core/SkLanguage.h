
/*
 * Copyright 2012 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#ifndef SkLanguage_DEFINED
#define SkLanguage_DEFINED

#include "SkTypes.h"

#ifdef SK_BUILD_FOR_ANDROID

#include "SkString.h"

struct SkLanguageInfo {
    SkLanguageInfo(const char* tag) : fTag(tag) { }
    SkString fTag; //! BCP 47 language identifier
};

/** \class SkLanguage

    The SkLanguage class represents a human written language, and is used by
    text draw operations to determine which glyph to draw when drawing
    characters with variants (ie Han-derived characters).
*/
class SkLanguage {
public:
    SkLanguage() : fInfo(getInfo("")) { }
    SkLanguage(const char* tag) : fInfo(getInfo(tag)) { }
    SkLanguage(const SkLanguage& b) : fInfo(b.fInfo) { }

    /** Gets a BCP 47 language identifier for this SkLanguage.
        @return a BCP 47 language identifier representing this language
    */
    const SkString& getTag() const { return fInfo->fTag; }

    /** Performs BCP 47 fallback to return an SkLanguage one step more general.
        @return an SkLanguage one step more general
    */
    SkLanguage getParent() const;

    bool operator==(const SkLanguage& b) const {
        return fInfo == b.fInfo;
    }
    bool operator!=(const SkLanguage& b) const {
        return fInfo != b.fInfo;
    }
    bool operator<(const SkLanguage& b) const {
        return fInfo < b.fInfo;
    }
    bool operator>(const SkLanguage& b) const {
        return fInfo > b.fInfo;
    }
    SkLanguage& operator=(const SkLanguage& b) {
        fInfo = b.fInfo;
        return *this;
    }

private:
    const SkLanguageInfo* fInfo;

    static const SkLanguageInfo* getInfo(const char* tag);
};

#endif // #ifdef SK_BUILD_FOR_ANDROID
#endif // #ifndef SkLanguage_DEFINED
