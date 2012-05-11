/* libs/graphics/ports/SkFontHost_android.cpp
**
** Copyright 2006, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include "SkFontHost.h"
#include "SkGraphics.h"
#include "SkDescriptor.h"
#include "SkMMapStream.h"
#include "SkPaint.h"
#include "SkString.h"
#include "SkStream.h"
#include "SkThread.h"
#include "SkTSearch.h"
#include "FontHostConfiguration_android.h"
#include <stdio.h>
#include <string.h>

//#define SkDEBUGF(args       )       SkDebugf args

#ifndef SK_FONT_FILE_PREFIX
    #define SK_FONT_FILE_PREFIX          "/fonts/"
#endif

// Defined in SkFontHost_FreeType.cpp
bool find_name_and_attributes(SkStream* stream, SkString* name,
                              SkTypeface::Style* style, bool* isFixedWidth);

static void getFullPathForSysFonts(SkString* full, const char name[]) {
    full->set(getenv("ANDROID_ROOT"));
    full->append(SK_FONT_FILE_PREFIX);
    full->append(name);
}

static bool getNameAndStyle(const char path[], SkString* name,
                               SkTypeface::Style* style,
                               bool* isFixedWidth, bool isExpected) {
    SkString        fullpath;
    getFullPathForSysFonts(&fullpath, path);

    SkMMAPStream stream(fullpath.c_str());
    if (stream.getLength() > 0) {
        return find_name_and_attributes(&stream, name, style, isFixedWidth);
    }
    else {
        SkFILEStream stream(fullpath.c_str());
        if (stream.getLength() > 0) {
            return find_name_and_attributes(&stream, name, style, isFixedWidth);
        }
    }

    if (isExpected) {
        SkDebugf("---- failed to open <%s> as a font\n", fullpath.c_str());
    }
    return false;
}

static SkTypeface* deserializeLocked(SkStream* stream);
static SkTypeface* createTypefaceLocked(const SkTypeface* familyFace,
        const char familyName[], const void* data, size_t bytelength,
        SkTypeface::Style style);
static SkStream* openStreamLocked(uint32_t fontID);
static size_t getFileNameLocked(SkFontID fontID, char path[], size_t length, int32_t* index);
static SkFontID nextLogicalFontLocked(SkFontID currFontID, SkFontID origFontID);
static SkTypeface* createTypefaceFromStreamLocked(SkStream* stream);

///////////////////////////////////////////////////////////////////////////////

struct FamilyRec;

/*  This guy holds a mapping of a name -> family, used for looking up fonts.
    Since it is stored in a stretchy array that doesn't preserve object
    semantics, we don't use constructor/destructors, but just have explicit
    helpers to manage our internal bookkeeping.
*/
struct NameFamilyPair {
    const char* fName;      // we own this
    FamilyRec*  fFamily;    // we don't own this, we just reference it

    void construct(const char name[], FamilyRec* family) {
        fName = strdup(name);
        fFamily = family;   // we don't own this, so just record the reference
    }

    void destruct() {
        free((char*)fName);
        // we don't own family, so just ignore our reference
    }
};
typedef SkTDArray<NameFamilyPair> NameFamilyPairList;

// we use atomic_inc to grow this for each typeface we create
static int32_t gUniqueFontID;

// this is the mutex that protects all of the global data structures in this module
// functions with the Locked() suffix must be called while holding this mutex
SK_DECLARE_STATIC_MUTEX(gFamilyHeadAndNameListMutex);
static FamilyRec* gFamilyHead = NULL;
static SkTDArray<NameFamilyPair> gFallbackFilenameList;
static NameFamilyPairList gNameList;

struct FamilyRec {
    FamilyRec*  fNext;
    SkTypeface* fFaces[4];

    FamilyRec() : fNext(NULL) {
        memset(fFaces, 0, sizeof(fFaces));
    }
};

