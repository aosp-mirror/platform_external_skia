/*
 * Copyright 2012, The Android Open Source Project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "SkBitmap.h"
#include "SkCanvas.h"
#include "SkMatrix.h"
#include "SkPicture.h"
#include "SkRegion.h"
#include "SkStream.h"

#define EXPORT_FUNC extern "C" __attribute__ ((visibility ("default")))

EXPORT_FUNC int legacy_skia_create_picture(const void* pictureStream, int streamLength,
                                           void**legacyPicture, int* width, int* height) {
    SkMemoryStream stream(pictureStream, streamLength);
    SkPicture* picture = new SkPicture(&stream);
    *legacyPicture = picture;
    *width = picture->width();
    *height = picture->height();
    return stream.peek();
}

EXPORT_FUNC void legacy_skia_delete_picture(void* legacyPicture) {
  free(legacyPicture);
}

EXPORT_FUNC void legacy_skia_draw_picture(void* legacyPicture, void* matrixStorage,
                                          void* clipStorage, int bitmapWidth,
                                          int bitmapHeight, int bitmapConfig,
                                          int bitmapRowBytes, void* pixels) {
    SkMatrix matrix;
    matrix.unflatten(matrixStorage);

    SkRegion region;
    region.unflatten(clipStorage);

    SkBitmap bitmap;
    bitmap.setConfig((SkBitmap::Config)bitmapConfig, bitmapWidth, bitmapHeight, bitmapRowBytes);
    bitmap.setPixels(pixels);

    SkCanvas canvas(bitmap);
    canvas.setMatrix(matrix);
    canvas.setClipRegion(region);
    canvas.drawPicture(*(SkPicture*)legacyPicture);
}
