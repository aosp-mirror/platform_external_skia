/*
 * Copyright 2010, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "SkImageDecoder.h"
#include "SkImageEncoder.h"
#include "SkColorPriv.h"
#include "SkDither.h"
#include "SkScaledBitmapSampler.h"
#include "SkStream.h"
#include "SkTemplates.h"
#include "SkUtils.h"

// A WebP decoder only, on top of (subset of) libwebp
// For more information on WebP image format, and libwebp library, see:
//   http://code.google.com/speed/webp/
//   http://www.webmproject.org/code/#libwebp_webp_image_decoder_library
//   http://review.webmproject.org/gitweb?p=libwebp.git

#include <stdio.h>
extern "C" {
// If moving libwebp out of skia source tree, path for webp headers must be updated accordingly.
// Here, we enforce using local copy in webp sub-directory.
#include "webp/decode.h"
#include "webp/decode_vp8.h"
#include "webp/encode.h"
}

#ifdef ANDROID
#include <cutils/properties.h>

// Key to lookup the size of memory buffer set in system property
static const char KEY_MEM_CAP[] = "ro.media.dec.webp.memcap";
#endif

// this enables timing code to report milliseconds for a decode
//#define TIME_DECODE

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

// Define VP8 I/O on top of Skia stream

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static const size_t WEBP_VP8_HEADER_SIZE = 30;
static const size_t WEBP_IDECODE_BUFFER_SZ = (1 << 16);

// Parse headers of RIFF container, and check for valid Webp (VP8) content.
static bool webp_parse_header(SkStream* stream, int* width, int* height) {
    unsigned char buffer[WEBP_VP8_HEADER_SIZE];
    const size_t len = stream->read(buffer, WEBP_VP8_HEADER_SIZE);
    if (len != WEBP_VP8_HEADER_SIZE) {
        return false; // can't read enough
    }

    if (WebPGetInfo(buffer, WEBP_VP8_HEADER_SIZE, width, height) == 0) {
        return false; // Invalid WebP file.
    }

    // sanity check for image size that's about to be decoded.
    {
        Sk64 size;
        size.setMul(*width, *height);
        if (size.isNeg() || !size.is32()) {
            return false;
        }
        // now check that if we are 4-bytes per pixel, we also don't overflow
        if (size.get32() > (0x7FFFFFFF >> 2)) {
            return false;
        }
    }
    return true;
}

class SkWEBPImageDecoder: public SkImageDecoder {
public:
    virtual Format getFormat() const {
        return kWEBP_Format;
    }

protected:
    virtual bool onDecode(SkStream* stream, SkBitmap* bm, Mode);

private:
    bool setDecodeConfig(SkBitmap* decodedBitmap, int width, int height);
};

//////////////////////////////////////////////////////////////////////////

#include "SkTime.h"

class AutoTimeMillis {
public:
    AutoTimeMillis(const char label[]) :
        fLabel(label) {
        if (!fLabel) {
            fLabel = "";
        }
        fNow = SkTime::GetMSecs();
    }
    ~AutoTimeMillis() {
        SkDebugf("---- Time (ms): %s %d\n", fLabel, SkTime::GetMSecs() - fNow);
    }
private:
    const char* fLabel;
    SkMSec fNow;
};

///////////////////////////////////////////////////////////////////////////////

// This guy exists just to aid in debugging, as it allows debuggers to just
// set a break-point in one place to see all error exists.
static bool return_false(const SkBitmap& bm, const char msg[]) {
#if 0
    SkDebugf("libwebp error %s [%d %d]", msg, bm.width(), bm.height());
#endif
    return false; // must always return false
}

// Incremental WebP image decoding. Reads input buffer of 64K size iteratively
// and decodes this block to appropriate color-space as per config object.
static bool webp_idecode(SkStream* stream, SkBitmap* decodedBitmap) {
    SkAutoLockPixels alp(*decodedBitmap);

    stream->rewind();
    const uint32_t contentSize = stream->getLength();

    WEBP_CSP_MODE mode = MODE_LAST;
    SkBitmap::Config config = decodedBitmap->config();
    if (config == SkBitmap::kARGB_8888_Config) {
      mode = MODE_RGBA;
    } else if (config == SkBitmap::kARGB_4444_Config) {
      mode = MODE_RGBA_4444;
    } else if (config == SkBitmap::kRGB_565_Config) {
      mode = MODE_RGB_565;
    } else {
      return false;
    }

    WebPDecoderConfig decode_config;
    if (WebPInitDecoderConfig(&decode_config) == 0) {
        return false;
    }

    decode_config.output.colorspace = mode;
    decode_config.output.u.RGBA.rgba = (uint8_t*)decodedBitmap->getPixels();
    decode_config.output.u.RGBA.stride = decodedBitmap->rowBytes();
    decode_config.output.u.RGBA.size = decodedBitmap->getSize();
    decode_config.output.is_external_memory = 1;

    WebPIDecoder* idec = WebPIDecode(NULL, NULL, &decode_config);
    if (idec == NULL) {
        WebPFreeDecBuffer(&decode_config.output);
        return false;
    }

    uint32_t read_buffer_size = contentSize;
    if (read_buffer_size > WEBP_IDECODE_BUFFER_SZ) {
        read_buffer_size = WEBP_IDECODE_BUFFER_SZ;
    }
    SkAutoMalloc srcStorage(read_buffer_size);
    unsigned char* input = (uint8_t*)srcStorage.get();
    if (input == NULL) {
        WebPIDelete(idec);
        WebPFreeDecBuffer(&decode_config.output);
        return false;
    }

    uint32_t bytes_remaining = contentSize;
    while (bytes_remaining > 0) {
        const uint32_t bytes_to_read =
            (bytes_remaining > WEBP_IDECODE_BUFFER_SZ) ?
                WEBP_IDECODE_BUFFER_SZ : bytes_remaining;

        const size_t bytes_read = stream->read(input, bytes_to_read);
        if (bytes_read == 0) {
            break;
        }

        VP8StatusCode status = WebPIAppend(idec, input, bytes_read);
        if (status == VP8_STATUS_OK || status == VP8_STATUS_SUSPENDED) {
            bytes_remaining -= bytes_read;
        } else {
            break;
        }
    }
    srcStorage.free();
    WebPIDelete(idec);
    WebPFreeDecBuffer(&decode_config.output);

    if (bytes_remaining > 0) {
        return false;
    } else {
        return true;
    }
}

bool SkWEBPImageDecoder::setDecodeConfig(SkBitmap* decodedBitmap,
                                         int origWidth, int origHeight) {
    bool hasAlpha = false;
    SkBitmap::Config config = this->getPrefConfig(k32Bit_SrcDepth, hasAlpha);

    // only accept prefConfig if it makes sense for us. YUV converter
    // supports output in RGB565, RGBA4444 and RGBA8888 formats.
    if (hasAlpha) {
        if (config != SkBitmap::kARGB_4444_Config) {
            config = SkBitmap::kARGB_8888_Config;
        }
    } else {
        if (config != SkBitmap::kRGB_565_Config &&
            config != SkBitmap::kARGB_4444_Config) {
            config = SkBitmap::kARGB_8888_Config;
        }
    }

    if (!this->chooseFromOneChoice(config, origWidth, origHeight)) {
        return false;
    }

    decodedBitmap->setConfig(config, origWidth, origHeight, 0);

    // Current WEBP specification has no support for alpha layer.
    decodedBitmap->setIsOpaque(true);

    return true;
}


bool SkWEBPImageDecoder::onDecode(SkStream* stream, SkBitmap* decodedBitmap,
                                  Mode mode) {
#ifdef TIME_DECODE
    AutoTimeMillis atm("WEBP Decode");
#endif

    int origWidth, origHeight;
    if (!webp_parse_header(stream, &origWidth, &origHeight)) {
        return false;
    }

    if (!setDecodeConfig(decodedBitmap, origWidth, origHeight)) {
        return false;
    }

    // If only bounds are requested, done
    if (SkImageDecoder::kDecodeBounds_Mode == mode) {
        return true;
    }

    if (!this->allocPixelRef(decodedBitmap, NULL)) {
        return return_false(*decodedBitmap, "allocPixelRef");
    }

    // Decode the WebP image data stream using WebP incremental decoding.
    if (!webp_idecode(stream, decodedBitmap)) {
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////

typedef void (*ScanlineImporter)(const uint8_t* in, uint8_t* out, int width,
                                 const SkPMColor* SK_RESTRICT ctable);

static void ARGB_8888_To_RGB(const uint8_t* in, uint8_t* rgb, int width,
                             const SkPMColor*) {
  const uint32_t* SK_RESTRICT src = (const uint32_t*)in;
  for (int i = 0; i < width; ++i) {
      const uint32_t c = *src++;
      rgb[0] = SkGetPackedR32(c);
      rgb[1] = SkGetPackedG32(c);
      rgb[2] = SkGetPackedB32(c);
      rgb += 3;
  }
}

static void RGB_565_To_RGB(const uint8_t* in, uint8_t* rgb, int width,
                           const SkPMColor*) {
  const uint16_t* SK_RESTRICT src = (const uint16_t*)in;
  for (int i = 0; i < width; ++i) {
      const uint16_t c = *src++;
      rgb[0] = SkPacked16ToR32(c);
      rgb[1] = SkPacked16ToG32(c);
      rgb[2] = SkPacked16ToB32(c);
      rgb += 3;
  }
}

static void ARGB_4444_To_RGB(const uint8_t* in, uint8_t* rgb, int width,
                             const SkPMColor*) {
  const SkPMColor16* SK_RESTRICT src = (const SkPMColor16*)in;
  for (int i = 0; i < width; ++i) {
      const SkPMColor16 c = *src++;
      rgb[0] = SkPacked4444ToR32(c);
      rgb[1] = SkPacked4444ToG32(c);
      rgb[2] = SkPacked4444ToB32(c);
      rgb += 3;
  }
}

static void Index8_To_RGB(const uint8_t* in, uint8_t* rgb, int width,
                          const SkPMColor* SK_RESTRICT ctable) {
  const uint8_t* SK_RESTRICT src = (const uint8_t*)in;
  for (int i = 0; i < width; ++i) {
      const uint32_t c = ctable[*src++];
      rgb[0] = SkGetPackedR32(c);
      rgb[1] = SkGetPackedG32(c);
      rgb[2] = SkGetPackedB32(c);
      rgb += 3;
  }
}

static ScanlineImporter ChooseImporter(const SkBitmap::Config& config) {
    switch (config) {
        case SkBitmap::kARGB_8888_Config:
            return ARGB_8888_To_RGB;
        case SkBitmap::kRGB_565_Config:
            return RGB_565_To_RGB;
        case SkBitmap::kARGB_4444_Config:
            return ARGB_4444_To_RGB;
        case SkBitmap::kIndex8_Config:
            return Index8_To_RGB;
        default:
            return NULL;
    }
}

static int StreamWriter(const uint8_t* data, size_t data_size,
                        const WebPPicture* const picture) {
  SkWStream* const stream = (SkWStream*)picture->custom_ptr;
  return stream->write(data, data_size) ? 1 : 0;
}

class SkWEBPImageEncoder : public SkImageEncoder {
protected:
    virtual bool onEncode(SkWStream* stream, const SkBitmap& bm, int quality);
};

bool SkWEBPImageEncoder::onEncode(SkWStream* stream, const SkBitmap& bm,
                                  int quality) {
    const SkBitmap::Config config = bm.getConfig();
    const ScanlineImporter scanline_import = ChooseImporter(config);
    if (NULL == scanline_import) {
        return false;
    }

    SkAutoLockPixels alp(bm);
    SkAutoLockColors ctLocker;
    if (NULL == bm.getPixels()) {
        return false;
    }

    WebPConfig webp_config;
    if (!WebPConfigPreset(&webp_config, WEBP_PRESET_DEFAULT, quality)) {
        return false;
    }

    WebPPicture pic;
    WebPPictureInit(&pic);
    pic.width = bm.width();
    pic.height = bm.height();
    pic.writer = StreamWriter;
    pic.custom_ptr = (void*)stream;

    const SkPMColor* colors = ctLocker.lockColors(bm);
    const uint8_t* src = (uint8_t*)bm.getPixels();
    const int rgb_stride = pic.width * 3;

    // Import (for each scanline) the bit-map image (in appropriate color-space)
    // to RGB color space.
    uint8_t* rgb = new uint8_t[rgb_stride * pic.height];
    for (int y = 0; y < pic.height; ++y) {
        scanline_import(src + y * bm.rowBytes(), rgb + y * rgb_stride,
                        pic.width, colors);
    }

    bool ok = WebPPictureImportRGB(&pic, rgb, rgb_stride);
    delete[] rgb;

    ok = ok && WebPEncode(&webp_config, &pic);
    WebPPictureFree(&pic);

    return ok;
}


///////////////////////////////////////////////////////////////////////////////

#include "SkTRegistry.h"

static SkImageDecoder* DFactory(SkStream* stream) {
    int width, height;
    if (!webp_parse_header(stream, &width, &height)) {
        return false;
    }

    // Magic matches, call decoder
    return SkNEW(SkWEBPImageDecoder);
}

SkImageDecoder* sk_libwebp_dfactory(SkStream* stream) {
    return DFactory(stream);
}

static SkImageEncoder* EFactory(SkImageEncoder::Type t) {
      return (SkImageEncoder::kWEBP_Type == t) ? SkNEW(SkWEBPImageEncoder) : NULL;
}

SkImageEncoder* sk_libwebp_efactory(SkImageEncoder::Type t) {
    return EFactory(t);
}

static SkTRegistry<SkImageDecoder*, SkStream*> gDReg(sk_libwebp_dfactory);
static SkTRegistry<SkImageEncoder*, SkImageEncoder::Type> gEReg(sk_libwebp_efactory);
