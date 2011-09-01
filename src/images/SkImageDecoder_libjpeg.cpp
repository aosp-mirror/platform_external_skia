/*
 * Copyright 2007, The Android Open Source Project
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
#include "SkJpegUtility.h"
#include "SkColorPriv.h"
#include "SkDither.h"
#include "SkScaledBitmapSampler.h"
#include "SkStream.h"
#include "SkTemplates.h"
#include "SkUtils.h"
#include "SkRect.h"
#include "SkCanvas.h"

#include <stdio.h>
extern "C" {
    #include "jpeglib.h"
    #include "jerror.h"
}

#ifdef ANDROID
#include <cutils/properties.h>

// Key to lookup the size of memory buffer set in system property
static const char KEY_MEM_CAP[] = "ro.media.dec.jpeg.memcap";
#endif

// this enables timing code to report milliseconds for an encode
//#define TIME_ENCODE
//#define TIME_DECODE

// this enables our rgb->yuv code, which is faster than libjpeg on ARM
// disable for the moment, as we have some glitches when width != multiple of 4
#define WE_CONVERT_TO_YUV

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

class SkJPEGImageIndex {
public:
    SkJPEGImageIndex() {}
    virtual ~SkJPEGImageIndex() {
        jpeg_destroy_huffman_index(index);
        delete cinfo->src;
        jpeg_finish_decompress(cinfo);
        jpeg_destroy_decompress(cinfo);
        free(cinfo);
    }
    jpeg_decompress_struct *cinfo;
    huffman_index *index;
};


class SkJPEGImageDecoder : public SkImageDecoder {
public:
    SkJPEGImageDecoder() {
        index = NULL;
    }
    ~SkJPEGImageDecoder() {
        if (index)
            delete index;
    }
    virtual Format getFormat() const {
        return kJPEG_Format;
    }
protected:
    virtual bool onBuildTileIndex(SkStream *stream,
                                int *width, int *height);
    virtual bool onDecodeRegion(SkBitmap* bitmap, SkIRect rect);
    virtual bool onDecode(SkStream* stream, SkBitmap* bm, Mode);
private:
    SkJPEGImageIndex *index;
};

//////////////////////////////////////////////////////////////////////////

#include "SkTime.h"

class AutoTimeMillis {
public:
    AutoTimeMillis(const char label[]) : fLabel(label) {
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
    SkMSec      fNow;
};

/* Automatically clean up after throwing an exception */
class JPEGAutoClean {
public:
    JPEGAutoClean(): cinfo_ptr(NULL) {}
    ~JPEGAutoClean() {
        if (cinfo_ptr) {
            jpeg_destroy_decompress(cinfo_ptr);
        }
    }
    void set(jpeg_decompress_struct* info) {
        cinfo_ptr = info;
    }
private:
    jpeg_decompress_struct* cinfo_ptr;
};

#ifdef ANDROID
/* Check if the memory cap property is set.
   If so, use the memory size for jpeg decode.
*/
static void overwrite_mem_buffer_size(j_decompress_ptr cinfo) {
#ifdef ANDROID_LARGE_MEMORY_DEVICE
    cinfo->mem->max_memory_to_use = 30 * 1024 * 1024;
#else
    cinfo->mem->max_memory_to_use = 5 * 1024 * 1024;
#endif
}
#endif


///////////////////////////////////////////////////////////////////////////////

/*  If we need to better match the request, we might examine the image and
     output dimensions, and determine if the downsampling jpeg provided is
     not sufficient. If so, we can recompute a modified sampleSize value to
     make up the difference.

     To skip this additional scaling, just set sampleSize = 1; below.
 */
static int recompute_sampleSize(int sampleSize,
                                const jpeg_decompress_struct& cinfo) {
    return sampleSize * cinfo.output_width / cinfo.image_width;
}

static bool valid_output_dimensions(const jpeg_decompress_struct& cinfo) {
    /* These are initialized to 0, so if they have non-zero values, we assume
       they are "valid" (i.e. have been computed by libjpeg)
     */
    return cinfo.output_width != 0 && cinfo.output_height != 0;
}

static bool skip_src_rows(jpeg_decompress_struct* cinfo, void* buffer,
                          int count) {
    for (int i = 0; i < count; i++) {
        JSAMPLE* rowptr = (JSAMPLE*)buffer;
        int row_count = jpeg_read_scanlines(cinfo, &rowptr, 1);
        if (row_count != 1) {
            return false;
        }
    }
    return true;
}

