/*
 * Copyright 2021 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef skgpu_ContextUtils_DEFINED
#define skgpu_ContextUtils_DEFINED

#include "experimental/graphite/include/Context.h"
#include "include/core/SkBlendMode.h"
#include "include/core/SkSpan.h"
#include "include/core/SkTileMode.h"

enum class CodeSnippetID : uint8_t;
class SkShaderCodeDictionary;
class SkUniform;
class SkUniformBlock;
class SkUniquePaintParamsID;

namespace skgpu {

class PaintParams;

std::tuple<SkUniquePaintParamsID, std::unique_ptr<SkUniformBlock>> ExtractPaintData(
        SkShaderCodeDictionary*, const PaintParams&);

SkSpan<const SkUniform> GetUniforms(CodeSnippetID);

// TODO: Temporary way to get at SkSL snippet for handling the given shader type, which will be
// embedded in the fragment function's body. It has access to the vertex output via a "interpolated"
// variable, and must have a statement that writes to a float4 "out.color". Its uniforms (as defined
// by GetUniforms(type)) are available as a variable named "uniforms".
std::tuple<const char*, const char*> GetShaderSkSL(CodeSnippetID);

} // namespace skgpu

#endif // skgpu_ContextUtils_DEFINED
