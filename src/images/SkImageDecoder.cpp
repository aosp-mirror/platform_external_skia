
/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#include "SkImageDecoder.h"
#include "SkBitmap.h"
#include "SkPixelRef.h"
#include "SkStream.h"
#include "SkTemplates.h"
#include "SkCanvas.h"

SkVMMemoryReporter::~SkVMMemoryReporter() {
}

const char *SkImageDecoder::kFormatName[] = {
    "Unknown Format",
    "BMP",
    "GIF",
    "ICO",
    "JPEG",
    "PNG",
    "WBMP",
    "WEBP",
};

static SkBitmap::Config gDeviceConfig = SkBitmap::kNo_Config;

SkBitmap::Config SkImageDecoder::GetDeviceConfig()
{
    return gDeviceConfig;
}

void SkImageDecoder::SetDeviceConfig(SkBitmap::Config config)
{
    gDeviceConfig = config;
}

///////////////////////////////////////////////////////////////////////////////

SkImageDecoder::SkImageDecoder()
    : fReporter(NULL), fPeeker(NULL), fChooser(NULL), fAllocator(NULL),
      fSampleSize(1), fDefaultPref(SkBitmap::kNo_Config), fDitherImage(true),
      fUsePrefTable(false),fPreferQualityOverSpeed(false) {
}

SkImageDecoder::~SkImageDecoder() {
    SkSafeUnref(fPeeker);
    SkSafeUnref(fChooser);
    SkSafeUnref(fAllocator);
    SkSafeUnref(fReporter);
}

SkImageDecoder::Format SkImageDecoder::getFormat() const {
    return kUnknown_Format;
}

SkImageDecoder::Peeker* SkImageDecoder::setPeeker(Peeker* peeker) {
    SkRefCnt_SafeAssign(fPeeker, peeker);
    return peeker;
}

SkImageDecoder::Chooser* SkImageDecoder::setChooser(Chooser* chooser) {
    SkRefCnt_SafeAssign(fChooser, chooser);
    return chooser;
}

SkBitmap::Allocator* SkImageDecoder::setAllocator(SkBitmap::Allocator* alloc) {
    SkRefCnt_SafeAssign(fAllocator, alloc);
    return alloc;
}

SkVMMemoryReporter* SkImageDecoder::setReporter(SkVMMemoryReporter* reporter) {
    SkRefCnt_SafeAssign(fReporter, reporter);
    return reporter;
}

void SkImageDecoder::setSampleSize(int size) {
    if (size < 1) {
        size = 1;
    }
    fSampleSize = size;
}

bool SkImageDecoder::chooseFromOneChoice(SkBitmap::Config config, int width,
                                         int height) const {
    Chooser* chooser = fChooser;

    if (NULL == chooser) {    // no chooser, we just say YES to decoding :)
        return true;
    }
    chooser->begin(1);
    chooser->inspect(0, config, width, height);
    return chooser->choose() == 0;
}

bool SkImageDecoder::allocPixelRef(SkBitmap* bitmap,
                                   SkColorTable* ctable) const {
    return bitmap->allocPixels(fAllocator, ctable);
}

///////////////////////////////////////////////////////////////////////////////

void SkImageDecoder::setPrefConfigTable(const SkBitmap::Config pref[6]) {
    if (NULL == pref) {
        fUsePrefTable = false;
    } else {
        fUsePrefTable = true;
        memcpy(fPrefTable, pref, sizeof(fPrefTable));
    }
}

SkBitmap::Config SkImageDecoder::getPrefConfig(SrcDepth srcDepth,
                                               bool srcHasAlpha) const {
    SkBitmap::Config config;

    if (fUsePrefTable) {
        int index = 0;
        switch (srcDepth) {
            case kIndex_SrcDepth:
                index = 0;
                break;
            case k16Bit_SrcDepth:
                index = 2;
                break;
            case k32Bit_SrcDepth:
                index = 4;
                break;
        }
        if (srcHasAlpha) {
            index += 1;
        }
        config = fPrefTable[index];
    } else {
        config = fDefaultPref;
    }

    if (SkBitmap::kNo_Config == config) {
        config = SkImageDecoder::GetDeviceConfig();
    }
    return config;
}