static bool skip_src_rows_tile(jpeg_decompress_struct* cinfo,
                          huffman_index *index, void* buffer,
                          int count) {
    for (int i = 0; i < count; i++) {
        JSAMPLE* rowptr = (JSAMPLE*)buffer;
        int row_count = jpeg_read_tile_scanline(cinfo, index, &rowptr);
        if (row_count != 1) {
            return false;
        }
    }
    return true;
}

// This guy exists just to aid in debugging, as it allows debuggers to just
// set a break-point in one place to see all error exists.
static bool return_false(const jpeg_decompress_struct& cinfo,
                         const SkBitmap& bm, const char msg[]) {
#ifdef SK_DEBUG
    SkDebugf("libjpeg error %d <%s> from %s [%d %d]", cinfo.err->msg_code,
             cinfo.err->jpeg_message_table[cinfo.err->msg_code], msg,
             bm.width(), bm.height());
#endif
    return false;   // must always return false
}

bool SkJPEGImageDecoder::onDecode(SkStream* stream, SkBitmap* bm, Mode mode) {
#ifdef TIME_DECODE
    AutoTimeMillis atm("JPEG Decode");
#endif

    SkAutoMalloc  srcStorage;
    JPEGAutoClean autoClean;

    jpeg_decompress_struct  cinfo;
    skjpeg_error_mgr        sk_err;
    skjpeg_source_mgr       sk_stream(stream, this, false);

    cinfo.err = jpeg_std_error(&sk_err);
    sk_err.error_exit = skjpeg_error_exit;

    // All objects need to be instantiated before this setjmp call so that
    // they will be cleaned up properly if an error occurs.
    if (setjmp(sk_err.fJmpBuf)) {
        return return_false(cinfo, *bm, "setjmp");
    }

    jpeg_create_decompress(&cinfo);
    autoClean.set(&cinfo);

#ifdef ANDROID
    overwrite_mem_buffer_size(&cinfo);
#endif

    //jpeg_stdio_src(&cinfo, file);
    cinfo.src = &sk_stream;

    int status = jpeg_read_header(&cinfo, true);
    if (status != JPEG_HEADER_OK) {
        return return_false(cinfo, *bm, "read_header");
    }

    /*  Try to fulfill the requested sampleSize. Since jpeg can do it (when it
        can) much faster that we, just use their num/denom api to approximate
        the size.
    */
    int sampleSize = this->getSampleSize();

    if (this->getPreferQualityOverSpeed()) {
        cinfo.dct_method = JDCT_ISLOW;
    } else {
        cinfo.dct_method = JDCT_IFAST;
    }

    cinfo.scale_num = 1;
    cinfo.scale_denom = sampleSize;

    /* this gives about 30% performance improvement. In theory it may
       reduce the visual quality, in practice I'm not seeing a difference
     */
    cinfo.do_fancy_upsampling = 0;

    /* this gives another few percents */
    cinfo.do_block_smoothing = 0;

    /* default format is RGB */
    cinfo.out_color_space = JCS_RGB;

    SkBitmap::Config config = this->getPrefConfig(k32Bit_SrcDepth, false);
    // only these make sense for jpegs
    if (config != SkBitmap::kARGB_8888_Config &&
        config != SkBitmap::kARGB_4444_Config &&
        config != SkBitmap::kRGB_565_Config) {
        config = SkBitmap::kARGB_8888_Config;
    }

#ifdef ANDROID_RGB
    cinfo.dither_mode = JDITHER_NONE;
    if (config == SkBitmap::kARGB_8888_Config) {
        cinfo.out_color_space = JCS_RGBA_8888;
    } else if (config == SkBitmap::kRGB_565_Config) {
        cinfo.out_color_space = JCS_RGB_565;
        if (this->getDitherImage()) {
            cinfo.dither_mode = JDITHER_ORDERED;
        }
    }
#endif

    if (sampleSize == 1 && mode == SkImageDecoder::kDecodeBounds_Mode) {
        bm->setConfig(config, cinfo.image_width, cinfo.image_height);
        bm->setIsOpaque(true);
        return true;
    }

    /*  image_width and image_height are the original dimensions, available
        after jpeg_read_header(). To see the scaled dimensions, we have to call
        jpeg_start_decompress(), and then read output_width and output_height.
    */
    if (!jpeg_start_decompress(&cinfo)) {
        /*  If we failed here, we may still have enough information to return
            to the caller if they just wanted (subsampled bounds). If sampleSize
            was 1, then we would have already returned. Thus we just check if
            we're in kDecodeBounds_Mode, and that we have valid output sizes.

            One reason to fail here is that we have insufficient stream data
            to complete the setup. However, output dimensions seem to get
            computed very early, which is why this special check can pay off.
         */
        if (SkImageDecoder::kDecodeBounds_Mode == mode &&
                valid_output_dimensions(cinfo)) {
            SkScaledBitmapSampler smpl(cinfo.output_width, cinfo.output_height,
                                       recompute_sampleSize(sampleSize, cinfo));
            bm->setConfig(config, smpl.scaledWidth(), smpl.scaledHeight());
            bm->setIsOpaque(true);
            return true;
        } else {
            return return_false(cinfo, *bm, "start_decompress");
        }
    }
    sampleSize = recompute_sampleSize(sampleSize, cinfo);

    // should we allow the Chooser (if present) to pick a config for us???
    if (!this->chooseFromOneChoice(config, cinfo.output_width,
                                   cinfo.output_height)) {
        return return_false(cinfo, *bm, "chooseFromOneChoice");
    }

#ifdef ANDROID_RGB
    /* short-circuit the SkScaledBitmapSampler when possible, as this gives
       a significant performance boost.
    */
    if (sampleSize == 1 &&
        ((config == SkBitmap::kARGB_8888_Config && 
                cinfo.out_color_space == JCS_RGBA_8888) ||
        (config == SkBitmap::kRGB_565_Config && 
                cinfo.out_color_space == JCS_RGB_565)))
    {
        bm->lockPixels();
        JSAMPLE* rowptr = (JSAMPLE*)bm->getPixels();
        bm->unlockPixels();
        bool reuseBitmap = (rowptr != NULL);
        if (reuseBitmap && ((int) cinfo.output_width != bm->width() ||
                (int) cinfo.output_height != bm->height())) {
            // Dimensions must match
            return false;
        }

        if (!reuseBitmap) {
            bm->setConfig(config, cinfo.output_width, cinfo.output_height);
            bm->setIsOpaque(true);
            if (SkImageDecoder::kDecodeBounds_Mode == mode) {
                return true;
            }
            if (!this->allocPixelRef(bm, NULL)) {
                return return_false(cinfo, *bm, "allocPixelRef");
            }
        } else if (SkImageDecoder::kDecodeBounds_Mode == mode) {
            return true;
        }
        SkAutoLockPixels alp(*bm);
        rowptr = (JSAMPLE*)bm->getPixels();
        INT32 const bpr =  bm->rowBytes();
        
        while (cinfo.output_scanline < cinfo.output_height) {
            int row_count = jpeg_read_scanlines(&cinfo, &rowptr, 1);
            // if row_count == 0, then we didn't get a scanline, so abort.
            // if we supported partial images, we might return true in this case
            if (0 == row_count) {
                return return_false(cinfo, *bm, "read_scanlines");
            }
            if (this->shouldCancelDecode()) {
                return return_false(cinfo, *bm, "shouldCancelDecode");
            }
            rowptr += bpr;
        }
        if (reuseBitmap) {
            bm->notifyPixelsChanged();
        }
        jpeg_finish_decompress(&cinfo);
        return true;
    }
#endif
    
    // check for supported formats
    SkScaledBitmapSampler::SrcConfig sc;
    if (3 == cinfo.out_color_components && JCS_RGB == cinfo.out_color_space) {
        sc = SkScaledBitmapSampler::kRGB;
#ifdef ANDROID_RGB
    } else if (JCS_RGBA_8888 == cinfo.out_color_space) {
        sc = SkScaledBitmapSampler::kRGBX;
    } else if (JCS_RGB_565 == cinfo.out_color_space) {
        sc = SkScaledBitmapSampler::kRGB_565;
#endif
    } else if (1 == cinfo.out_color_components &&
               JCS_GRAYSCALE == cinfo.out_color_space) {
        sc = SkScaledBitmapSampler::kGray;
    } else {
        return return_false(cinfo, *bm, "jpeg colorspace");
    }

    SkScaledBitmapSampler sampler(cinfo.output_width, cinfo.output_height,
                                  sampleSize);

    bm->lockPixels();
    JSAMPLE* rowptr = (JSAMPLE*)bm->getPixels();
    bool reuseBitmap = (rowptr != NULL);
    bm->unlockPixels();
    if (reuseBitmap && (sampler.scaledWidth() != bm->width() ||
            sampler.scaledHeight() != bm->height())) {
        // Dimensions must match
        return false;
    }

    if (!reuseBitmap) {
        bm->setConfig(config, sampler.scaledWidth(), sampler.scaledHeight());
        // jpegs are always opaque (i.e. have no per-pixel alpha)
        bm->setIsOpaque(true);

        if (SkImageDecoder::kDecodeBounds_Mode == mode) {
            return true;
        }
        if (!this->allocPixelRef(bm, NULL)) {
            return return_false(cinfo, *bm, "allocPixelRef");
        }
    } else if (SkImageDecoder::kDecodeBounds_Mode == mode) {
        return true;
    }

    SkAutoLockPixels alp(*bm);                          
    if (!sampler.begin(bm, sc, this->getDitherImage())) {
        return return_false(cinfo, *bm, "sampler.begin");
    }

    uint8_t* srcRow = (uint8_t*)srcStorage.alloc(cinfo.output_width * 4);

    //  Possibly skip initial rows [sampler.srcY0]
    if (!skip_src_rows(&cinfo, srcRow, sampler.srcY0())) {
        return return_false(cinfo, *bm, "skip rows");
    }

    // now loop through scanlines until y == bm->height() - 1
    for (int y = 0;; y++) {
        JSAMPLE* rowptr = (JSAMPLE*)srcRow;
        int row_count = jpeg_read_scanlines(&cinfo, &rowptr, 1);
        if (0 == row_count) {
            return return_false(cinfo, *bm, "read_scanlines");
        }
        if (this->shouldCancelDecode()) {
            return return_false(cinfo, *bm, "shouldCancelDecode");
        }
        
        sampler.next(srcRow);
        if (bm->height() - 1 == y) {
            // we're done
            break;
        }

        if (!skip_src_rows(&cinfo, srcRow, sampler.srcDY() - 1)) {
            return return_false(cinfo, *bm, "skip rows");
        }
    }

    // we formally skip the rest, so we don't get a complaint from libjpeg
    if (!skip_src_rows(&cinfo, srcRow,
                       cinfo.output_height - cinfo.output_scanline)) {
        return return_false(cinfo, *bm, "skip rows");
    }
    if (reuseBitmap) {
        bm->notifyPixelsChanged();
    }
    jpeg_finish_decompress(&cinfo);

//    SkDebugf("------------------- bm2 size %d [%d %d] %d\n", bm->getSize(), bm->width(), bm->height(), bm->config());
    return true;
}

