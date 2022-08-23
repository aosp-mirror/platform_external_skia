/*
 * Copyright 2022 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkShaderCodeDictionary.h"

#include "include/core/SkTileMode.h"
#include "include/effects/SkRuntimeEffect.h"
#include "include/private/SkOpts_spi.h"
#include "src/core/SkRuntimeEffectPriv.h"
#include "src/core/SkSLTypeShared.h"

#ifdef SK_GRAPHITE_ENABLED
#include "include/gpu/graphite/Context.h"
#include "include/private/SkSLString.h"
#include "src/core/SkRuntimeEffectDictionary.h"
#include "src/gpu/graphite/ContextUtils.h"
#include "src/gpu/graphite/Renderer.h"
#include "src/sksl/codegen/SkSLPipelineStageCodeGenerator.h"
#include "src/sksl/ir/SkSLVarDeclarations.h"
#endif

#ifdef SK_ENABLE_PRECOMPILE
#include "include/core/SkCombinationBuilder.h"
#endif

#include <new>

using DataPayloadField = SkPaintParamsKey::DataPayloadField;
using DataPayloadType = SkPaintParamsKey::DataPayloadType;

namespace {

#if defined(SK_GRAPHITE_ENABLED) && defined(SK_ENABLE_SKSL)
std::string get_mangled_name(const std::string& baseName, int manglingSuffix) {
    return baseName + "_" + std::to_string(manglingSuffix);
}
#endif

} // anonymous namespace


std::string SkShaderSnippet::getMangledUniformName(int uniformIdx, int mangleId) const {
    std::string result;
    result = fUniforms[uniformIdx].name() + std::string("_") + std::to_string(mangleId);
    return result;
}

std::string SkShaderSnippet::getMangledSamplerName(int samplerIdx, int mangleId) const {
    std::string result;
    result = fTexturesAndSamplers[samplerIdx].name() + std::string("_") + std::to_string(mangleId);
    return result;
}

#if defined(SK_GRAPHITE_ENABLED) && defined(SK_ENABLE_SKSL)

// Returns an expression to invoke this entry, passing along an updated pre-local matrix.
static std::string emit_expression_for_entry(const SkShaderInfo& shaderInfo,
                                             int entryIndex,
                                             const std::string& priorStageOutputName,
                                             const std::string& fragCoord,
                                             std::string preLocalMatrix) {
    const SkPaintParamsKey::BlockReader& reader = shaderInfo.blockReader(entryIndex);
    const SkShaderSnippet* entry = reader.entry();

    if (entry->needsLocalCoords()) {
        // The snippet requested local coordinates; the pre-local matrix must be its first uniform.
        SkASSERT(entry->fUniforms.size() >= 1);
        SkASSERT(entry->fUniforms.front().type() == SkSLType::kFloat4x4);

        // Multiply in this entry's pre-local coordinate matrix.
        preLocalMatrix += " * ";
        preLocalMatrix += entry->getMangledUniformName(0, entryIndex);
    }

    return entry->fExpressionGenerator(shaderInfo, entryIndex, reader, priorStageOutputName,
                                       fragCoord, preLocalMatrix);
}

// Emit the glue code needed to invoke a single static helper isolated within its own scope.
// Glue code will assign the resulting color into a variable `half4 outColor%d`, where the %d is
// filled in with 'entryIndex'.
static std::string emit_glue_code_for_entry(const SkShaderInfo& shaderInfo,
                                            int entryIndex,
                                            const std::string& priorStageOutputName,
                                            const std::string& fragCoord,
                                            const std::string& parentPreLocalName,
                                            std::string* funcBody) {
    const SkShaderSnippet* entry = shaderInfo.blockReader(entryIndex).entry();

    std::string expr = emit_expression_for_entry(shaderInfo,
                                                 entryIndex,
                                                 priorStageOutputName,
                                                 fragCoord,
                                                 parentPreLocalName);
    std::string outputVar = get_mangled_name("outColor", entryIndex);
    SkSL::String::appendf(funcBody,
                          "    // %s\n"
                          "    half4 %s = %s;\n",
                          entry->fName,
                          outputVar.c_str(),
                          expr.c_str());
    return outputVar;
}

static void emit_preamble_for_entry(const SkShaderInfo& shaderInfo,
                                    int* entryIndex,
                                    std::string* preamble) {
    const SkPaintParamsKey::BlockReader& reader = shaderInfo.blockReader(*entryIndex);

    [[maybe_unused]] int startingEntryIndex = *entryIndex;
    reader.entry()->fPreambleGenerator(shaderInfo, entryIndex, reader, preamble);

    // Preamble generators are responsible for increasing the entry index as entries are consumed.
    SkASSERT(*entryIndex > startingEntryIndex);
}

// The current, incomplete, model for shader construction is:
//   - Static code snippets (which can have an arbitrary signature) live in the Graphite
//     pre-compiled module, which is located at `src/sksl/sksl_graphite_frag.sksl`.
//   - Glue code is generated in a `main` method which calls these static code snippets.
//     The glue code is responsible for:
//            1) gathering the correct (mangled) uniforms
//            2) passing the uniforms and any other parameters to the helper method
//   - The result of the final code snippet is then copied into "sk_FragColor".
//   Note: each entry's 'fStaticFunctionName' field is expected to match the name of a function
//   in the Graphite pre-compiled module.
std::string SkShaderInfo::toSkSL(const skgpu::graphite::RenderStep* step,
                                 const bool defineLocalCoordsVarying) const {
    std::string preamble = "layout(location = 0, index = 0) out half4 sk_FragColor;\n";
    preamble += skgpu::graphite::EmitVaryings(step, "in", defineLocalCoordsVarying);

    // The uniforms are mangled by having their index in 'fEntries' as a suffix (i.e., "_%d")
    // TODO: replace hard-coded bufferIDs with the backend's step and paint uniform-buffer indices.
    // TODO: The use of these indices is Metal-specific. We should replace these functions with
    // API-independent ones.
    if (step->numUniforms() > 0) {
        preamble += skgpu::graphite::EmitRenderStepUniforms(/*bufferID=*/1, "Step",
                                                            step->uniforms());
    }
    preamble += skgpu::graphite::EmitPaintParamsUniforms(/*bufferID=*/2, "FS", fBlockReaders,
                                                         this->needsLocalCoords());
    int binding = 0;
    preamble += skgpu::graphite::EmitTexturesAndSamplers(fBlockReaders, &binding);
    if (step->hasTextures()) {
        preamble += step->texturesAndSamplersSkSL(binding);
    }

    // TODO: Remove all the use of dev2LocalUni and the preLocal matrices once all render steps
    // that require local coordinates emit them directly.
    std::string mainBody = SkSL::String::printf("void main() {\n"
                                                "    float4 coords = %s sk_FragCoord;\n",
                                                this->needsLocalCoords() ? "dev2LocalUni *" : "");

    // TODO: what is the correct initial color to feed in?
    std::string lastOutputVar = "initialColor";
    SkSL::String::appendf(&mainBody, "    half4 %s = half4(0);", lastOutputVar.c_str());
    if (this->needsLocalCoords()) {
        // Get the local coordinates varying into half4 format as expected by emit_glue_code.
        mainBody += "float4 outLocalCoords = float4(localCoordsVar, 0.0, 0.0);\n";
    }

    for (int entryIndex = 0; entryIndex < (int)fBlockReaders.size();) {
        // Emit shader main body code. This never alters the preamble or increases the entry index.
        // TODO - Once RenderSteps that require local coordinates emit them directly to the
        // localCoordsVar varying, "outLocalCoords" can be passed in here instead of "coords".
        lastOutputVar = emit_glue_code_for_entry(*this, entryIndex, lastOutputVar, "coords",
                                                 "float4x4(1.0)", &mainBody);

        // Emit preamble code. This iterates over all the children as well, and increases the entry
        // index as we go.
        emit_preamble_for_entry(*this, &entryIndex, &preamble);
    }

    if (step->emitsPrimitiveColor()) {
        mainBody += "half4 primitiveColor;";
        mainBody += step->fragmentColorSkSL();
        // TODO: Apply primitive blender
        // For now, just overwrite the prior color stored in lastOutputVar
        SkSL::String::appendf(&mainBody, "    %s = primitiveColor;\n", lastOutputVar.c_str());
    }
    if (step->emitsCoverage()) {
        mainBody += "half4 outputCoverage;";
        mainBody += step->fragmentCoverageSkSL();
        SkSL::String::appendf(&mainBody, "    sk_FragColor = %s*outputCoverage;\n",
                              lastOutputVar.c_str());
    } else {
        SkSL::String::appendf(&mainBody, "    sk_FragColor = %s;\n", lastOutputVar.c_str());
    }
    mainBody += "}\n";

    return preamble + "\n" + mainBody;
}
#endif