static SkTypeface* findBestFaceLocked(const FamilyRec* family,
                                  SkTypeface::Style style) {
    SkTypeface* const* faces = family->fFaces;

    if (faces[style] != NULL) { // exact match
        return faces[style];
    }
    // look for a matching bold
    style = (SkTypeface::Style)(style ^ SkTypeface::kItalic);
    if (faces[style] != NULL) {
        return faces[style];
    }
    // look for the plain
    if (faces[SkTypeface::kNormal] != NULL) {
        return faces[SkTypeface::kNormal];
    }
    // look for anything
    for (int i = 0; i < 4; i++) {
        if (faces[i] != NULL) {
            return faces[i];
        }
    }
    // should never get here, since the faces list should not be empty
    SkDEBUGFAIL("faces list is empty");
    return NULL;
}

static FamilyRec* findFamilyLocked(const SkTypeface* member) {
    FamilyRec* curr = gFamilyHead;
    while (curr != NULL) {
        for (int i = 0; i < 4; i++) {
            if (curr->fFaces[i] == member) {
                return curr;
            }
        }
        curr = curr->fNext;
    }
    return NULL;
}

/*  Returns the matching typeface, or NULL. If a typeface is found, its refcnt
    is not modified.
 */
static SkTypeface* findFromUniqueIDLocked(uint32_t uniqueID) {
    FamilyRec* curr = gFamilyHead;
    while (curr != NULL) {
        for (int i = 0; i < 4; i++) {
            SkTypeface* face = curr->fFaces[i];
            if (face != NULL && face->uniqueID() == uniqueID) {
                return face;
            }
        }
        curr = curr->fNext;
    }
    return NULL;
}

/*  Remove reference to this face from its family. If the resulting family
    is empty (has no faces), return that family, otherwise return NULL
*/
static FamilyRec* removeFromFamilyLocked(const SkTypeface* face) {
    FamilyRec* family = findFamilyLocked(face);
    if (family) {
        SkASSERT(family->fFaces[face->style()] == face);
        family->fFaces[face->style()] = NULL;

        for (int i = 0; i < 4; i++) {
            if (family->fFaces[i] != NULL) {    // family is non-empty
                return NULL;
            }
        }
    } else {
//        SkDebugf("removeFromFamilyLocked(%p) face not found", face);
    }
    return family;  // return the empty family
}

// maybe we should make FamilyRec be doubly-linked
static void detachAndDeleteFamilyLocked(FamilyRec* family) {
    FamilyRec* curr = gFamilyHead;
    FamilyRec* prev = NULL;

    while (curr != NULL) {
        FamilyRec* next = curr->fNext;
        if (curr == family) {
            if (prev == NULL) {
                gFamilyHead = next;
            } else {
                prev->fNext = next;
            }
            SkDELETE(family);
            return;
        }
        prev = curr;
        curr = next;
    }
    SkASSERT(!"Yikes, couldn't find family in our list to remove/delete");
}

static SkTypeface* findTypefaceLocked(const char name[], SkTypeface::Style style) {
    int count = gNameList.count();
    NameFamilyPair* list = gNameList.begin();
    int index = SkStrLCSearch(&list[0].fName, count, name, sizeof(list[0]));
    if (index >= 0) {
        return findBestFaceLocked(list[index].fFamily, style);
    }
    return NULL;
}

static SkTypeface* findTypefaceLocked(const SkTypeface* familyMember,
                                 SkTypeface::Style style) {
    const FamilyRec* family = findFamilyLocked(familyMember);
    return family ? findBestFaceLocked(family, style) : NULL;
}

static void addNameLocked(const char name[], FamilyRec* family) {
    SkAutoAsciiToLC tolc(name);
    name = tolc.lc();

    int count = gNameList.count();
    NameFamilyPair* list = gNameList.begin();
    int index = SkStrLCSearch(&list[0].fName, count, name, sizeof(list[0]));
    if (index < 0) {
        list = gNameList.insert(~index);
        list->construct(name, family);
    }
}

static void removeFromNamesLocked(FamilyRec* emptyFamily) {
#ifdef SK_DEBUG
    for (int i = 0; i < 4; i++) {
        SkASSERT(emptyFamily->fFaces[i] == NULL);
    }
#endif

    // must go backwards when removing
    for (int i = gNameList.count() - 1; i >= 0; --i) {
        NameFamilyPair& pair = gNameList[i];
        if (pair.fFamily == emptyFamily) {
            pair.destruct();
            gNameList.remove(i);
        }
    }
}