bool SkJPEGImageDecoder::onBuildTileIndex(SkStream* stream,
                                        int *width, int *height) {
    SkAutoMalloc  srcStorage;
    SkJPEGImageIndex *index = new SkJPEGImageIndex;

    jpeg_decompress_struct  *cinfo = (jpeg_decompress_struct*)
                                        malloc(sizeof(jpeg_decompress_struct));
    skjpeg_error_mgr        sk_err;
    skjpeg_source_mgr       *sk_stream =
        new skjpeg_source_mgr(stream, this, true);
    if (cinfo == NULL || sk_stream == NULL) {
        return false;
    }

    cinfo->err = jpeg_std_error(&sk_err);
    sk_err.error_exit = skjpeg_error_exit;

    // All objects need to be instantiated before this setjmp call so that
    // they will be cleaned up properly if an error occurs.
    if (setjmp(sk_err.fJmpBuf)) {
        return false;
    }

    jpeg_create_decompress(cinfo);
    cinfo->do_fancy_upsampling = 0;
    cinfo->do_block_smoothing = 0;

#ifdef ANDROID
    overwrite_mem_buffer_size(cinfo);
#endif

    cinfo->src = sk_stream;
    int status = jpeg_read_header(cinfo, true);
    if (status != JPEG_HEADER_OK) {
        return false;
    }
    index->index = (huffman_index*)malloc(sizeof(huffman_index));
    jpeg_create_huffman_index(cinfo, index->index);

    cinfo->scale_num = 1;
    cinfo->scale_denom = 1;
    if (!jpeg_build_huffman_index(cinfo, index->index)) {
        return false;
    }
    if (fReporter)
        fReporter->reportMemory(index->index->mem_used);
    jpeg_destroy_decompress(cinfo);


    // Init decoder to image decode mode
    jpeg_create_decompress(cinfo);

#ifdef ANDROID
    overwrite_mem_buffer_size(cinfo);
#endif

    cinfo->src = sk_stream;
    status = jpeg_read_header(cinfo,true);
    if (status != JPEG_HEADER_OK) {
        return false;
    }
    cinfo->out_color_space = JCS_RGBA_8888;
    cinfo->do_fancy_upsampling = 0;
    cinfo->do_block_smoothing = 0;
    //jpeg_start_decompress(cinfo);
    jpeg_start_tile_decompress(cinfo);

    cinfo->scale_num = 1;
    index->cinfo = cinfo;
    *height = cinfo->output_height;
    *width = cinfo->output_width;

    this->index = index;
    return true;
}