SkShaderCodeDictionary::Entry* SkShaderCodeDictionary::makeEntry(
        const SkPaintParamsKey& key
#ifdef SK_GRAPHITE_ENABLED
        , const skgpu::BlendInfo& blendInfo
#endif
        ) {
    uint8_t* newKeyData = fArena.makeArray<uint8_t>(key.sizeInBytes());
    memcpy(newKeyData, key.data(), key.sizeInBytes());

    SkSpan<const uint8_t> newKeyAsSpan = SkSpan(newKeyData, key.sizeInBytes());
#ifdef SK_GRAPHITE_ENABLED
    return fArena.make([&](void *ptr) { return new(ptr) Entry(newKeyAsSpan, blendInfo); });
#else
    return fArena.make([&](void *ptr) { return new(ptr) Entry(newKeyAsSpan); });
#endif
}

size_t SkShaderCodeDictionary::SkPaintParamsKeyPtr::Hash::operator()(SkPaintParamsKeyPtr p) const {
    return SkOpts::hash_fn(p.fKey->data(), p.fKey->sizeInBytes(), 0);
}

size_t SkShaderCodeDictionary::RuntimeEffectKey::Hash::operator()(RuntimeEffectKey k) const {
    return SkOpts::hash_fn(&k, sizeof(k), 0);
}

const SkShaderCodeDictionary::Entry* SkShaderCodeDictionary::findOrCreate(
        SkPaintParamsKeyBuilder* builder) {
    const SkPaintParamsKey& key = builder->lockAsKey();

    SkAutoSpinlock lock{fSpinLock};

    Entry** existingEntry = fHash.find(SkPaintParamsKeyPtr{&key});
    if (existingEntry) {
        SkASSERT(fEntryVector[(*existingEntry)->uniqueID().asUInt()] == *existingEntry);
        return *existingEntry;
    }

#ifdef SK_GRAPHITE_ENABLED
    Entry* newEntry = this->makeEntry(key, builder->blendInfo());
#else
    Entry* newEntry = this->makeEntry(key);
#endif
    newEntry->setUniqueID(fEntryVector.size());
    fHash.set(SkPaintParamsKeyPtr{&newEntry->paintParamsKey()}, newEntry);
    fEntryVector.push_back(newEntry);

    return newEntry;
}

const SkShaderCodeDictionary::Entry* SkShaderCodeDictionary::lookup(
        SkUniquePaintParamsID codeID) const {

    if (!codeID.isValid()) {
        return nullptr;
    }

    SkAutoSpinlock lock{fSpinLock};

    SkASSERT(codeID.asUInt() < fEntryVector.size());

    return fEntryVector[codeID.asUInt()];
}

SkSpan<const SkUniform> SkShaderCodeDictionary::getUniforms(SkBuiltInCodeSnippetID id) const {
    return fBuiltInCodeSnippets[(int) id].fUniforms;
}

SkSpan<const DataPayloadField> SkShaderCodeDictionary::dataPayloadExpectations(
        int codeSnippetID) const {
    // All callers of this entry point should already have ensured that 'codeSnippetID' is valid
    return this->getEntry(codeSnippetID)->fDataPayloadExpectations;
}

const SkShaderSnippet* SkShaderCodeDictionary::getEntry(int codeSnippetID) const {
    if (codeSnippetID < 0) {
        return nullptr;
    }

    if (codeSnippetID < kBuiltInCodeSnippetIDCount) {
        return &fBuiltInCodeSnippets[codeSnippetID];
    }

    int userDefinedCodeSnippetID = codeSnippetID - kBuiltInCodeSnippetIDCount;
    if (userDefinedCodeSnippetID < SkTo<int>(fUserDefinedCodeSnippets.size())) {
        return fUserDefinedCodeSnippets[userDefinedCodeSnippetID].get();
    }

    return nullptr;
}

void SkShaderCodeDictionary::getShaderInfo(SkUniquePaintParamsID uniqueID,
                                           SkShaderInfo* info) const {
    auto entry = this->lookup(uniqueID);

    entry->paintParamsKey().toShaderInfo(this, info);

#ifdef SK_GRAPHITE_ENABLED
    info->setBlendInfo(entry->blendInfo());
#endif
}

