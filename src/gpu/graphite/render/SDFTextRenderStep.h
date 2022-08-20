/*
 * Copyright 2022 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef skgpu_graphite_render_SDFTextRenderStep_DEFINED
#define skgpu_graphite_render_SDFTextRenderStep_DEFINED

#include "src/gpu/graphite/Renderer.h"

namespace skgpu { enum class MaskFormat; }

namespace skgpu::graphite {

class SDFTextRenderStep final : public RenderStep {
public:
    SDFTextRenderStep(bool isA8);

    ~SDFTextRenderStep() override;

    const char* vertexSkSL() const override;
    std::string texturesAndSamplersSkSL(int startBinding) const override;
    const char* fragmentCoverageSkSL() const override;

    void writeVertices(DrawWriter*, const DrawParams&, int ssboIndex) const override;
    void writeUniformsAndTextures(const DrawParams&, SkPipelineDataGatherer*) const override;
};

}  // namespace skgpu::graphite

#endif // skgpu_graphite_render_SDFTextRenderStep_DEFINED