bool SkJPEGImageDecoder::onDecodeRegion(SkBitmap* bm, SkIRect region) {
    if (index == NULL) {
        return false;
    }
    int startX = region.fLeft;
    int startY = region.fTop;
    int width = region.width();
    int height = region.height();
    jpeg_decompress_struct *cinfo = index->cinfo;
    SkAutoMalloc  srcStorage;
    skjpeg_error_mgr        sk_err;
    cinfo->err = jpeg_std_error(&sk_err);
    sk_err.error_exit = skjpeg_error_exit;
    if (setjmp(sk_err.fJmpBuf)) {
        return false;
    }
    int requestedSampleSize = this->getSampleSize();
    cinfo->scale_denom = requestedSampleSize;

    if (this->getPreferQualityOverSpeed()) {
        cinfo->dct_method = JDCT_ISLOW;
    } else {
        cinfo->dct_method = JDCT_IFAST;
    }

    SkBitmap::Config config = this->getPrefConfig(k32Bit_SrcDepth, false);
    if (config != SkBitmap::kARGB_8888_Config &&
        config != SkBitmap::kARGB_4444_Config &&
        config != SkBitmap::kRGB_565_Config) {
        config = SkBitmap::kARGB_8888_Config;
    }

    /* default format is RGB */
    cinfo->out_color_space = JCS_RGB;

#ifdef ANDROID_RGB
    cinfo->dither_mode = JDITHER_NONE;
    if (config == SkBitmap::kARGB_8888_Config) {
        cinfo->out_color_space = JCS_RGBA_8888;
    } else if (config == SkBitmap::kRGB_565_Config) {
        cinfo->out_color_space = JCS_RGB_565;
        if (this->getDitherImage()) {
            cinfo->dither_mode = JDITHER_ORDERED;
        }
    }
#endif

    int oriStartX = startX;
    int oriStartY = startY;
    int oriWidth = width;
    int oriHeight = height;
    jpeg_init_read_tile_scanline(cinfo, index->index,
                                 &startX, &startY, &width, &height);
    int skiaSampleSize = recompute_sampleSize(requestedSampleSize, *cinfo);
    int actualSampleSize = skiaSampleSize * (DCTSIZE / cinfo->min_DCT_scaled_size);

    SkBitmap *bitmap = new SkBitmap;
    SkAutoTDelete<SkBitmap> adb(bitmap);

#ifdef ANDROID_RGB
    /* short-circuit the SkScaledBitmapSampler when possible, as this gives
       a significant performance boost.
    */
    if (skiaSampleSize == 1 &&
        ((config == SkBitmap::kARGB_8888_Config &&
                cinfo->out_color_space == JCS_RGBA_8888) ||
        (config == SkBitmap::kRGB_565_Config &&
                cinfo->out_color_space == JCS_RGB_565)))
    {
        bitmap->setConfig(config, cinfo->output_width, height);
        bitmap->setIsOpaque(true);
        if (!this->allocPixelRef(bitmap, NULL)) {
            return return_false(*cinfo, *bitmap, "allocPixelRef");
        }
        SkAutoLockPixels alp(*bitmap);
        JSAMPLE* rowptr = (JSAMPLE*)bitmap->getPixels();
        INT32 const bpr = bitmap->rowBytes();
        int row_total_count = 0;

        while (row_total_count < height) {
            int row_count = jpeg_read_tile_scanline(cinfo,
                    index->index, &rowptr);
            // if row_count == 0, then we didn't get a scanline, so abort.
            // if we supported partial images, we might return true in this case
            if (0 == row_count) {
                return return_false(*cinfo, *bitmap, "read_scanlines");
            }
            if (this->shouldCancelDecode()) {
                return return_false(*cinfo, *bitmap, "shouldCancelDecode");
            }
            row_total_count += row_count;
            rowptr += bpr;
        }
        cropBitmap(bm, bitmap, actualSampleSize, oriStartX, oriStartY,
                   oriWidth, oriHeight, startX, startY);
        return true;
    }
#endif
    // check for supported formats
    SkScaledBitmapSampler::SrcConfig sc;
    if (3 == cinfo->out_color_components && JCS_RGB == cinfo->out_color_space) {
        sc = SkScaledBitmapSampler::kRGB;
#ifdef ANDROID_RGB
    } else if (JCS_RGBA_8888 == cinfo->out_color_space) {
        sc = SkScaledBitmapSampler::kRGBX;
    } else if (JCS_RGB_565 == cinfo->out_color_space) {
        sc = SkScaledBitmapSampler::kRGB_565;
#endif
    } else if (1 == cinfo->out_color_components &&
               JCS_GRAYSCALE == cinfo->out_color_space) {
        sc = SkScaledBitmapSampler::kGray;
    } else {
        return return_false(*cinfo, *bm, "jpeg colorspace");
    }

    SkScaledBitmapSampler sampler(width, height, skiaSampleSize);

    bitmap->setConfig(config, sampler.scaledWidth(), sampler.scaledHeight());
    bitmap->setIsOpaque(true);

    if (!this->allocPixelRef(bitmap, NULL)) {
        return return_false(*cinfo, *bitmap, "allocPixelRef");
    }

    SkAutoLockPixels alp(*bitmap);
    if (!sampler.begin(bitmap, sc, this->getDitherImage())) {
        return return_false(*cinfo, *bitmap, "sampler.begin");
    }

    uint8_t* srcRow = (uint8_t*)srcStorage.alloc(width * 4);

    //  Possibly skip initial rows [sampler.srcY0]
    if (!skip_src_rows_tile(cinfo, index->index, srcRow, sampler.srcY0())) {
        return return_false(*cinfo, *bitmap, "skip rows");
    }

    // now loop through scanlines until y == bitmap->height() - 1
    for (int y = 0;; y++) {
        JSAMPLE* rowptr = (JSAMPLE*)srcRow;
        int row_count = jpeg_read_tile_scanline(cinfo, index->index, &rowptr);
        if (0 == row_count) {
            return return_false(*cinfo, *bitmap, "read_scanlines");
        }
        if (this->shouldCancelDecode()) {
            return return_false(*cinfo, *bitmap, "shouldCancelDecode");
        }

        sampler.next(srcRow);
        if (bitmap->height() - 1 == y) {
            // we're done
            break;
        }

        if (!skip_src_rows_tile(cinfo, index->index, srcRow,
                                sampler.srcDY() - 1)) {
            return return_false(*cinfo, *bitmap, "skip rows");
        }
    }
    cropBitmap(bm, bitmap, actualSampleSize, oriStartX, oriStartY,
               oriWidth, oriHeight, startX, startY);
    return true;
}

