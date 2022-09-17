/*
 * Copyright 2022 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef skgpu_graphite_TextureUtils_DEFINED
#define skgpu_graphite_TextureUtils_DEFINED

#include "include/core/SkImage.h"
#include "src/gpu/graphite/TextureProxyView.h"

#include <functional>

class SkBitmap;
enum SkColorType : int;
struct SkImageInfo;

namespace skgpu::graphite {

class Context;

// Create TextureProxyView and SkColorType pair using pixel data in SkBitmap,
// adding any necessary copy commands to Recorder
std::tuple<TextureProxyView, SkColorType> MakeBitmapProxyView(Recorder*,
                                                              const SkBitmap&,
                                                              sk_sp<SkMipmap>,
                                                              Mipmapped,
                                                              SkBudgeted);

sk_sp<SkImage> MakeFromBitmap(Recorder*,
                              const SkColorInfo&,
                              const SkBitmap&,
                              sk_sp<SkMipmap>,
                              SkBudgeted,
                              SkImage::RequiredImageProperties);

using FlushPendingWorkCallback = std::function<void()>;

bool ReadPixelsHelper(FlushPendingWorkCallback&&,
                      Context*,
                      Recorder*,
                      TextureProxy* srcProxy,
                      const SkImageInfo& dstInfo,
                      void* dstPixels,
                      size_t dstRowBytes,
                      int srcX,
                      int srcY);
} // namespace skgpu::graphite

#endif // skgpu_graphite_TextureUtils_DEFINED
