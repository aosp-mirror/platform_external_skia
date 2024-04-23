/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef skgpu_graphite_Image_YUVA_Graphite_DEFINED
#define skgpu_graphite_Image_YUVA_Graphite_DEFINED

#include "src/gpu/graphite/Image_Base_Graphite.h"

#include "src/gpu/graphite/YUVATextureProxies.h"

namespace skgpu::graphite {

class Recorder;

class Image_YUVA final : public Image_Base {
public:
    Image_YUVA(YUVATextureProxies proxies, sk_sp<SkColorSpace>);

    ~Image_YUVA() override;

    // Create an Image_YUVA by interpreting the multiple 'planes' using 'yuvaInfo'. If the info
    // or provided plane proxies do not produce a valid mulitplane image, null is returned.
    static sk_sp<Image_YUVA> Make(const Caps* caps,
                                  const SkYUVAInfo& yuvaInfo,
                                  SkSpan<TextureProxyView> planes,
                                  sk_sp<SkColorSpace> imageColorSpace);

    // Wraps the Graphite-backed Image planes into a YUV[A] image. The returned image shares
    // textures as well as any links to Devices that might modify those textures.
    static sk_sp<Image_YUVA> WrapImages(const Caps* caps,
                                        const SkYUVAInfo& yuvaInfo,
                                        SkSpan<const sk_sp<SkImage>> images,
                                        sk_sp<SkColorSpace> imageColorSpace);

    SkImage_Base::Type type() const override { return SkImage_Base::Type::kGraphiteYUVA; }

    size_t textureSize() const override;

    bool onHasMipmaps() const override { return fYUVAProxies.mipmapped() == Mipmapped::kYes; }

    bool onIsProtected() const override { return fYUVAProxies.isProtected() == Protected::kYes; }

    sk_sp<SkImage> onReinterpretColorSpace(sk_sp<SkColorSpace>) const override;

    const YUVATextureProxies& yuvaProxies() const {
        return fYUVAProxies;
    }

private:

    YUVATextureProxies fYUVAProxies;
};

} // namespace skgpu::graphite

#endif // skgpu_graphite_Image_YUVA_Graphite_DEFINED