///////////////////////////////////////////////////////////////////////////////

#include "SkColorPriv.h"

// taken from jcolor.c in libjpeg
#if 0   // 16bit - precise but slow
    #define CYR     19595   // 0.299
    #define CYG     38470   // 0.587
    #define CYB      7471   // 0.114

    #define CUR    -11059   // -0.16874
    #define CUG    -21709   // -0.33126
    #define CUB     32768   // 0.5

    #define CVR     32768   // 0.5
    #define CVG    -27439   // -0.41869
    #define CVB     -5329   // -0.08131

    #define CSHIFT  16
#else      // 8bit - fast, slightly less precise
    #define CYR     77    // 0.299
    #define CYG     150    // 0.587
    #define CYB      29    // 0.114

    #define CUR     -43    // -0.16874
    #define CUG    -85    // -0.33126
    #define CUB     128    // 0.5

    #define CVR      128   // 0.5
    #define CVG     -107   // -0.41869
    #define CVB      -21   // -0.08131

    #define CSHIFT  8
#endif

static void rgb2yuv_32(uint8_t dst[], SkPMColor c) {
    int r = SkGetPackedR32(c);
    int g = SkGetPackedG32(c);
    int b = SkGetPackedB32(c);

    int  y = ( CYR*r + CYG*g + CYB*b ) >> CSHIFT;
    int  u = ( CUR*r + CUG*g + CUB*b ) >> CSHIFT;
    int  v = ( CVR*r + CVG*g + CVB*b ) >> CSHIFT;

    dst[0] = SkToU8(y);
    dst[1] = SkToU8(u + 128);
    dst[2] = SkToU8(v + 128);
}

