/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/GrProgramDesc.h"

#include "include/private/SkChecksum.h"
#include "include/private/SkTo.h"
#include "src/gpu/GrPipeline.h"
#include "src/gpu/GrPrimitiveProcessor.h"
#include "src/gpu/GrProcessor.h"
#include "src/gpu/GrProgramInfo.h"
#include "src/gpu/GrRenderTarget.h"
#include "src/gpu/GrShaderCaps.h"
#include "src/gpu/GrTexture.h"
#include "src/gpu/glsl/GrGLSLFragmentProcessor.h"
#include "src/gpu/glsl/GrGLSLFragmentShaderBuilder.h"

enum {
    kSamplerOrImageTypeKeyBits = 4
};

static inline uint16_t texture_type_key(GrTextureType type) {
    int value = UINT16_MAX;
    switch (type) {
        case GrTextureType::k2D:
            value = 0;
            break;
        case GrTextureType::kExternal:
            value = 1;
            break;
        case GrTextureType::kRectangle:
            value = 2;
            break;
        default:
            SK_ABORT("Unexpected texture type");
            value = 3;
            break;
    }
    SkASSERT((value & ((1 << kSamplerOrImageTypeKeyBits) - 1)) == value);
    return SkToU16(value);
}

static uint32_t sampler_key(GrTextureType textureType, const GrSwizzle& swizzle,
                            const GrCaps& caps) {
    int samplerTypeKey = texture_type_key(textureType);

    static_assert(2 == sizeof(swizzle.asKey()));
    uint16_t swizzleKey = swizzle.asKey();
    return SkToU32(samplerTypeKey | swizzleKey << kSamplerOrImageTypeKeyBits);
}

static void add_pp_sampler_keys(GrProcessorKeyBuilder* b, const GrPrimitiveProcessor& pp,
                                const GrCaps& caps) {
    int numTextureSamplers = pp.numTextureSamplers();
    if (!numTextureSamplers) {
        return;
    }
    for (int i = 0; i < numTextureSamplers; ++i) {
        const GrPrimitiveProcessor::TextureSampler& sampler = pp.textureSampler(i);
        const GrBackendFormat& backendFormat = sampler.backendFormat();

        uint32_t samplerKey = sampler_key(backendFormat.textureType(), sampler.swizzle(), caps);
        b->add32(samplerKey);

        caps.addExtraSamplerKey(b, sampler.samplerState(), backendFormat);
    }
}

// Currently we allow 8 bits for the class id and 24 bits for the overall processor key size
// (as measured in bits, so the byte count of the processor key must be < 2^21).
static constexpr uint32_t kClassIDBits = 8;
static constexpr uint32_t kKeySizeBits = 24;

static bool processor_meta_data_fits(uint32_t classID, size_t keySize) {
    return (classID < (1u << kClassIDBits)) && (keySize < (1u << kKeySizeBits));
}

/**
 * A function which emits a meta key into the key builder.  This is required because shader code may
 * be dependent on properties of the effect that the effect itself doesn't use
 * in its key (e.g. the pixel format of textures used). So we create a meta-key for
 * every effect using this function. It is also responsible for inserting the effect's class ID
 * which must be different for every GrProcessor subclass. It can fail if an effect uses too many
 * transforms, etc, for the space allotted in the meta-key.  NOTE, both FPs and GPs share this
 * function because it is hairy, though FPs do not have attribs, and GPs do not have transforms
 */
static bool gen_fp_meta_key(const GrFragmentProcessor& fp,
                            const GrCaps& caps,
                            uint32_t transformKey,
                            GrProcessorKeyBuilder* b) {
    size_t processorKeySize = b->sizeInBits();
    uint32_t classID = fp.classID();

    if (!processor_meta_data_fits(classID, processorKeySize)) {
        return false;
    }

    fp.visitTextureEffects([&](const GrTextureEffect& te) {
        const GrBackendFormat& backendFormat = te.view().proxy()->backendFormat();
        uint32_t samplerKey = sampler_key(backendFormat.textureType(), te.view().swizzle(), caps);
        b->add32(samplerKey);
        caps.addExtraSamplerKey(b, te.samplerState(), backendFormat);
    });

    b->addBits(kClassIDBits, classID,          "fpClassID");
    b->addBits(kKeySizeBits, processorKeySize, "fpKeySize");
    b->add32(transformKey,                     "fpTransforms");
    return true;
}

static bool gen_pp_meta_key(const GrPrimitiveProcessor& pp,
                            const GrCaps& caps,
                            GrProcessorKeyBuilder* b) {
    size_t processorKeySize = b->sizeInBits();
    uint32_t classID = pp.classID();

    if (!processor_meta_data_fits(classID, processorKeySize)) {
        return false;
    }

    add_pp_sampler_keys(b, pp, caps);

    b->addBits(kClassIDBits, classID,          "ppClassID");
    b->addBits(kKeySizeBits, processorKeySize, "ppKeySize");
    return true;
}