//--------------------------------------------------------------------------------------------------
namespace {

#if defined(SK_GRAPHITE_ENABLED) && defined(SK_ENABLE_SKSL)
static std::string append_default_snippet_arguments(const SkShaderSnippet* entry,
                                                    int entryIndex,
                                                    const std::string& priorStageOutputName,
                                                    const std::string& fragCoord,
                                                    const std::string& currentPreLocalExpr,
                                                    SkSpan<const std::string> childOutputs) {
    std::string code = "(";

    const char* separator = "";

    // Append prior-stage output color.
    if (entry->needsPriorStageOutput()) {
        code += priorStageOutputName;
        separator = ", ";
    }

    // Append fragment coordinates.
    if (entry->needsLocalCoords()) {
        code += separator;
        code += fragCoord;
        separator = ", ";
    }

    // Append uniform names.
    for (size_t i = 0; i < entry->fUniforms.size(); ++i) {
        code += separator;
        separator = ", ";

        if (i == 0 && entry->needsLocalCoords()) {
            code += currentPreLocalExpr;
        } else {
            code += entry->getMangledUniformName(i, entryIndex);
        }
    }

    // Append samplers.
    for (size_t i = 0; i < entry->fTexturesAndSamplers.size(); ++i) {
        code += separator;
        code += entry->getMangledSamplerName(i, entryIndex);
        separator = ", ";
    }

    // Append child output names.
    for (const std::string& childOutputVar : childOutputs) {
        code += separator;
        separator = ", ";
        code += childOutputVar;
    }
    code.push_back(')');

    return code;
}

static void emit_helper_function(const SkShaderInfo& shaderInfo,
                                 int* entryIndex,
                                 std::string* preamble) {
    const SkPaintParamsKey::BlockReader& reader = shaderInfo.blockReader(*entryIndex);
    const SkShaderSnippet* entry = reader.entry();

    const int numChildren = reader.numChildren();
    SkASSERT(numChildren == entry->fNumChildren);

    // Advance over the parent entry.
    int curEntryIndex = *entryIndex;
    *entryIndex += 1;

    // Create a helper function that invokes each of the children, then calls the entry's snippet
    // and passes all the child outputs along as arguments.
    std::string helperFnName = get_mangled_name(entry->fStaticFunctionName, curEntryIndex);
    std::string helperFn = SkSL::String::printf(
            "half4 %s(half4 inColor, float4 pos, float4x4 preLocal) {",
            helperFnName.c_str());
    std::vector<std::string> childOutputVarNames;
    for (int j = 0; j < numChildren; ++j) {
        // Emit glue code into our helper function body.
        std::string childOutputVar = emit_glue_code_for_entry(shaderInfo, *entryIndex, "inColor",
                                                              "pos", "preLocal", &helperFn);
        childOutputVarNames.push_back(std::move(childOutputVar));

        // If this entry itself requires a preamble, handle that here. This will advance the
        // entry index forward as required.
        emit_preamble_for_entry(shaderInfo, entryIndex, preamble);
    }

    // Finally, invoke the snippet from the helper function, passing uniforms and child outputs.
    SkSL::String::appendf(&helperFn, "    return %s", entry->fStaticFunctionName);
    helperFn += append_default_snippet_arguments(entry, curEntryIndex, "inColor",
                                                 "pos", "preLocal", childOutputVarNames);
    helperFn += ";\n"
                "}\n";

    // Add our new helper function to the bottom of the preamble.
    *preamble += helperFn;
}
#endif

// If we have no children, the default expression just calls a built-in snippet with the signature:
//     half4 BuiltinFunctionName(/* all uniforms as parameters */);
//
// If we do have children, we have created a function in the preamble and we call that instead. Its
// signature looks like this:
//     half4 BuiltinFunctionName_N(half4 inColor, float4x4 preLocal);

std::string GenerateDefaultExpression(const SkShaderInfo& shaderInfo,
                                      int entryIndex,
                                      const SkPaintParamsKey::BlockReader& reader,
                                      const std::string& priorStageOutputName,
                                      const std::string& fragCoord,
                                      const std::string& currentPreLocalExpr) {
#if defined(SK_GRAPHITE_ENABLED) && defined(SK_ENABLE_SKSL)
    const SkShaderSnippet* entry = reader.entry();
    if (entry->fNumChildren == 0) {
        // We don't have any children; return an expression which invokes the snippet directly.
        return entry->fStaticFunctionName +
               append_default_snippet_arguments(entry, entryIndex, priorStageOutputName,
                                                fragCoord, currentPreLocalExpr,
                                                /*childOutputs=*/{});
    } else {
        // Return an expression which invokes the helper function from the preamble.
        std::string helperFnName = get_mangled_name(entry->fStaticFunctionName, entryIndex);
        return SkSL::String::printf("%s(%s, %s, %s)",
                                    helperFnName.c_str(),
                                    priorStageOutputName.c_str(),
                                    fragCoord.c_str(),
                                    currentPreLocalExpr.c_str());
    }
#else
    return priorStageOutputName;
#endif  // defined(SK_GRAPHITE_ENABLED) && defined(SK_ENABLE_SKSL)
}

// If we have no children, we don't need to add anything into the preamble.
// If we have child entries, we create a function in the preamble with a signature of:
//     half4 BuiltinFunctionName_N(half4 inColor, float4x4 preLocal) { ... }
// This function invokes each child in sequence, and then calls the built-in function, passing all
// uniforms and child outputs along:
//     half4 BuiltinFunctionName(/* all uniforms as parameters */,
//                               /* all child output variable names as parameters */);
void GenerateDefaultPreamble(const SkShaderInfo& shaderInfo,
                             int* entryIndex,
                             const SkPaintParamsKey::BlockReader& reader,
                             std::string* preamble) {
#if defined(SK_GRAPHITE_ENABLED) && defined(SK_ENABLE_SKSL)
    const SkShaderSnippet* entry = reader.entry();
    if (entry->needsLocalCoords()) {
        // Any snippet that requests local coordinates must have a localMatrix as its first uniform.
        SkASSERT(entry->fUniforms.size() >= 1);
        SkASSERT(entry->fUniforms.front().type() == SkSLType::kFloat4x4);
    }

    if (entry->fNumChildren > 0) {
        // Create a helper function which invokes all the child snippets.
        emit_helper_function(shaderInfo, entryIndex, preamble);
    } else {
        // We don't need a helper function; just advance over this entry.
        SkASSERT(reader.numChildren() == 0);
        *entryIndex += 1;
    }
#endif  // defined(SK_GRAPHITE_ENABLED) && defined(SK_ENABLE_SKSL)
}

//--------------------------------------------------------------------------------------------------
static constexpr int kFourStopGradient = 4;
static constexpr int kEightStopGradient = 8;

static constexpr SkUniform kLinearGradientUniforms4[] = {
        { "localMatrix", SkSLType::kFloat4x4 },
        { "colors",      SkSLType::kFloat4, kFourStopGradient },
        { "offsets",     SkSLType::kFloat,  kFourStopGradient },
        { "point0",      SkSLType::kFloat2 },
        { "point1",      SkSLType::kFloat2 },
        { "tilemode",    SkSLType::kInt },
};
static constexpr SkUniform kLinearGradientUniforms8[] = {
        { "localMatrix", SkSLType::kFloat4x4 },
        { "colors",      SkSLType::kFloat4, kEightStopGradient },
        { "offsets",     SkSLType::kFloat,  kEightStopGradient },
        { "point0",      SkSLType::kFloat2 },
        { "point1",      SkSLType::kFloat2 },
        { "tilemode",    SkSLType::kInt },
};

static constexpr SkUniform kRadialGradientUniforms4[] = {
        { "localMatrix", SkSLType::kFloat4x4 },
        { "colors",      SkSLType::kFloat4, kFourStopGradient },
        { "offsets",     SkSLType::kFloat,  kFourStopGradient },
        { "center",      SkSLType::kFloat2 },
        { "radius",      SkSLType::kFloat },
        { "tilemode",    SkSLType::kInt },
};
static constexpr SkUniform kRadialGradientUniforms8[] = {
        { "localMatrix", SkSLType::kFloat4x4 },
        { "colors",      SkSLType::kFloat4, kEightStopGradient },
        { "offsets",     SkSLType::kFloat,  kEightStopGradient },
        { "center",      SkSLType::kFloat2 },
        { "radius",      SkSLType::kFloat },
        { "tilemode",    SkSLType::kInt },
};

static constexpr SkUniform kSweepGradientUniforms4[] = {
        { "localMatrix", SkSLType::kFloat4x4 },
        { "colors",      SkSLType::kFloat4, kFourStopGradient },
        { "offsets",     SkSLType::kFloat,  kFourStopGradient },
        { "center",      SkSLType::kFloat2 },
        { "bias",        SkSLType::kFloat },
        { "scale",       SkSLType::kFloat },
        { "tilemode",    SkSLType::kInt },
};
static constexpr SkUniform kSweepGradientUniforms8[] = {
        { "localMatrix", SkSLType::kFloat4x4 },
        { "colors",      SkSLType::kFloat4, kEightStopGradient },
        { "offsets",     SkSLType::kFloat,  kEightStopGradient },
        { "center",      SkSLType::kFloat2 },
        { "bias",        SkSLType::kFloat },
        { "scale",       SkSLType::kFloat },
        { "tilemode",    SkSLType::kInt },
};

static constexpr SkUniform kConicalGradientUniforms4[] = {
        { "localMatrix", SkSLType::kFloat4x4 },
        { "colors",      SkSLType::kFloat4, kFourStopGradient },
        { "offsets",     SkSLType::kFloat,  kFourStopGradient },
        { "point0",      SkSLType::kFloat2 },
        { "point1",      SkSLType::kFloat2 },
        { "radius0",     SkSLType::kFloat },
        { "radius1",     SkSLType::kFloat },
        { "tilemode",    SkSLType::kInt },
};
static constexpr SkUniform kConicalGradientUniforms8[] = {
        { "localMatrix", SkSLType::kFloat4x4 },
        { "colors",      SkSLType::kFloat4, kEightStopGradient },
        { "offsets",     SkSLType::kFloat,  kEightStopGradient },
        { "point0",      SkSLType::kFloat2 },
        { "point1",      SkSLType::kFloat2 },
        { "radius0",     SkSLType::kFloat },
        { "radius1",     SkSLType::kFloat },
        { "tilemode",    SkSLType::kInt },
};

static constexpr char kLinearGradient4Name[] = "sk_linear_grad_4_shader";
static constexpr char kLinearGradient8Name[] = "sk_linear_grad_8_shader";
static constexpr char kRadialGradient4Name[] = "sk_radial_grad_4_shader";
static constexpr char kRadialGradient8Name[] = "sk_radial_grad_8_shader";
static constexpr char kSweepGradient4Name[] = "sk_sweep_grad_4_shader";
static constexpr char kSweepGradient8Name[] = "sk_sweep_grad_8_shader";
static constexpr char kConicalGradient4Name[] = "sk_conical_grad_4_shader";
static constexpr char kConicalGradient8Name[] = "sk_conical_grad_8_shader";

//--------------------------------------------------------------------------------------------------
static constexpr SkUniform kSolidShaderUniforms[] = {
        { "color", SkSLType::kFloat4 }
};

static constexpr char kSolidShaderName[] = "sk_solid_shader";

//--------------------------------------------------------------------------------------------------
static constexpr SkUniform kLocalMatrixShaderUniforms[] = {
        { "localMatrix", SkSLType::kFloat4x4 },
};

static constexpr int kNumLocalMatrixShaderChildren = 1;

static constexpr char kLocalMatrixShaderName[] = "sk_local_matrix_shader";

//--------------------------------------------------------------------------------------------------
static constexpr SkUniform kImageShaderUniforms[] = {
        { "localMatrix", SkSLType::kFloat4x4 },
        { "subset",      SkSLType::kFloat4 },
        { "tilemodeX",   SkSLType::kInt },
        { "tilemodeY",   SkSLType::kInt },
        { "imgWidth",    SkSLType::kInt },
        { "imgHeight",   SkSLType::kInt },
};

static constexpr SkTextureAndSampler kISTexturesAndSamplers[] = {
        {"sampler"},
};

static_assert(0 == static_cast<int>(SkTileMode::kClamp),  "ImageShader code depends on SkTileMode");
static_assert(1 == static_cast<int>(SkTileMode::kRepeat), "ImageShader code depends on SkTileMode");
static_assert(2 == static_cast<int>(SkTileMode::kMirror), "ImageShader code depends on SkTileMode");
static_assert(3 == static_cast<int>(SkTileMode::kDecal),  "ImageShader code depends on SkTileMode");

static constexpr char kImageShaderName[] = "sk_compute_coords";

// This is _not_ what we want to do.
// Ideally the "sk_compute_coords" code snippet could just take texture and
// sampler references and do everything. That is going to take more time to figure out though so,
// for the sake of expediency, we're generating custom code to do the sampling.
std::string GenerateImageShaderExpression(const SkShaderInfo&,
                                          int entryIndex,
                                          const SkPaintParamsKey::BlockReader& reader,
                                          const std::string& priorStageOutputName,
                                          const std::string& fragCoord,
                                          const std::string& currentPreLocalExpr) {
#if defined(SK_GRAPHITE_ENABLED) && defined(SK_ENABLE_SKSL)
    std::string samplerVarName = reader.entry()->getMangledSamplerName(0, entryIndex);

    // Uniform slot 0 is used to make the preLocalMatrix; it's handled in emit_glue_code_for_entry.
    std::string subsetName = reader.entry()->getMangledUniformName(1, entryIndex);
    std::string tmXName = reader.entry()->getMangledUniformName(2, entryIndex);
    std::string tmYName = reader.entry()->getMangledUniformName(3, entryIndex);
    std::string imgWidthName = reader.entry()->getMangledUniformName(4, entryIndex);
    std::string imgHeightName = reader.entry()->getMangledUniformName(5, entryIndex);

    return SkSL::String::printf("sample(%s, %s(%s, %s, %s, %s, %s, %s, %s))",
                                samplerVarName.c_str(),
                                reader.entry()->fStaticFunctionName,
                                fragCoord.c_str(),
                                currentPreLocalExpr.c_str(),
                                subsetName.c_str(),
                                tmXName.c_str(),
                                tmYName.c_str(),
                                imgWidthName.c_str(),
                                imgHeightName.c_str());
#else
    return priorStageOutputName;
#endif  // defined(SK_GRAPHITE_ENABLED) && defined(SK_ENABLE_SKSL)
}

//--------------------------------------------------------------------------------------------------
static constexpr SkUniform kBlendShaderUniforms[] = {
        { "blendMode", SkSLType::kInt },
};

static constexpr int kNumBlendShaderChildren = 2;

static constexpr char kBlendShaderName[] = "sk_blend_shader";

//--------------------------------------------------------------------------------------------------
static constexpr char kRuntimeShaderName[] = "RuntimeEffect";

#if defined(SK_GRAPHITE_ENABLED) && defined(SK_ENABLE_SKSL)

class GraphitePipelineCallbacks : public SkSL::PipelineStage::Callbacks {
public:
    GraphitePipelineCallbacks(const SkShaderInfo& shaderInfo,
                              int entryIndex,
                              const std::vector<int>& childEntryIndices,
                              std::string* preamble)
            : fShaderInfo(shaderInfo)
            , fEntryIndex(entryIndex)
            , fChildEntryIndices(childEntryIndices)
            , fPreamble(preamble) {}