static void rgb2yuv_4444(uint8_t dst[], U16CPU c) {
    int r = SkGetPackedR4444(c);
    int g = SkGetPackedG4444(c);
    int b = SkGetPackedB4444(c);

    int  y = ( CYR*r + CYG*g + CYB*b ) >> (CSHIFT - 4);
    int  u = ( CUR*r + CUG*g + CUB*b ) >> (CSHIFT - 4);
    int  v = ( CVR*r + CVG*g + CVB*b ) >> (CSHIFT - 4);

    dst[0] = SkToU8(y);
    dst[1] = SkToU8(u + 128);
    dst[2] = SkToU8(v + 128);
}

static void rgb2yuv_16(uint8_t dst[], U16CPU c) {
    int r = SkGetPackedR16(c);
    int g = SkGetPackedG16(c);
    int b = SkGetPackedB16(c);

    int  y = ( 2*CYR*r + CYG*g + 2*CYB*b ) >> (CSHIFT - 2);
    int  u = ( 2*CUR*r + CUG*g + 2*CUB*b ) >> (CSHIFT - 2);
    int  v = ( 2*CVR*r + CVG*g + 2*CVB*b ) >> (CSHIFT - 2);

    dst[0] = SkToU8(y);
    dst[1] = SkToU8(u + 128);
    dst[2] = SkToU8(v + 128);
}

