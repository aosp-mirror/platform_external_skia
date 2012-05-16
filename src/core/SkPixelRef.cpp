
/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "SkPixelRef.h"
#include "SkFlattenable.h"
#include "SkThread.h"

// must be a power-of-2. undef to just use 1 mutex
#define PIXELREF_MUTEX_RING_COUNT       32

#ifdef PIXELREF_MUTEX_RING_COUNT
    static int32_t gPixelRefMutexRingIndex;
    static SK_DECLARE_MUTEX_ARRAY(gPixelRefMutexRing, PIXELREF_MUTEX_RING_COUNT);
#else
    SK_DECLARE_STATIC_MUTEX(gPixelRefMutex);
#endif

SkBaseMutex* get_default_mutex() {
#ifdef PIXELREF_MUTEX_RING_COUNT
    // atomic_inc might be overkill here. It may be fine if once in a while
    // we hit a race-condition and two subsequent calls get the same index...
    int index = sk_atomic_inc(&gPixelRefMutexRingIndex);
    return &gPixelRefMutexRing[index & (PIXELREF_MUTEX_RING_COUNT - 1)];
#else
    return &gPixelRefMutex;
#endif
}

///////////////////////////////////////////////////////////////////////////////

extern int32_t SkNextPixelRefGenerationID();
int32_t SkNextPixelRefGenerationID() {
    static int32_t  gPixelRefGenerationID;
    // do a loop in case our global wraps around, as we never want to
    // return a 0
    int32_t genID;
    do {
        genID = sk_atomic_inc(&gPixelRefGenerationID) + 1;
    } while (0 == genID);
    return genID;
}

///////////////////////////////////////////////////////////////////////////////

void SkPixelRef::setMutex(SkBaseMutex* mutex) {
    if (NULL == mutex) {
        mutex = get_default_mutex();
    }
    fMutex = mutex;
}

// just need a > 0 value, so pick a funny one to aid in debugging
#define SKPIXELREF_PRELOCKED_LOCKCOUNT     123456789

SkPixelRef::SkPixelRef(SkBaseMutex* mutex) : fPreLocked(false) {
    this->setMutex(mutex);
    fPixels = NULL;
    fColorTable = NULL; // we do not track ownership of this
    fLockCount = 0;
    fGenerationID = 0;  // signal to rebuild
    fIsImmutable = false;
    fPreLocked = false;
}

SkPixelRef::SkPixelRef(SkFlattenableReadBuffer& buffer, SkBaseMutex* mutex) {
    this->setMutex(mutex);
    fPixels = NULL;
    fColorTable = NULL; // we do not track ownership of this
    fLockCount = 0;
    fGenerationID = 0;  // signal to rebuild
    fIsImmutable = buffer.readBool();
    fPreLocked = false;
}

void SkPixelRef::setPreLocked(void* pixels, SkColorTable* ctable) {
    // only call me in your constructor, otherwise fLockCount tracking can get
    // out of sync.
    fPixels = pixels;
    fColorTable = ctable;
    fLockCount = SKPIXELREF_PRELOCKED_LOCKCOUNT;
    fPreLocked = true;
}

void SkPixelRef::flatten(SkFlattenableWriteBuffer& buffer) const {
    buffer.writeBool(fIsImmutable);
}

void SkPixelRef::lockPixels() {
    SkASSERT(!fPreLocked || SKPIXELREF_PRELOCKED_LOCKCOUNT == fLockCount);

    if (!fPreLocked) {
        SkAutoMutexAcquire  ac(*fMutex);

        if (1 == ++fLockCount) {
            fPixels = this->onLockPixels(&fColorTable);
        }
    }
}

void SkPixelRef::unlockPixels() {
    SkASSERT(!fPreLocked || SKPIXELREF_PRELOCKED_LOCKCOUNT == fLockCount);
    
    if (!fPreLocked) {
        SkAutoMutexAcquire  ac(*fMutex);

        SkASSERT(fLockCount > 0);
        if (0 == --fLockCount) {
            this->onUnlockPixels();
            fPixels = NULL;
            fColorTable = NULL;
        }
    }
}

bool SkPixelRef::lockPixelsAreWritable() const {
    return this->onLockPixelsAreWritable();
}

bool SkPixelRef::onLockPixelsAreWritable() const {
    return true;
}

uint32_t SkPixelRef::getGenerationID() const {
    if (0 == fGenerationID) {
        fGenerationID = SkNextPixelRefGenerationID();
    }
    return fGenerationID;
}

void SkPixelRef::notifyPixelsChanged() {
#ifdef SK_DEBUG
    if (fIsImmutable) {
        SkDebugf("========== notifyPixelsChanged called on immutable pixelref");
    }
#endif
    // this signals us to recompute this next time around
    fGenerationID = 0;
}

void SkPixelRef::setImmutable() {
    fIsImmutable = true;
}

bool SkPixelRef::readPixels(SkBitmap* dst, const SkIRect* subset) {
    return this->onReadPixels(dst, subset);
}

bool SkPixelRef::onReadPixels(SkBitmap* dst, const SkIRect* subset) {
    return false;
}

///////////////////////////////////////////////////////////////////////////////

#define MAX_PAIR_COUNT  16

struct Pair {
    const char*          fName;
    SkPixelRef::Factory  fFactory;
};

static int gCount;
static Pair gPairs[MAX_PAIR_COUNT];

void SkPixelRef::Register(const char name[], Factory factory) {
    SkASSERT(name);
    SkASSERT(factory);

    static bool gOnce;
    if (!gOnce) {
        gCount = 0;
        gOnce = true;
    }

    SkASSERT(gCount < MAX_PAIR_COUNT);

    gPairs[gCount].fName = name;
    gPairs[gCount].fFactory = factory;
    gCount += 1;
}

#if !SK_ALLOW_STATIC_GLOBAL_INITIALIZERS && defined(SK_DEBUG)
static void report_no_entries(const char* functionName) {
    if (!gCount) {
        SkDebugf("%s has no registered name/factory pairs."
                 " Call SkGraphics::Init() at process initialization time.",
                 functionName);
    }
}
#endif

SkPixelRef::Factory SkPixelRef::NameToFactory(const char name[]) {
#if !SK_ALLOW_STATIC_GLOBAL_INITIALIZERS && defined(SK_DEBUG)
    report_no_entries(__FUNCTION__);
#endif
    const Pair* pairs = gPairs;
    for (int i = gCount - 1; i >= 0; --i) {
        if (strcmp(pairs[i].fName, name) == 0) {
            return pairs[i].fFactory;
        }
    }
    return NULL;
}

const char* SkPixelRef::FactoryToName(Factory fact) {
#if !SK_ALLOW_STATIC_GLOBAL_INITIALIZERS && defined(SK_DEBUG)
    report_no_entries(__FUNCTION__);
#endif
    const Pair* pairs = gPairs;
    for (int i = gCount - 1; i >= 0; --i) {
        if (pairs[i].fFactory == fact) {
            return pairs[i].fName;
        }
    }
    return NULL;
}

///////////////////////////////////////////////////////////////////////////////

#ifdef SK_BUILD_FOR_ANDROID
void SkPixelRef::globalRef(void* data) {
    this->ref();
}

void SkPixelRef::globalUnref() {
    this->unref();
}
#endif