    std::string declareUniform(const SkSL::VarDeclaration* decl) override {
        return get_mangled_name(std::string(decl->var().name()), fEntryIndex);
    }

    void defineFunction(const char* decl, const char* body, bool isMain) override {
        if (isMain) {
            SkSL::String::appendf(fPreamble,
                                  "half4 %s_%d(half4 inColor, float4 coords, float4x4 preLocal) {\n"
                                  "    float2 pos = (preLocal * coords).xy;\n"
                                  "%s"
                                  "}\n",
                                  kRuntimeShaderName,
                                  fEntryIndex,
                                  body);
        } else {
            SkSL::String::appendf(fPreamble, "%s {\n%s}\n", decl, body);
        }
    }

    void declareFunction(const char* decl) override {
        *fPreamble += std::string(decl) + ";\n";
    }

    void defineStruct(const char* definition) override {
        *fPreamble += std::string(definition) + ";\n";
    }

    void declareGlobal(const char* declaration) override {
        *fPreamble += std::string(declaration) + ";\n";
    }

    std::string sampleShader(int index, std::string coords) override {
        SkASSERT(index >= 0 && index < (int)fChildEntryIndices.size());
        return emit_expression_for_entry(fShaderInfo, fChildEntryIndices[index],
                                         "inColor", "float4(" + coords + ",0,1)", "float4x4(1.0)");
    }