bool SkImageDecoder::decode(SkStream* stream, SkBitmap* bm,
                            SkBitmap::Config pref, Mode mode, bool reuseBitmap) {
    // pass a temporary bitmap, so that if we return false, we are assured of
    // leaving the caller's bitmap untouched.
    SkBitmap    tmp;

    // we reset this to false before calling onDecode
    fShouldCancelDecode = false;
    // assign this, for use by getPrefConfig(), in case fUsePrefTable is false
    fDefaultPref = pref;

    if (reuseBitmap) {
        SkAutoLockPixels alp(*bm);
        if (bm->getPixels() != NULL) {
            return this->onDecode(stream, bm, mode);
        }
    }
    if (!this->onDecode(stream, &tmp, mode)) {
        return false;
    }
    bm->swap(tmp);
    return true;
}

bool SkImageDecoder::decodeRegion(SkBitmap* bm, SkIRect rect,
                                  SkBitmap::Config pref) {
    // we reset this to false before calling onDecodeRegion
    fShouldCancelDecode = false;
    // assign this, for use by getPrefConfig(), in case fUsePrefTable is false
    fDefaultPref = pref;

    if (!this->onDecodeRegion(bm, rect)) {
        return false;
    }
    return true;
}

bool SkImageDecoder::buildTileIndex(SkStream* stream,
                                int *width, int *height) {
    // we reset this to false before calling onBuildTileIndex
    fShouldCancelDecode = false;

    return this->onBuildTileIndex(stream, width, height);
}

void SkImageDecoder::cropBitmap(SkBitmap *dest, SkBitmap *src,
                                    int sampleSize, int destX, int destY,
                                    int width, int height, int srcX, int srcY) {
    int w = width / sampleSize;
    int h = height / sampleSize;
    // if the destination has no pixels then we must allocate them.
    if (dest->isNull()) {
        dest->setConfig(src->getConfig(), w, h);
        dest->setIsOpaque(src->isOpaque());

        if (!this->allocPixelRef(dest, NULL)) {
            SkDEBUGF(("failed to allocate pixels needed to crop the bitmap"));
            return;
        }
    }
    // check to see if the destination is large enough to decode the desired
    // region. If this assert fails we will just draw as much of the source
    // into the destination that we can.
    SkASSERT(dest->width() >= w && dest->height() >= h);

    // Set the Src_Mode for the paint to prevent transparency issue in the
    // dest in the event that the dest was being re-used.
    SkPaint paint;
    paint.setXfermodeMode(SkXfermode::kSrc_Mode);

    SkCanvas canvas(*dest);
    canvas.drawSprite(*src, (srcX - destX) / sampleSize,
                            (srcY - destY) / sampleSize,
                            &paint);
}

///////////////////////////////////////////////////////////////////////////////

bool SkImageDecoder::DecodeFile(const char file[], SkBitmap* bm,
                            SkBitmap::Config pref,  Mode mode, Format* format) {
    SkASSERT(file);
    SkASSERT(bm);

    SkFILEStream    stream(file);
    if (stream.isValid()) {
        if (SkImageDecoder::DecodeStream(&stream, bm, pref, mode, format)) {
            bm->pixelRef()->setURI(file);
            return true;
        }
    }
    return false;
}

bool SkImageDecoder::DecodeMemory(const void* buffer, size_t size, SkBitmap* bm,
                          SkBitmap::Config pref, Mode mode, Format* format) {
    if (0 == size) {
        return false;
    }
    SkASSERT(buffer);

    SkMemoryStream  stream(buffer, size);
    return SkImageDecoder::DecodeStream(&stream, bm, pref, mode, format);
}

bool SkImageDecoder::DecodeStream(SkStream* stream, SkBitmap* bm,
                          SkBitmap::Config pref, Mode mode, Format* format) {
    SkASSERT(stream);
    SkASSERT(bm);

    bool success = false;
    SkImageDecoder* codec = SkImageDecoder::Factory(stream);

    if (NULL != codec) {
        success = codec->decode(stream, bm, pref, mode);
        if (success && format) {
            *format = codec->getFormat();
        }
        delete codec;
    }
    return success;
}