static bool gen_xp_meta_key(const GrXferProcessor& xp, GrProcessorKeyBuilder* b) {
    size_t processorKeySize = b->sizeInBits();
    uint32_t classID = xp.classID();

    if (!processor_meta_data_fits(classID, processorKeySize)) {
        return false;
    }

    b->addBits(kClassIDBits, classID,          "xpClassID");
    b->addBits(kKeySizeBits, processorKeySize, "xpKeySize");
    return true;
}

static bool gen_frag_proc_and_meta_keys(const GrFragmentProcessor& fp,
                                        const GrCaps& caps,
                                        GrProcessorKeyBuilder* b) {
    for (int i = 0; i < fp.numChildProcessors(); ++i) {
        if (auto child = fp.childProcessor(i)) {
            if (!gen_frag_proc_and_meta_keys(*child, caps, b)) {
                return false;
            }
        } else {
            // Fold in a sentinel value as the "class ID" for any null children
            b->add32(GrProcessor::ClassID::kNull_ClassID);
        }
    }

    b->addString([&](){ return fp.name(); });
    fp.getGLSLProcessorKey(*caps.shaderCaps(), b);

    return gen_fp_meta_key(fp, caps, GrPrimitiveProcessor::ComputeCoordTransformsKey(fp), b);
}

bool GrProgramDesc::Build(GrProgramDesc* desc,
                          GrRenderTarget* renderTarget,
                          const GrProgramInfo& programInfo,
                          const GrCaps& caps) {
#ifdef SK_DEBUG
    if (renderTarget) {
        SkASSERT(programInfo.backendFormat() == renderTarget->backendFormat());
    }
#endif

    // The descriptor is used as a cache key. Thus when a field of the
    // descriptor will not affect program generation (because of the attribute
    // bindings in use or other descriptor field settings) it should be set
    // to a canonical value to avoid duplicate programs with different keys.

    desc->key().reset();
    GrProcessorKeyBuilder b(&desc->key());

    const GrPrimitiveProcessor& primitiveProcessor = programInfo.primProc();
    b.addString([&](){ return primitiveProcessor.name(); });
    primitiveProcessor.getGLSLProcessorKey(*caps.shaderCaps(), &b);
    primitiveProcessor.getAttributeKey(&b);
    if (!gen_pp_meta_key(primitiveProcessor, caps, &b)) {
        desc->key().reset();
        return false;
    }

    const GrPipeline& pipeline = programInfo.pipeline();
    int numColorFPs = 0, numCoverageFPs = 0;
    for (int i = 0; i < pipeline.numFragmentProcessors(); ++i) {
        const GrFragmentProcessor& fp = pipeline.getFragmentProcessor(i);
        if (!gen_frag_proc_and_meta_keys(fp, caps, &b)) {
            desc->key().reset();
            return false;
        }
        if (pipeline.isColorFragmentProcessor(i)) {
            ++numColorFPs;
        } else if (pipeline.isCoverageFragmentProcessor(i)) {
            ++numCoverageFPs;
        }
    }

    const GrXferProcessor& xp = pipeline.getXferProcessor();
    const GrSurfaceOrigin* originIfDstTexture = nullptr;
    GrSurfaceOrigin origin;
    if (pipeline.dstProxyView().proxy()) {
        origin = pipeline.dstProxyView().origin();
        originIfDstTexture = &origin;
    }
    b.addString([&](){ return xp.name(); });
    xp.getGLSLProcessorKey(*caps.shaderCaps(), &b, originIfDstTexture, pipeline.dstSampleType());
    if (!gen_xp_meta_key(xp, &b)) {
        desc->key().reset();
        return false;
    }

    if (programInfo.requestedFeatures() & GrProcessor::CustomFeatures::kSampleLocations) {
        SkASSERT(pipeline.isHWAntialiasState());
        b.add32(renderTarget->getSamplePatternKey());
    }

    // Add "header" metadata
    b.addBits(16, pipeline.writeSwizzle().asKey(), "writeSwizzle");
    b.addBits( 1, numColorFPs,    "numColorFPs");
    b.addBits( 2, numCoverageFPs, "numCoverageFPs");
    // If we knew the shader won't depend on origin, we could skip this (and use the same program
    // for both origins). Instrumenting all fragment processors would be difficult and error prone.
    b.addBits( 2, GrGLSLFragmentShaderBuilder::KeyForSurfaceOrigin(programInfo.origin()), "origin");
    b.addBits( 1, static_cast<uint32_t>(programInfo.requestedFeatures()), "requestedFeatures");
    b.addBits( 1, pipeline.snapVerticesToPixelCenters(), "snapVertices");
    // The base descriptor only stores whether or not the primitiveType is kPoints. Backend-
    // specific versions (e.g., Vulkan) require more detail
    b.addBits( 1, (programInfo.primitiveType() == GrPrimitiveType::kPoints), "isPoints");

    // Put a clean break between the "common" data written by this function, and any backend data
    // appended later. The initial key length will just be this portion (rounded to 4 bytes).
    b.flush();
    desc->fInitialKeyLength = desc->keyLength();

    return true;
}