    std::string sampleColorFilter(int index, std::string color) override {
        SkASSERT(index >= 0 && index < (int)fChildEntryIndices.size());
        return emit_expression_for_entry(fShaderInfo, fChildEntryIndices[index],
                                         color, "coords", "float4x4(1.0)");
    }

    std::string sampleBlender(int index, std::string src, std::string dst) override {
        // TODO(skia:13508): implement child blenders
        return src;
    }

    std::string toLinearSrgb(std::string color) override {
        // TODO(skia:13508): implement to-linear-SRGB child effect
        return color;
    }
    std::string fromLinearSrgb(std::string color) override {
        // TODO(skia:13508): implement from-linear-SRGB child effect
        return color;
    }

    std::string getMangledName(const char* name) override {
        return get_mangled_name(name, fEntryIndex);
    }

private:
    const SkShaderInfo& fShaderInfo;
    int fEntryIndex;
    const std::vector<int>& fChildEntryIndices;
    std::string* fPreamble;
};

#endif

void GenerateRuntimeShaderPreamble(const SkShaderInfo& shaderInfo,
                                   int* entryIndex,
                                   const SkPaintParamsKey::BlockReader& reader,
                                   std::string* preamble) {
#if defined(SK_GRAPHITE_ENABLED) && defined(SK_ENABLE_SKSL)
    const SkShaderSnippet* entry = reader.entry();

    // Advance over the parent entry.
    int curEntryIndex = *entryIndex;
    *entryIndex += 1;

    // Emit the preambles for all of our child effects (and advance the entry-index past them).
    // This computes the indices of our child effects, which we use when invoking them below.
    std::vector<int> childEntryIndices;
    childEntryIndices.reserve(entry->fNumChildren);
    for (int j = 0; j < entry->fNumChildren; ++j) {
        childEntryIndices.push_back(*entryIndex);
        emit_preamble_for_entry(shaderInfo, entryIndex, preamble);
    }

    // Find this runtime effect in the runtime-effect dictionary.
    const int codeSnippetId = reader.codeSnippetId();
    const SkRuntimeEffect* effect = shaderInfo.runtimeEffectDictionary()->find(codeSnippetId);
    SkASSERT(effect);
    const SkSL::Program& program = SkRuntimeEffectPriv::Program(*effect);

    GraphitePipelineCallbacks callbacks{shaderInfo, curEntryIndex, childEntryIndices, preamble};
    SkASSERT(std::string_view(entry->fName) == kRuntimeShaderName);  // the callbacks assume this
    SkSL::PipelineStage::ConvertProgram(program, "pos", "inColor", "half4(1)", &callbacks);
#endif  // defined(SK_GRAPHITE_ENABLED) && defined(SK_ENABLE_SKSL)
}

std::string GenerateRuntimeShaderExpression(const SkShaderInfo& shaderInfo,
                                            int entryIndex,
                                            const SkPaintParamsKey::BlockReader& reader,
                                            const std::string& priorStageOutputName,
                                            const std::string& fragCoord,
                                            const std::string& currentPreLocalExpr) {
#if defined(SK_GRAPHITE_ENABLED) && defined(SK_ENABLE_SKSL)
    const SkShaderSnippet* entry = reader.entry();
    return SkSL::String::printf("%s_%d(%s, %s, %s)",
                                entry->fName, entryIndex,
                                priorStageOutputName.c_str(),
                                fragCoord.c_str(),
                                currentPreLocalExpr.c_str());
#else
    return priorStageOutputName;
#endif  // defined(SK_GRAPHITE_ENABLED) && defined(SK_ENABLE_SKSL)
}

//--------------------------------------------------------------------------------------------------
// TODO: investigate the implications of having separate hlsa and rgba matrix colorfilters. It
// may be that having them separate will not contribute to combinatorial explosion.
static constexpr SkUniform kMatrixColorFilterUniforms[] = {
        { "matrix",    SkSLType::kFloat4x4 },
        { "translate", SkSLType::kFloat4 },
        { "inHSL",     SkSLType::kInt },
};

static constexpr char kMatrixColorFilterName[] = "sk_matrix_colorfilter";

//--------------------------------------------------------------------------------------------------
static constexpr SkUniform kBlendColorFilterUniforms[] = {
        { "blendMode", SkSLType::kInt },
        { "color",     SkSLType::kFloat4 }
};

static constexpr char kBlendColorFilterName[] = "sk_blend_colorfilter";

//--------------------------------------------------------------------------------------------------
static constexpr char kComposeColorFilterName[] = "ComposeColorFilter";

static constexpr int kNumComposeColorFilterChildren = 2;

void GenerateComposeColorFilterPreamble(const SkShaderInfo& shaderInfo,
                                        int* entryIndex,
                                        const SkPaintParamsKey::BlockReader& reader,
                                        std::string* preamble) {
#if defined(SK_GRAPHITE_ENABLED) && defined(SK_ENABLE_SKSL)
    const SkShaderSnippet* entry = reader.entry();
    SkASSERT(entry->fNumChildren == 2);

    // Advance over the parent entry.
    int curEntryIndex = *entryIndex;
    *entryIndex += 1;

    // Evaluate inner child.
    std::string innerColor = emit_expression_for_entry(shaderInfo, *entryIndex, "inColor", "coords",
                                                      "preLocal");

    // Emit preamble code for inner child.
    emit_preamble_for_entry(shaderInfo, entryIndex, preamble);

    // Evaluate outer child.
    std::string outerColor = emit_expression_for_entry(shaderInfo, *entryIndex, innerColor,
                                                       "coords", "preLocal");

    // Emit preamble code for outer child.
    emit_preamble_for_entry(shaderInfo, entryIndex, preamble);

    // Create a helper function that invokes the inner expression, then passes that result to the
    // outer expression, and returns the composed result.
    std::string helperFnName = get_mangled_name(entry->fStaticFunctionName, curEntryIndex);
    SkSL::String::appendf(preamble,
                          "half4 %s(half4 inColor, float4 coords, float4x4 preLocal) {\n"
                          "    return %s;\n"
                          "}\n",
                          helperFnName.c_str(),
                          outerColor.c_str());
#endif  // defined(SK_GRAPHITE_ENABLED) && defined(SK_ENABLE_SKSL)
}

//--------------------------------------------------------------------------------------------------
static constexpr SkTextureAndSampler kTableColorFilterTexturesAndSamplers[] = {
        {"tableSampler"},
};

static constexpr char kTableColorFilterName[] = "sk_table_colorfilter";

std::string GenerateTableColorFilterExpression(const SkShaderInfo& shaderInfo,
                                               int entryIndex,
                                               const SkPaintParamsKey::BlockReader& reader,
                                               const std::string& priorStageOutputName,
                                               const std::string& fragCoord,
                                               const std::string& currentPreLocalExpr) {
#if defined(SK_GRAPHITE_ENABLED) && defined(SK_ENABLE_SKSL)
    const SkShaderSnippet* entry = reader.entry();

    // Return an expression which invokes the helper function from the preamble.
    std::string helperFnName = get_mangled_name(entry->fStaticFunctionName, entryIndex);
    return SkSL::String::printf("%s(%s)",
                                helperFnName.c_str(),
                                priorStageOutputName.c_str());
#else
    return priorStageOutputName;
#endif  // defined(SK_GRAPHITE_ENABLED) && defined(SK_ENABLE_SKSL)
}

void GenerateTableColorFilterPreamble(const SkShaderInfo& shaderInfo,
                                      int* entryIndex,
                                      const SkPaintParamsKey::BlockReader& reader,
                                      std::string* preamble) {
#if defined(SK_GRAPHITE_ENABLED) && defined(SK_ENABLE_SKSL)
    const SkShaderSnippet* entry = reader.entry();
    SkASSERT(entry->fNumChildren == 0);

    int curEntryIndex = *entryIndex;
    *entryIndex += 1;

    std::string samplerName = reader.entry()->getMangledSamplerName(0, curEntryIndex);

    // Create a helper function that directly uses the mangled sampler
    std::string helperFnName = get_mangled_name(entry->fStaticFunctionName, curEntryIndex);
    SkSL::String::appendf(
            preamble,
            "half4 %s(half4 colorIn) {\n"
            "    half4 coords = unpremul(colorIn) * 255.0/256.0 + 0.5/256.0;\n"
            "    half4 color = half4(sample(%s, half2(coords.r, 3.0/8.0)).r,\n"
            "                        sample(%s, half2(coords.g, 5.0/8.0)).r,\n"
            "                        sample(%s, half2(coords.b, 7.0/8.0)).r,\n"
            "                        1);\n"
            "    return color * sample(%s, half2(coords.a, 1.0/8.0)).r;\n"
            "}\n",
            helperFnName.c_str(),
            samplerName.c_str(),
            samplerName.c_str(),
            samplerName.c_str(),
            samplerName.c_str());
#endif  // defined(SK_GRAPHITE_ENABLED) && defined(SK_ENABLE_SKSL)
}

//--------------------------------------------------------------------------------------------------
static constexpr char kGaussianColorFilterName[] = "sk_gaussian_colorfilter";

//--------------------------------------------------------------------------------------------------
static constexpr char kErrorName[] = "sk_error";

//--------------------------------------------------------------------------------------------------
static constexpr char kPassthroughName[] = "sk_passthrough";

//--------------------------------------------------------------------------------------------------
// This method generates the glue code for the case where the SkBlendMode-based blending is
// handled with fixed function blending.
std::string GenerateFixedFunctionBlenderExpression(const SkShaderInfo&,
                                                   int entryIndex,
                                                   const SkPaintParamsKey::BlockReader& reader,
                                                   const std::string& priorStageOutputName,
                                                   const std::string& fragCoord,
                                                   const std::string& currentPreLocalExpr) {
#if defined(SK_GRAPHITE_ENABLED) && defined(SK_ENABLE_SKSL)
    SkASSERT(reader.entry()->fUniforms.empty());
    SkASSERT(reader.numDataPayloadFields() == 0);

    // The actual blending is set up via the fixed function pipeline so we don't actually
    // need to access the blend mode in the glue code.
#endif  // defined(SK_GRAPHITE_ENABLED) && defined(SK_ENABLE_SKSL)

    return priorStageOutputName;
}

//--------------------------------------------------------------------------------------------------
static constexpr SkUniform kShaderBasedBlenderUniforms[] = {
        { "blendMode", SkSLType::kInt },
};

static constexpr char kBlendHelperName[] = "sk_blend";

// This method generates the glue code for the case where the SkBlendMode-based blending must occur
// in the shader (i.e., fixed function blending isn't possible).
// It exists as custom glue code so that we can deal with the dest reads. If that can be
// standardized (e.g., via a snippets requirement flag) this could be removed.
std::string GenerateShaderBasedBlenderExpression(const SkShaderInfo&,
                                                 int entryIndex,
                                                 const SkPaintParamsKey::BlockReader& reader,
                                                 const std::string& priorStageOutputName,
                                                 const std::string& fragCoord,
                                                 const std::string& currentPreLocalExpr) {
#if defined(SK_GRAPHITE_ENABLED) && defined(SK_ENABLE_SKSL)
    SkASSERT(reader.entry()->fUniforms.size() == 1);
    SkASSERT(reader.numDataPayloadFields() == 0);

    std::string uniformName = reader.entry()->getMangledUniformName(0, entryIndex);

    // TODO: emit function to perform dest read into preamble, and replace half(1) with that call

    return SkSL::String::printf("%s(%s, %s, half4(1))",
                                reader.entry()->fStaticFunctionName,
                                uniformName.c_str(),
                                priorStageOutputName.c_str());
#else
    return priorStageOutputName;
#endif  // defined(SK_GRAPHITE_ENABLED) && defined(SK_ENABLE_SKSL)
}

//--------------------------------------------------------------------------------------------------

} // anonymous namespace

