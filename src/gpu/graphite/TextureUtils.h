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
#include <tuple>

class SkBitmap;
enum SkColorType : int;
struct SkImageInfo;

namespace skgpu::graphite {

class Context;

// Create TextureProxyView and SkColorType pair using pixel data in SkBitmap,
// adding any necessary copy commands to Recorder
std::tuple<TextureProxyView, SkColorType> MakeBitmapProxyView(
        Recorder*, const SkBitmap&, sk_sp<SkMipmap>, Mipmapped, skgpu::Budgeted);

sk_sp<SkImage> MakeFromBitmap(Recorder*,
                              const SkColorInfo&,
                              const SkBitmap&,
                              sk_sp<SkMipmap>,
                              skgpu::Budgeted,
                              SkImage::RequiredProperties);

size_t ComputeSize(SkISize dimensions, const TextureInfo&);

sk_sp<SkImage> RescaleImage(Recorder*,
                            const SkImage* srcImage,
                            SkIRect srcIRect,
                            const SkImageInfo& dstInfo,
                            SkImage::RescaleGamma rescaleGamma,
                            SkImage::RescaleMode rescaleMode);

bool GenerateMipmaps(Recorder*, sk_sp<TextureProxy>, const SkColorInfo&);

} // namespace skgpu::graphite

#endif // skgpu_graphite_TextureUtils_DEFINED