static void addTypefaceLocked(SkTypeface* typeface, SkTypeface* familyMember) {
    FamilyRec* rec = NULL;
    if (familyMember) {
        rec = findFamilyLocked(familyMember);
        SkASSERT(rec);
    } else {
        rec = SkNEW(FamilyRec);
        rec->fNext = gFamilyHead;
        gFamilyHead = rec;
    }
    rec->fFaces[typeface->style()] = typeface;
}

static void removeTypeface(SkTypeface* typeface) {
    SkAutoMutexAcquire ac(gFamilyHeadAndNameListMutex);

    // remove us from our family. If the family is now empty, we return
    // that and then remove that family from the name list
    FamilyRec* family = removeFromFamilyLocked(typeface);
    if (NULL != family) {
        removeFromNamesLocked(family);
        detachAndDeleteFamilyLocked(family);
    }
}

///////////////////////////////////////////////////////////////////////////////

class FamilyTypeface : public SkTypeface {
protected:
    FamilyTypeface(Style style, bool sysFont, bool isFixedWidth)
    : SkTypeface(style, sk_atomic_inc(&gUniqueFontID) + 1, isFixedWidth) {
        fIsSysFont = sysFont;
    }

public:
    virtual ~FamilyTypeface() {
        removeTypeface(this);
    }

    bool isSysFont() const { return fIsSysFont; }

    virtual SkStream* openStream() = 0;
    virtual const char* getUniqueString() const = 0;
    virtual const char* getFilePath() const = 0;

private:
    bool    fIsSysFont;

    typedef SkTypeface INHERITED;
};

///////////////////////////////////////////////////////////////////////////////

class StreamTypeface : public FamilyTypeface {
public:
    StreamTypeface(Style style, bool sysFont, SkStream* stream, bool isFixedWidth)
    : INHERITED(style, sysFont, isFixedWidth) {
        SkASSERT(stream);
        stream->ref();
        fStream = stream;
    }

    virtual ~StreamTypeface() {
        fStream->unref();
    }

    // overrides
    virtual SkStream* openStream() {
        // we just ref our existing stream, since the caller will call unref()
        // when they are through
        fStream->ref();
        // must rewind each time, since the caller assumes a "new" stream
        fStream->rewind();
        return fStream;
    }
    virtual const char* getUniqueString() const { return NULL; }
    virtual const char* getFilePath() const { return NULL; }

private:
    SkStream* fStream;

    typedef FamilyTypeface INHERITED;
};

class FileTypeface : public FamilyTypeface {
public:
    FileTypeface(Style style, bool sysFont, const char path[], bool isFixedWidth)
    : INHERITED(style, sysFont, isFixedWidth) {
        fPath.set(path);
    }

    // overrides
    virtual SkStream* openStream() {
        SkStream* stream = SkNEW_ARGS(SkMMAPStream, (fPath.c_str()));

        // check for failure
        if (stream->getLength() <= 0) {
            SkDELETE(stream);
            // maybe MMAP isn't supported. try FILE
            stream = SkNEW_ARGS(SkFILEStream, (fPath.c_str()));
            if (stream->getLength() <= 0) {
                SkDELETE(stream);
                stream = NULL;
            }
        }
        return stream;
    }
    virtual const char* getUniqueString() const {
        const char* str = strrchr(fPath.c_str(), '/');
        if (str) {
            str += 1;   // skip the '/'
        }
        return str;
    }
    virtual const char* getFilePath() const {
        return fPath.c_str();
    }

private:
    SkString fPath;