bool SkShaderCodeDictionary::isValidID(int snippetID) const {
    if (snippetID < 0) {
        return false;
    }

    if (snippetID < kBuiltInCodeSnippetIDCount) {
        return true;
    }

    int userDefinedCodeSnippetID = snippetID - kBuiltInCodeSnippetIDCount;
    return userDefinedCodeSnippetID < SkTo<int>(fUserDefinedCodeSnippets.size());
}

static constexpr int kNoChildren = 0;

int SkShaderCodeDictionary::addUserDefinedSnippet(
        const char* name,
        SkSpan<const SkUniform> uniforms,
        SnippetRequirementFlags snippetRequirementFlags,
        SkSpan<const SkTextureAndSampler> texturesAndSamplers,
        const char* functionName,
        SkShaderSnippet::GenerateExpressionForSnippetFn expressionGenerator,
        SkShaderSnippet::GeneratePreambleForSnippetFn preambleGenerator,
        int numChildren,
        SkSpan<const SkPaintParamsKey::DataPayloadField> dataPayloadExpectations) {
    // TODO: the memory for user-defined entries could go in the dictionary's arena but that
    // would have to be a thread safe allocation since the arena also stores entries for
    // 'fHash' and 'fEntryVector'
    fUserDefinedCodeSnippets.push_back(std::make_unique<SkShaderSnippet>(name,
                                                                         uniforms,
                                                                         snippetRequirementFlags,
                                                                         texturesAndSamplers,
                                                                         functionName,
                                                                         expressionGenerator,
                                                                         preambleGenerator,
                                                                         numChildren,
                                                                         dataPayloadExpectations));

    return kBuiltInCodeSnippetIDCount + fUserDefinedCodeSnippets.size() - 1;
}

// TODO: this version needs to be removed
int SkShaderCodeDictionary::addUserDefinedSnippet(
        const char* name,
        SkSpan<const DataPayloadField> dataPayloadExpectations) {
    return this->addUserDefinedSnippet("UserDefined",
                                       {},  // no uniforms
                                       SnippetRequirementFlags::kNone,
                                       {},  // no samplers
                                       name,
                                       GenerateDefaultExpression,
                                       GenerateDefaultPreamble,
                                       kNoChildren,
                                       dataPayloadExpectations);
}

#ifdef SK_ENABLE_PRECOMPILE
SkBlenderID SkShaderCodeDictionary::addUserDefinedBlender(sk_sp<SkRuntimeEffect> effect) {
    if (!effect) {
        return {};
    }

    // TODO: at this point we need to extract the uniform definitions, children and helper functions
    // from the runtime effect in order to create a real SkShaderSnippet
    // Additionally, we need to hash the provided code to deduplicate the runtime effects in case
    // the client keeps giving us different rtEffects w/ the same backing SkSL.
    int codeSnippetID = this->addUserDefinedSnippet("UserDefined",
                                                    {},  // missing uniforms
                                                    SnippetRequirementFlags::kNone,
                                                    {},  // missing samplers
                                                    "foo",
                                                    GenerateDefaultExpression,
                                                    GenerateDefaultPreamble,
                                                    kNoChildren,
                                                    /*dataPayloadExpectations=*/{});
    return SkBlenderID(codeSnippetID);
}

