/*
 * Copyright 2021 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef skgpu_graphite_ContextUtils_DEFINED
#define skgpu_graphite_ContextUtils_DEFINED

#include "src/core/SkPaintParamsKey.h"
#include "src/gpu/graphite/PipelineDataCache.h"

class SkM44;
class SkPaintParamsKeyBuilder;
class SkPipelineDataGatherer;
class SkRuntimeEffectDictionary;
class SkUniquePaintParamsID;

namespace skgpu::graphite {

class DrawParams;
class GraphicsPipelineDesc;
class PaintParams;
class Recorder;
class RenderStep;

std::tuple<SkUniquePaintParamsID, UniformDataCache::Index, TextureDataCache::Index>
ExtractPaintData(Recorder*,
                 SkPipelineDataGatherer* gatherer,
                 SkPaintParamsKeyBuilder* builder,
                 const SkM44& dev2local,
                 const PaintParams&);

std::tuple<UniformDataCache::Index, TextureDataCache::Index>
ExtractRenderStepData(UniformDataCache* geometryUniformDataCache,
                      TextureDataCache* textureDataCache,
                      SkPipelineDataGatherer* gatherer,
                      const RenderStep* step,
                      const DrawParams& params);

std::string GetSkSLVS(const GraphicsPipelineDesc& desc, bool defineLocalCoordsVarying);

std::string GetSkSLFS(SkShaderCodeDictionary* dict,
                      SkRuntimeEffectDictionary* rteDict,
                      const GraphicsPipelineDesc& desc,
                      BlendInfo* blendInfo,
                      bool* requiresLocalCoordsVarying);

std::string EmitPaintParamsUniforms(int bufferID,
                                    const char* name,
                                    const std::vector<SkPaintParamsKey::BlockReader>&,
                                    bool needsDev2Local);
std::string EmitRenderStepUniforms(int bufferID, const char* name,
                                   SkSpan<const SkUniform> uniforms);
std::string EmitTexturesAndSamplers(const std::vector<SkPaintParamsKey::BlockReader>&,
                                    int* binding);
std::string EmitVaryings(const RenderStep*, const char* direction, bool emitLocalCoordsVarying);

} // namespace skgpu::graphite

#endif // skgpu_graphite_ContextUtils_DEFINED