    typedef FamilyTypeface INHERITED;
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// used to record our notion of the pre-existing fonts
struct FontInitRec {
    const char*         fFileName;
    const char* const*  fNames;     // null-terminated list
};

// deliberately empty, but we use the address to identify fallback fonts
static const char* gFBNames[] = { NULL };


/*  Fonts are grouped by family, with the first font in a family having the
    list of names (even if that list is empty), and the following members having
    null for the list. The names list must be NULL-terminated.
*/
static SkTDArray<FontInitRec> gSystemFonts;
static SkTDArray<SkFontID> gFallbackFonts;

// these globals are assigned (once) by loadSystemFontsLocked()
static FamilyRec* gDefaultFamily = NULL;
static SkTypeface* gDefaultNormal = NULL;
static char** gDefaultNames = NULL;

static void dumpGlobalsLocked() {
    SkDebugf("gDefaultNormal=%p id=%u refCnt=%d", gDefaultNormal,
             gDefaultNormal ? gDefaultNormal->uniqueID() : 0,
             gDefaultNormal ? gDefaultNormal->getRefCnt() : 0);

    if (gDefaultFamily) {
        SkDebugf("gDefaultFamily=%p fFaces={%u,%u,%u,%u} refCnt={%d,%d,%d,%d}",
                 gDefaultFamily,
                 gDefaultFamily->fFaces[0] ? gDefaultFamily->fFaces[0]->uniqueID() : 0,
                 gDefaultFamily->fFaces[1] ? gDefaultFamily->fFaces[1]->uniqueID() : 0,
                 gDefaultFamily->fFaces[2] ? gDefaultFamily->fFaces[2]->uniqueID() : 0,
                 gDefaultFamily->fFaces[3] ? gDefaultFamily->fFaces[3]->uniqueID() : 0,
                 gDefaultFamily->fFaces[0] ? gDefaultFamily->fFaces[0]->getRefCnt() : 0,
                 gDefaultFamily->fFaces[1] ? gDefaultFamily->fFaces[1]->getRefCnt() : 0,
                 gDefaultFamily->fFaces[2] ? gDefaultFamily->fFaces[2]->getRefCnt() : 0,
                 gDefaultFamily->fFaces[3] ? gDefaultFamily->fFaces[3]->getRefCnt() : 0);
    } else {
        SkDebugf("gDefaultFamily=%p", gDefaultFamily);
    }

    SkDebugf("gSystemFonts.count()=%d gFallbackFonts.count()=%d",
            gSystemFonts.count(), gFallbackFonts.count());

    for (int i = 0; i < gSystemFonts.count(); ++i) {
        SkDebugf("gSystemFonts[%d] fileName=%s", i, gSystemFonts[i].fFileName);
        size_t namesIndex = 0;
        if (gSystemFonts[i].fNames)
            for (const char* fontName = gSystemFonts[i].fNames[namesIndex];
                    fontName != 0;
                    fontName = gSystemFonts[i].fNames[++namesIndex]) {
                SkDebugf("       name[%u]=%s", namesIndex, fontName);
            }
    }

    if (gFamilyHead) {
        FamilyRec* rec = gFamilyHead;
        int i=0;
        while (rec) {
            SkDebugf("gFamilyHead[%d]=%p fFaces={%u,%u,%u,%u} refCnt={%d,%d,%d,%d}",
                     i++, rec,
                     rec->fFaces[0] ? rec->fFaces[0]->uniqueID() : 0,
                     rec->fFaces[1] ? rec->fFaces[1]->uniqueID() : 0,
                     rec->fFaces[2] ? rec->fFaces[2]->uniqueID() : 0,
                     rec->fFaces[3] ? rec->fFaces[3]->uniqueID() : 0,
                     rec->fFaces[0] ? rec->fFaces[0]->getRefCnt() : 0,
                     rec->fFaces[1] ? rec->fFaces[1]->getRefCnt() : 0,
                     rec->fFaces[2] ? rec->fFaces[2]->getRefCnt() : 0,
                     rec->fFaces[3] ? rec->fFaces[3]->getRefCnt() : 0);
            rec = rec->fNext;
        }
    } else {
        SkDebugf("gFamilyHead=%p", gFamilyHead);
    }

}


static bool haveSystemFont(const char* filename) {
    for (int i = 0; i < gSystemFonts.count(); i++) {
        if (strcmp(gSystemFonts[i].fFileName, filename) == 0) {
            return true;
        }
    }
    return false;
}

/*  Load info from a configuration file that populates the system/fallback font structures
*/
static void loadFontInfoLocked() {
    SkTDArray<FontFamily*> fontFamilies;
    getFontFamilies(fontFamilies);

    gSystemFonts.reset();

    for (int i = 0; i < fontFamilies.count(); ++i) {
        FontFamily *family = fontFamilies[i];
        for (int j = 0; j < family->fFileNames.count(); ++j) {
            const char* filename = family->fFileNames[j];
            if (haveSystemFont(filename)) {
                SkDebugf("---- system font and fallback font files specify a duplicate "
                        "font %s, skipping the second occurrence", filename);
                continue;
            }

            FontInitRec fontInfoRecord;
            fontInfoRecord.fFileName = filename;
            if (j == 0) {
                if (family->fNames.count() == 0) {
                    // Fallback font
                    fontInfoRecord.fNames = (char **)gFBNames;
                } else {
                    SkTDArray<const char*> names = family->fNames;
                    const char **nameList = (const char**)
                            malloc((names.count() + 1) * sizeof(char*));
                    if (nameList == NULL) {
                        // shouldn't get here
                        break;
                    }
                    if (gDefaultNames == NULL) {
                        gDefaultNames = (char**) nameList;
                    }
                    for (int i = 0; i < names.count(); ++i) {
                        nameList[i] = names[i];
                    }
                    nameList[names.count()] = NULL;
                    fontInfoRecord.fNames = nameList;
                }
            } else {
                fontInfoRecord.fNames = NULL;
            }
            *gSystemFonts.append() = fontInfoRecord;
        }
    }
    fontFamilies.deleteAll();

    SkDEBUGF(("---- We have %d system fonts", gSystemFonts.count()));
    for (int i = 0; i < gSystemFonts.count(); ++i) {
        SkDEBUGF(("---- gSystemFonts[%d] fileName=%s", i, gSystemFonts[i].fFileName));
    }
}


/*
 *  Called once (ensured by the sentinel check at the beginning of our body).
 *  Initializes all the globals, and register the system fonts.
 */
static void initSystemFontsLocked() {
    // check if we've already been called
    if (gDefaultNormal) {
        return;
    }

    SkASSERT(gUniqueFontID == 0);

    loadFontInfoLocked();

    gFallbackFonts.reset();

    SkTypeface* firstInFamily = NULL;
    for (int i = 0; i < gSystemFonts.count(); i++) {
        // if we're the first in a new family, clear firstInFamily
        const char* const* names = gSystemFonts[i].fNames;
        if (names != NULL) {
            firstInFamily = NULL;
        }

        bool isFixedWidth;
        SkString name;
        SkTypeface::Style style;

        // we expect all the fonts, except the "fallback" fonts
        bool isExpected = (names != gFBNames);
        if (!getNameAndStyle(gSystemFonts[i].fFileName, &name, &style,
                &isFixedWidth, isExpected)) {
            // We need to increase gUniqueFontID here so that the unique id of
            // each font matches its index in gSystemFonts array, as expected
            // by findUniqueIDLocked.
            sk_atomic_inc(&gUniqueFontID);
            continue;
        }

        SkString fullpath;
        getFullPathForSysFonts(&fullpath, gSystemFonts[i].fFileName);

        SkTypeface* tf = SkNEW_ARGS(FileTypeface, (style,
                true,  // system-font (cannot delete)
                fullpath.c_str(), // filename
                isFixedWidth));
        addTypefaceLocked(tf, firstInFamily);

        SkDEBUGF(("---- SkTypeface[%d] %s fontID %d\n",
                  i, gSystemFonts[i].fFileName, tf->uniqueID()));

        if (names != NULL) {
            // see if this is one of our fallback fonts
            if (names == gFBNames) {
                SkDEBUGF(("---- adding %s as fallback[%d] fontID %d\n",
                        gSystemFonts[i].fFileName, gFallbackFonts.count(), tf->uniqueID()));
                *gFallbackFonts.append() = tf->uniqueID();
            }

            firstInFamily = tf;
            FamilyRec* family = findFamilyLocked(tf);

            // record the default family if this is it
            if (names == gDefaultNames) {
                gDefaultFamily = family;
            }
            // add the names to map to this family
            while (*names) {
                addNameLocked(*names, family);
                names += 1;
            }
        }
    }

    // do this after all fonts are loaded. This is our default font, and it
    // acts as a sentinel so we only execute loadSystemFontsLocked() once
    gDefaultNormal = findBestFaceLocked(gDefaultFamily, SkTypeface::kNormal);

    SkDEBUGCODE(dumpGlobalsLocked());
}

static SkFontID findUniqueIDLocked(const char* filename) {
    // uniqueID is the index, offset by one, of the associated element in
    // gSystemFonts[] (assumes system fonts are loaded before external fonts)
    // return 0 if not found
    for (int i = 0; i < gSystemFonts.count(); i++) {
        if (strcmp(gSystemFonts[i].fFileName, filename) == 0) {
            return i + 1; // assume unique id of i'th system font is i + 1
        }
    }
    return 0;
}

static int findFallbackFontIndex(SkFontID fontId) {
    for (int i = 0; i < gFallbackFonts.count(); i++) {
        if (gFallbackFonts[i] == fontId) {
            return i;
        }
    }
    return -1;
}

static void reloadFallbackFontsLocked() {
    SkGraphics::PurgeFontCache();

    SkTDArray<FontFamily*> fallbackFamilies;
    getFallbackFontFamilies(fallbackFamilies);

    gFallbackFonts.reset();

    for (int i = 0; i < fallbackFamilies.count(); ++i) {
        FontFamily *family = fallbackFamilies[i];

        for (int j = 0; j < family->fFileNames.count(); ++j) {
            const char* filename = family->fFileNames[j];
            if (filename) {
                if (!haveSystemFont(filename)) {
                    SkDebugf("---- skipping fallback font %s because it was not "
                            "previously loaded as a system font", filename);
                    continue;
                }

                // ensure the fallback font exists before adding it to the list
                bool isFixedWidth;
                SkString name;
                SkTypeface::Style style;
                if (!getNameAndStyle(filename, &name, &style,
                                        &isFixedWidth, false)) {
                    continue;
                }

                SkFontID uniqueID = findUniqueIDLocked(filename);
                SkASSERT(uniqueID != 0);
                if (findFallbackFontIndex(uniqueID) >= 0) {
                    SkDebugf("---- system font and fallback font files specify a duplicate "
                            "font %s, skipping the second occurrence", filename);
                    continue;
                }

                SkDEBUGF(("---- reload %s as fallback[%d] fontID %d\n",
                          filename, gFallbackFonts.count(), uniqueID));

                *gFallbackFonts.append() = uniqueID;
                break;  // The fallback set contains only the first font of each family
            }
        }
    }

    fallbackFamilies.deleteAll();
}

static void loadSystemFontsLocked() {
#if !defined(SK_BUILD_FOR_ANDROID_NDK)
    static char prevLanguage[3];
    static char prevRegion[3];
    char language[3] = "";
    char region[3] = "";

    getLocale(language, region);

    if (!gDefaultNormal) {
        strncpy(prevLanguage, language, 2);
        strncpy(prevRegion, region, 2);
        initSystemFontsLocked();
    } else if (strncmp(language, prevLanguage, 2) || strncmp(region, prevRegion, 2)) {
        strncpy(prevLanguage, language, 2);
        strncpy(prevRegion, region, 2);
        reloadFallbackFontsLocked();
    }
#else
    if (!gDefaultNormal) {
        initSystemFontsLocked();
        reloadFallbackFontsLocked();
    }
#endif
}

///////////////////////////////////////////////////////////////////////////////

void SkFontHost::Serialize(const SkTypeface* face, SkWStream* stream) {
    // lookup and record if the font is custom (i.e. not a system font)
    bool isCustomFont = !((FamilyTypeface*)face)->isSysFont();
    stream->writeBool(isCustomFont);

    if (isCustomFont) {
        SkStream* fontStream = ((FamilyTypeface*)face)->openStream();

        // store the length of the custom font
        uint32_t len = fontStream->getLength();
        stream->write32(len);

        // store the entire font in the serialized stream
        void* fontData = malloc(len);

        fontStream->read(fontData, len);
        stream->write(fontData, len);

        fontStream->unref();
        free(fontData);
//      SkDebugf("--- fonthost custom serialize %d %d\n", face->style(), len);

    } else {
        const char* name = ((FamilyTypeface*)face)->getUniqueString();

        stream->write8((uint8_t)face->style());

        if (NULL == name || 0 == *name) {
            stream->writePackedUInt(0);
//          SkDebugf("--- fonthost serialize null\n");
        } else {
            uint32_t len = strlen(name);
            stream->writePackedUInt(len);
            stream->write(name, len);
//          SkDebugf("--- fonthost serialize <%s> %d\n", name, face->style());
        }
    }
}

SkTypeface* SkFontHost::Deserialize(SkStream* stream) {
    SkAutoMutexAcquire  ac(gFamilyHeadAndNameListMutex);
    return deserializeLocked(stream);
}

static SkTypeface* deserializeLocked(SkStream* stream) {
    loadSystemFontsLocked();

    // check if the font is a custom or system font
    bool isCustomFont = stream->readBool();

    if (isCustomFont) {

        // read the length of the custom font from the stream
        uint32_t len = stream->readU32();

        // generate a new stream to store the custom typeface
        SkMemoryStream* fontStream = new SkMemoryStream(len);
        stream->read((void*)fontStream->getMemoryBase(), len);

        SkTypeface* face = createTypefaceFromStreamLocked(fontStream);

        fontStream->unref();

//      SkDebugf("--- fonthost custom deserialize %d %d\n", face->style(), len);
        return face;

    } else {
        int style = stream->readU8();

        int len = stream->readPackedUInt();
        if (len > 0) {
            SkString str;
            str.resize(len);
            stream->read(str.writable_str(), len);

            for (int i = 0; i < gSystemFonts.count(); i++) {
                if (strcmp(gSystemFonts[i].fFileName, str.c_str()) == 0) {
                    // backup until we hit the fNames
                    for (int j = i; j >= 0; --j) {
                        if (gSystemFonts[j].fNames != NULL) {
                            return createTypefaceLocked(NULL,
                                    gSystemFonts[j].fNames[0], NULL, 0,
                                    (SkTypeface::Style)style);
                        }
                    }
                }
            }
        }
    }
    return NULL;
}

///////////////////////////////////////////////////////////////////////////////

SkTypeface* SkFontHost::CreateTypeface(const SkTypeface* familyFace,
                                       const char familyName[],
                                       const void* data, size_t bytelength,
                                       SkTypeface::Style style) {
    SkAutoMutexAcquire  ac(gFamilyHeadAndNameListMutex);
    return createTypefaceLocked(familyFace, familyName, data, bytelength, style);
}

static SkTypeface* createTypefaceLocked(const SkTypeface* familyFace,
        const char familyName[], const void* data, size_t bytelength,
        SkTypeface::Style style) {
    loadSystemFontsLocked();

    // clip to legal style bits
    style = (SkTypeface::Style)(style & SkTypeface::kBoldItalic);

    SkTypeface* tf = NULL;

    if (NULL != familyFace) {
        tf = findTypefaceLocked(familyFace, style);
    } else if (NULL != familyName) {
//        SkDebugf("======= familyName <%s>\n", familyName);
        tf = findTypefaceLocked(familyName, style);
    }

    if (NULL == tf) {
        tf = findBestFaceLocked(gDefaultFamily, style);
    }

    // we ref(), since the semantic is to return a new instance
    tf->ref();
    return tf;
}

SkStream* SkFontHost::OpenStream(uint32_t fontID) {
    SkAutoMutexAcquire  ac(gFamilyHeadAndNameListMutex);
    return openStreamLocked(fontID);
}

static SkStream* openStreamLocked(uint32_t fontID) {
    FamilyTypeface* tf = (FamilyTypeface*)findFromUniqueIDLocked(fontID);
    SkStream* stream = tf ? tf->openStream() : NULL;

    if (stream && stream->getLength() == 0) {
        stream->unref();
        stream = NULL;
    }
    return stream;
}

size_t SkFontHost::GetFileName(SkFontID fontID, char path[], size_t length,
                               int32_t* index) {
    SkAutoMutexAcquire  ac(gFamilyHeadAndNameListMutex);
    return getFileNameLocked(fontID, path, length, index);
}

static size_t getFileNameLocked(SkFontID fontID, char path[], size_t length, int32_t* index) {
    FamilyTypeface* tf = (FamilyTypeface*)findFromUniqueIDLocked(fontID);
    const char* src = tf ? tf->getFilePath() : NULL;

    if (src) {
        size_t size = strlen(src);
        if (path) {
            memcpy(path, src, SkMin32(size, length));
        }
        if (index) {
            *index = 0; // we don't have collections (yet)
        }
        return size;
    } else {
        return 0;
    }
}

SkFontID SkFontHost::NextLogicalFont(SkFontID currFontID, SkFontID origFontID) {
    SkAutoMutexAcquire  ac(gFamilyHeadAndNameListMutex);
    return nextLogicalFontLocked(currFontID, origFontID);
}

static SkFontID nextLogicalFontLocked(SkFontID currFontID, SkFontID origFontID) {
    loadSystemFontsLocked();

    const SkTypeface* origTypeface = findFromUniqueIDLocked(origFontID);
    const SkTypeface* currTypeface = findFromUniqueIDLocked(currFontID);

    SkASSERT(origTypeface != 0);
    SkASSERT(currTypeface != 0);

    // Our fallback list always stores the id of the plain in each fallback
    // family, so we transform currFontID to its plain equivalent.
    SkFontID plainFontID = findTypefaceLocked(currTypeface, SkTypeface::kNormal)->uniqueID();

    /*  First see if fontID is already one of our fallbacks. If so, return
        its successor. If fontID is not in our list, then return the first one
        in our list. Note: list is zero-terminated, and returning zero means
        we have no more fonts to use for fallbacks.
     */
    int plainFallbackFontIndex = findFallbackFontIndex(plainFontID);
    int nextFallbackFontIndex = plainFallbackFontIndex + 1;
    SkFontID nextFontID;
    if (nextFallbackFontIndex == gFallbackFonts.count()) {
        nextFontID = 0; // no more fallbacks
    } else {
        const SkTypeface* nextTypeface = findFromUniqueIDLocked(gFallbackFonts[nextFallbackFontIndex]);
        nextFontID = findTypefaceLocked(nextTypeface, origTypeface->style())->uniqueID();
    }

    SkDEBUGF(("---- nextLogicalFont: currFontID=%d, origFontID=%d, plainFontID=%d, "
            "plainFallbackFontIndex=%d, nextFallbackFontIndex=%d "
            "=> nextFontID=%d", currFontID, origFontID, plainFontID,
            plainFallbackFontIndex, nextFallbackFontIndex, nextFontID));
    return nextFontID;
}

///////////////////////////////////////////////////////////////////////////////

SkTypeface* SkFontHost::CreateTypefaceFromStream(SkStream* stream) {
    SkAutoMutexAcquire  ac(gFamilyHeadAndNameListMutex);
    return createTypefaceFromStreamLocked(stream);
}

static SkTypeface* createTypefaceFromStreamLocked(SkStream* stream) {
    if (NULL == stream || stream->getLength() <= 0) {
        return NULL;
    }

    // Make sure system fonts are loaded first to comply with the assumption
    // that the font's uniqueID can be found using the findUniqueIDLocked method.
    loadSystemFontsLocked();

    bool isFixedWidth;
    SkTypeface::Style style;

    if (find_name_and_attributes(stream, NULL, &style, &isFixedWidth)) {
        SkTypeface* typeface = SkNEW_ARGS(StreamTypeface, (style, false, stream, isFixedWidth));
        addTypefaceLocked(typeface, NULL);
        return typeface;
    } else {
        return NULL;
    }
}

SkTypeface* SkFontHost::CreateTypefaceFromFile(const char path[]) {
    SkStream* stream = SkNEW_ARGS(SkMMAPStream, (path));
    SkTypeface* face = SkFontHost::CreateTypefaceFromStream(stream);
    // since we created the stream, we let go of our ref() here
    stream->unref();
    return face;
}