const SkShaderSnippet* SkShaderCodeDictionary::getEntry(SkBlenderID id) const {
    return this->getEntry(id.asUInt());
}

#endif // SK_ENABLE_PRECOMPILE

static SkSLType uniform_type_to_sksl_type(const SkRuntimeEffect::Uniform& u) {
    using Type = SkRuntimeEffect::Uniform::Type;
    if (u.flags & SkRuntimeEffect::Uniform::kHalfPrecision_Flag) {
        switch (u.type) {
            case Type::kFloat:    return SkSLType::kHalf;
            case Type::kFloat2:   return SkSLType::kHalf2;
            case Type::kFloat3:   return SkSLType::kHalf3;
            case Type::kFloat4:   return SkSLType::kHalf4;
            case Type::kFloat2x2: return SkSLType::kHalf2x2;
            case Type::kFloat3x3: return SkSLType::kHalf3x3;
            case Type::kFloat4x4: return SkSLType::kHalf4x4;
            case Type::kInt:      return SkSLType::kShort;
            case Type::kInt2:     return SkSLType::kShort2;
            case Type::kInt3:     return SkSLType::kShort3;
            case Type::kInt4:     return SkSLType::kShort4;
        }
    } else {
        switch (u.type) {
            case Type::kFloat:    return SkSLType::kFloat;
            case Type::kFloat2:   return SkSLType::kFloat2;
            case Type::kFloat3:   return SkSLType::kFloat3;
            case Type::kFloat4:   return SkSLType::kFloat4;
            case Type::kFloat2x2: return SkSLType::kFloat2x2;
            case Type::kFloat3x3: return SkSLType::kFloat3x3;
            case Type::kFloat4x4: return SkSLType::kFloat4x4;
            case Type::kInt:      return SkSLType::kInt;
            case Type::kInt2:     return SkSLType::kInt2;
            case Type::kInt3:     return SkSLType::kInt3;
            case Type::kInt4:     return SkSLType::kInt4;
        }
    }
    SkUNREACHABLE;
}

const char* SkShaderCodeDictionary::addTextToArena(std::string_view text) {
    char* textInArena = fArena.makeArrayDefault<char>(text.size() + 1);
    memcpy(textInArena, text.data(), text.size());
    textInArena[text.size()] = '\0';
    return textInArena;
}

SkSpan<const SkUniform> SkShaderCodeDictionary::convertUniforms(const SkRuntimeEffect* effect) {
    using Uniform = SkRuntimeEffect::Uniform;
    SkSpan<const Uniform> uniforms = effect->uniforms();

    bool addLocalMatrixUniform = effect->allowShader();

    // Convert the SkRuntimeEffect::Uniform array into its SkUniform equivalent.
    int numUniforms = uniforms.size() + (addLocalMatrixUniform ? 1 : 0);
    SkUniform* uniformArray = fArena.makeInitializedArray<SkUniform>(numUniforms, [&](int index) {
        // Graphite wants a `localMatrix` float4x4 uniform at the front of the uniform list.
        const Uniform* u;
        if (addLocalMatrixUniform) {
            if (index == 0) {
                return SkUniform("localMatrix", SkSLType::kFloat4x4);
            }
            u = &uniforms[index - 1];
        } else {
            u = &uniforms[index];
        }

        // The existing uniform names live in the passed-in SkRuntimeEffect and may eventually
        // disappear. Copy them into fArena. (It's safe to do this within makeInitializedArray; the
        // entire array is allocated in one big slab before any initialization calls are done.)
        const char* name = this->addTextToArena(u->name);

        // Add one SkUniform to our array.
        SkSLType type = uniform_type_to_sksl_type(*u);
        return (u->flags & Uniform::kArray_Flag) ? SkUniform(name, type, u->count)
                                                 : SkUniform(name, type);
    });

    return SkSpan<const SkUniform>(uniformArray, numUniforms);
}

int SkShaderCodeDictionary::findOrCreateRuntimeEffectSnippet(const SkRuntimeEffect* effect) {
    // Use the combination of {SkSL program hash, uniform size} as our key.
    // In the unfortunate event of a hash collision, at least we'll have the right amount of
    // uniform data available.
    RuntimeEffectKey key;
    key.fHash = SkRuntimeEffectPriv::Hash(*effect);
    key.fUniformSize = effect->uniformSize();

    SkAutoSpinlock lock{fSpinLock};

    int32_t* existingCodeSnippetID = fRuntimeEffectMap.find(key);
    if (existingCodeSnippetID) {
        return *existingCodeSnippetID;
    }

    const SnippetRequirementFlags snippetFlags = effect->allowShader()
                                                         ? SnippetRequirementFlags::kLocalCoords
                                                         : SnippetRequirementFlags::kNone;
    int newCodeSnippetID = this->addUserDefinedSnippet("RuntimeEffect",
                                                       this->convertUniforms(effect),
                                                       snippetFlags,
                                                       /*texturesAndSamplers=*/{},
                                                       kRuntimeShaderName,
                                                       GenerateRuntimeShaderExpression,
                                                       GenerateRuntimeShaderPreamble,
                                                       (int)effect->children().size(),
                                                       /*dataPayloadExpectations=*/{});
    fRuntimeEffectMap.set(key, newCodeSnippetID);
    return newCodeSnippetID;
}

