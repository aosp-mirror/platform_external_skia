/* libs/graphics/images/SkImageDecoder.cpp
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
      fUsePrefTable(false) {
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
    // pass a temporary bitmap, so that if we return false, we are assured of
    // leaving the caller's bitmap untouched.
    SkBitmap    tmp;

    // we reset this to false before calling onDecodeRegion
    fShouldCancelDecode = false;
    // assign this, for use by getPrefConfig(), in case fUsePrefTable is false
    fDefaultPref = pref;

    if (!this->onDecodeRegion(&tmp, rect)) {
        return false;
    }
    bm->swap(tmp);
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
    if (w == src->width() && h == src->height() &&
          (srcX - destX) / sampleSize == 0 && (srcY - destY) / sampleSize == 0) {
        // The output rect is the same as the decode result
        dest->swap(*src);
        return;
    }
    dest->setConfig(src->getConfig(), w, h);
    dest->setIsOpaque(src->isOpaque());
    this->allocPixelRef(dest, NULL);

    SkCanvas canvas(*dest);
    canvas.drawBitmap(*src, (srcX - destX) / sampleSize,
                             (srcY - destY) / sampleSize);
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
        }
        return true;
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