///////////////////////////////////////////////////////////////////////////////

typedef void (*WriteScanline)(uint8_t* SK_RESTRICT dst,
                              const void* SK_RESTRICT src, int width,
                              const SkPMColor* SK_RESTRICT ctable);

static void Write_32_YUV(uint8_t* SK_RESTRICT dst,
                         const void* SK_RESTRICT srcRow, int width,
                         const SkPMColor*) {
    const uint32_t* SK_RESTRICT src = (const uint32_t*)srcRow;
    while (--width >= 0) {
#ifdef WE_CONVERT_TO_YUV
        rgb2yuv_32(dst, *src++);
#else
        uint32_t c = *src++;
        dst[0] = SkGetPackedR32(c);
        dst[1] = SkGetPackedG32(c);
        dst[2] = SkGetPackedB32(c);
#endif
        dst += 3;
    }
}

static void Write_4444_YUV(uint8_t* SK_RESTRICT dst,
                           const void* SK_RESTRICT srcRow, int width,
                           const SkPMColor*) {
    const SkPMColor16* SK_RESTRICT src = (const SkPMColor16*)srcRow;
    while (--width >= 0) {
#ifdef WE_CONVERT_TO_YUV
        rgb2yuv_4444(dst, *src++);
#else
        SkPMColor16 c = *src++;
        dst[0] = SkPacked4444ToR32(c);
        dst[1] = SkPacked4444ToG32(c);
        dst[2] = SkPacked4444ToB32(c);
#endif
        dst += 3;
    }
}

static void Write_16_YUV(uint8_t* SK_RESTRICT dst,
                         const void* SK_RESTRICT srcRow, int width,
                         const SkPMColor*) {
    const uint16_t* SK_RESTRICT src = (const uint16_t*)srcRow;
    while (--width >= 0) {
#ifdef WE_CONVERT_TO_YUV
        rgb2yuv_16(dst, *src++);
#else
        uint16_t c = *src++;
        dst[0] = SkPacked16ToR32(c);
        dst[1] = SkPacked16ToG32(c);
        dst[2] = SkPacked16ToB32(c);
#endif
        dst += 3;
    }
}