SkShaderCodeDictionary::SkShaderCodeDictionary() {
    // The 0th index is reserved as invalid
    fEntryVector.push_back(nullptr);

    fBuiltInCodeSnippets[(int) SkBuiltInCodeSnippetID::kError] = {
            "Error",
            { },     // no uniforms
            SnippetRequirementFlags::kNone,
            { },     // no samplers
            kErrorName,
            GenerateDefaultExpression,
            GenerateDefaultPreamble,
            kNoChildren,
            { }      // no data payload
    };
    fBuiltInCodeSnippets[(int) SkBuiltInCodeSnippetID::kPassthroughShader] = {
            "Passthrough",
            { },     // no uniforms
            SnippetRequirementFlags::kPriorStageOutput,
            { },     // no samplers
            kPassthroughName,
            GenerateDefaultExpression,
            GenerateDefaultPreamble,
            kNoChildren,
            { }      // no data payload
    };
    fBuiltInCodeSnippets[(int) SkBuiltInCodeSnippetID::kSolidColorShader] = {
            "SolidColor",
            SkSpan(kSolidShaderUniforms),
            SnippetRequirementFlags::kNone,
            { },     // no samplers
            kSolidShaderName,
            GenerateDefaultExpression,
            GenerateDefaultPreamble,
            kNoChildren,
            { }      // no data payload
    };
    fBuiltInCodeSnippets[(int) SkBuiltInCodeSnippetID::kLinearGradientShader4] = {
            "LinearGradient4",
            SkSpan(kLinearGradientUniforms4),
            SnippetRequirementFlags::kLocalCoords,
            { },     // no samplers
            kLinearGradient4Name,
            GenerateDefaultExpression,
            GenerateDefaultPreamble,
            kNoChildren,
            { }      // no data payload
    };
    fBuiltInCodeSnippets[(int) SkBuiltInCodeSnippetID::kLinearGradientShader8] = {
            "LinearGradient8",
            SkSpan(kLinearGradientUniforms8),
            SnippetRequirementFlags::kLocalCoords,
            { },     // no samplers
            kLinearGradient8Name,
            GenerateDefaultExpression,
            GenerateDefaultPreamble,
            kNoChildren,
            { }      // no data payload
    };
    fBuiltInCodeSnippets[(int) SkBuiltInCodeSnippetID::kRadialGradientShader4] = {
            "RadialGradient4",
            SkSpan(kRadialGradientUniforms4),
            SnippetRequirementFlags::kLocalCoords,
            { },     // no samplers
            kRadialGradient4Name,
            GenerateDefaultExpression,
            GenerateDefaultPreamble,
            kNoChildren,
            { }      // no data payload
    };
    fBuiltInCodeSnippets[(int) SkBuiltInCodeSnippetID::kRadialGradientShader8] = {
            "RadialGradient8",
            SkSpan(kRadialGradientUniforms8),
            SnippetRequirementFlags::kLocalCoords,
            { },     // no samplers
            kRadialGradient8Name,
            GenerateDefaultExpression,
            GenerateDefaultPreamble,
            kNoChildren,
            { }      // no data payload
    };
    fBuiltInCodeSnippets[(int) SkBuiltInCodeSnippetID::kSweepGradientShader4] = {
            "SweepGradient4",
            SkSpan(kSweepGradientUniforms4),
            SnippetRequirementFlags::kLocalCoords,
            { },     // no samplers
            kSweepGradient4Name,
            GenerateDefaultExpression,
            GenerateDefaultPreamble,
            kNoChildren,
            { }      // no data payload
    };
    fBuiltInCodeSnippets[(int) SkBuiltInCodeSnippetID::kSweepGradientShader8] = {
            "SweepGradient8",
            SkSpan(kSweepGradientUniforms8),
            SnippetRequirementFlags::kLocalCoords,
            { },     // no samplers
            kSweepGradient8Name,
            GenerateDefaultExpression,
            GenerateDefaultPreamble,
            kNoChildren,
            { }      // no data payload
    };
    fBuiltInCodeSnippets[(int) SkBuiltInCodeSnippetID::kConicalGradientShader4] = {
            "ConicalGradient4",
            SkSpan(kConicalGradientUniforms4),
            SnippetRequirementFlags::kLocalCoords,
            { },     // no samplers
            kConicalGradient4Name,
            GenerateDefaultExpression,
            GenerateDefaultPreamble,
            kNoChildren,
            { }      // no data payload
    };
    fBuiltInCodeSnippets[(int) SkBuiltInCodeSnippetID::kConicalGradientShader8] = {
            "ConicalGradient8",
            SkSpan(kConicalGradientUniforms8),
            SnippetRequirementFlags::kLocalCoords,
            { },     // no samplers
            kConicalGradient8Name,
            GenerateDefaultExpression,
            GenerateDefaultPreamble,
            kNoChildren,
            { }      // no data payload
    };
    fBuiltInCodeSnippets[(int) SkBuiltInCodeSnippetID::kLocalMatrixShader] = {
            "LocalMatrixShader",
            SkSpan(kLocalMatrixShaderUniforms),
            SnippetRequirementFlags::kLocalCoords,
            { },     // no samplers
            kLocalMatrixShaderName,
            GenerateDefaultExpression,
            GenerateDefaultPreamble,
            kNumLocalMatrixShaderChildren,
            { }      // no data payload
    };
    fBuiltInCodeSnippets[(int) SkBuiltInCodeSnippetID::kImageShader] = {
            "ImageShader",
            SkSpan(kImageShaderUniforms),
            SnippetRequirementFlags::kLocalCoords,
            SkSpan(kISTexturesAndSamplers),
            kImageShaderName,
            GenerateImageShaderExpression,
            GenerateDefaultPreamble,
            kNoChildren,
            { }      // no data payload
    };
    fBuiltInCodeSnippets[(int) SkBuiltInCodeSnippetID::kBlendShader] = {
            "BlendShader",
            SkSpan(kBlendShaderUniforms),
            SnippetRequirementFlags::kNone,
            { },     // no samplers
            kBlendShaderName,
            GenerateDefaultExpression,
            GenerateDefaultPreamble,
            kNumBlendShaderChildren,
            { }      // no data payload
    };

    // SkColorFilter snippets
    fBuiltInCodeSnippets[(int) SkBuiltInCodeSnippetID::kMatrixColorFilter] = {
            "MatrixColorFilter",
            SkSpan(kMatrixColorFilterUniforms),
            SnippetRequirementFlags::kPriorStageOutput,
            { },     // no samplers
            kMatrixColorFilterName,
            GenerateDefaultExpression,
            GenerateDefaultPreamble,
            kNoChildren,
            { }      // no data payload
    };
    fBuiltInCodeSnippets[(int) SkBuiltInCodeSnippetID::kBlendColorFilter] = {
            "BlendColorFilter",
            SkSpan(kBlendColorFilterUniforms),
            SnippetRequirementFlags::kPriorStageOutput,
            { },     // no samplers
            kBlendColorFilterName,
            GenerateDefaultExpression,
            GenerateDefaultPreamble,
            kNoChildren,
            { }      // no data payload
    };
    fBuiltInCodeSnippets[(int) SkBuiltInCodeSnippetID::kComposeColorFilter] = {
            "ComposeColorFilter",
            { },     // no uniforms
            SnippetRequirementFlags::kPriorStageOutput,
            { },     // no samplers
            kComposeColorFilterName,
            GenerateDefaultExpression,
            GenerateComposeColorFilterPreamble,
            kNumComposeColorFilterChildren,
            { }      // no data payload
    };
    fBuiltInCodeSnippets[(int) SkBuiltInCodeSnippetID::kTableColorFilter] = {
            "TableColorFilter",
            { },     // no uniforms
            SnippetRequirementFlags::kPriorStageOutput,
            SkSpan(kTableColorFilterTexturesAndSamplers),
            kTableColorFilterName,
            GenerateTableColorFilterExpression,
            GenerateTableColorFilterPreamble,
            kNoChildren,
            { }      // no data payload
    };
    fBuiltInCodeSnippets[(int) SkBuiltInCodeSnippetID::kGaussianColorFilter] = {
            "GaussianColorFilter",
            { },     // no uniforms
            SnippetRequirementFlags::kPriorStageOutput,
            { },     // no samplers
            kGaussianColorFilterName,
            GenerateDefaultExpression,
            GenerateDefaultPreamble,
            kNoChildren,
            { }      // no data payload
    };

    fBuiltInCodeSnippets[(int) SkBuiltInCodeSnippetID::kFixedFunctionBlender] = {
            "FixedFunctionBlender",
            { },     // no uniforms
            SnippetRequirementFlags::kNone,
            { },     // no samplers
            "FF-blending",  // fixed function blending doesn't use static SkSL
            GenerateFixedFunctionBlenderExpression,
            GenerateDefaultPreamble,
            kNoChildren,
            { }      // no data payload
    };
    fBuiltInCodeSnippets[(int) SkBuiltInCodeSnippetID::kShaderBasedBlender] = {
            "ShaderBasedBlender",
            SkSpan(kShaderBasedBlenderUniforms),
            SnippetRequirementFlags::kNone,
            { },     // no samplers
            kBlendHelperName,
            GenerateShaderBasedBlenderExpression,
            GenerateDefaultPreamble,
            kNoChildren,
            { }      // no data payload
    };
}