static void Write_Index_YUV(uint8_t* SK_RESTRICT dst,
                            const void* SK_RESTRICT srcRow, int width,
                            const SkPMColor* SK_RESTRICT ctable) {
    const uint8_t* SK_RESTRICT src = (const uint8_t*)srcRow;
    while (--width >= 0) {
#ifdef WE_CONVERT_TO_YUV
        rgb2yuv_32(dst, ctable[*src++]);
#else
        uint32_t c = ctable[*src++];
        dst[0] = SkGetPackedR32(c);
        dst[1] = SkGetPackedG32(c);
        dst[2] = SkGetPackedB32(c);
#endif
        dst += 3;
    }
}

static WriteScanline ChooseWriter(const SkBitmap& bm) {
    switch (bm.config()) {
        case SkBitmap::kARGB_8888_Config:
            return Write_32_YUV;
        case SkBitmap::kRGB_565_Config:
            return Write_16_YUV;
        case SkBitmap::kARGB_4444_Config:
            return Write_4444_YUV;
        case SkBitmap::kIndex8_Config:
            return Write_Index_YUV;
        default:
            return NULL;
    }
}

class SkJPEGImageEncoder : public SkImageEncoder {
protected:
    virtual bool onEncode(SkWStream* stream, const SkBitmap& bm, int quality) {
#ifdef TIME_ENCODE
        AutoTimeMillis atm("JPEG Encode");
#endif

        const WriteScanline writer = ChooseWriter(bm);
        if (NULL == writer) {
            return false;
        }

        SkAutoLockPixels alp(bm);
        if (NULL == bm.getPixels()) {
            return false;
        }

        jpeg_compress_struct    cinfo;
        skjpeg_error_mgr        sk_err;
        skjpeg_destination_mgr  sk_wstream(stream);

        // allocate these before set call setjmp
        SkAutoMalloc    oneRow;
        SkAutoLockColors ctLocker;

        cinfo.err = jpeg_std_error(&sk_err);
        sk_err.error_exit = skjpeg_error_exit;
        if (setjmp(sk_err.fJmpBuf)) {
            return false;
        }
        jpeg_create_compress(&cinfo);

        cinfo.dest = &sk_wstream;
        cinfo.image_width = bm.width();
        cinfo.image_height = bm.height();
        cinfo.input_components = 3;
#ifdef WE_CONVERT_TO_YUV
        cinfo.in_color_space = JCS_YCbCr;
#else
        cinfo.in_color_space = JCS_RGB;
#endif
        cinfo.input_gamma = 1;

        jpeg_set_defaults(&cinfo);
        jpeg_set_quality(&cinfo, quality, TRUE /* limit to baseline-JPEG values */);
        cinfo.dct_method = JDCT_IFAST;

        jpeg_start_compress(&cinfo, TRUE);

        const int       width = bm.width();
        uint8_t*        oneRowP = (uint8_t*)oneRow.alloc(width * 3);

        const SkPMColor* colors = ctLocker.lockColors(bm);
        const void*      srcRow = bm.getPixels();

        while (cinfo.next_scanline < cinfo.image_height) {
            JSAMPROW row_pointer[1];    /* pointer to JSAMPLE row[s] */

            writer(oneRowP, srcRow, width, colors);
            row_pointer[0] = oneRowP;
            (void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
            srcRow = (const void*)((const char*)srcRow + bm.rowBytes());
        }

        jpeg_finish_compress(&cinfo);
        jpeg_destroy_compress(&cinfo);

        return true;
    }
};

///////////////////////////////////////////////////////////////////////////////

#include "SkTRegistry.h"

static SkImageDecoder* DFactory(SkStream* stream) {
    static const char gHeader[] = { 0xFF, 0xD8, 0xFF };
    static const size_t HEADER_SIZE = sizeof(gHeader);

    char buffer[HEADER_SIZE];
    size_t len = stream->read(buffer, HEADER_SIZE);

    if (len != HEADER_SIZE) {
        return NULL;   // can't read enough
    }
    if (memcmp(buffer, gHeader, HEADER_SIZE)) {
        return NULL;
    }
    return SkNEW(SkJPEGImageDecoder);
}

static SkImageEncoder* EFactory(SkImageEncoder::Type t) {
    return (SkImageEncoder::kJPEG_Type == t) ? SkNEW(SkJPEGImageEncoder) : NULL;
}

static SkTRegistry<SkImageDecoder*, SkStream*> gDReg(DFactory);
static SkTRegistry<SkImageEncoder*, SkImageEncoder::Type> gEReg(EFactory);
