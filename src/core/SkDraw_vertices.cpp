/*
 * Copyright 2017 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkString.h"
#include "include/private/SkNx.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkAutoBlitterChoose.h"
#include "src/core/SkConvertPixels.h"
#include "src/core/SkCoreBlitters.h"
#include "src/core/SkDraw.h"
#include "src/core/SkRasterClip.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkScan.h"
#include "src/core/SkVertState.h"
#include "src/shaders/SkComposeShader.h"
#include "src/shaders/SkShaderBase.h"

// Compute the crossing point (across zero) for the two values, expressed as a
// normalized 0...1 value. If curr is 0, returns 0. If next is 0, returns 1.
//
static float compute_t(float curr, float next) {
    SkASSERT((curr > 0 && next <= 0) || (curr <= 0 && next > 0));
    float t = curr / (curr - next);
    SkASSERT(t >= 0 && t <= 1);
    return t;
}

static SkPoint3 lerp(SkPoint3 curr, SkPoint3 next, float t) {
    return curr + t * (next - curr);
}

// tol is the nudge away from zero, to keep the numerics nice.
// Think of it as our near-clipping-plane (or w-plane).
static SkPoint3 clip(SkPoint3 curr, SkPoint3 next, float tol) {
    // Return the point between curr and next where the fZ value corses tol.
    // To be (really) perspective correct, we should be computing baesd on 1/Z, not Z.
    // For now, this is close enough (and faster).
    return lerp(curr, next, compute_t(curr.fZ - tol, next.fZ - tol));
}

constexpr int kMaxClippedTrianglePointCount = 4;
// Clip a triangle (based on its homogeneous W values), and return the projected polygon.
// Since we only clip against one "edge"/plane, the max number of points in the clipped
// polygon is 4.
static int clip_triangle(SkPoint dst[], const int idx[3], const SkPoint3 pts[]) {
    SkPoint3 outPoints[4];
    SkPoint3* outP = outPoints;
    const float tol = 0.05f;

    for (int i = 0; i < 3; ++i) {
        int curr = idx[i];
        int next = idx[(i + 1) % 3];
        if (pts[curr].fZ > tol) {
            *outP++ = pts[curr];
            if (pts[next].fZ <= tol) { // curr is IN, next is OUT
                *outP++ = clip(pts[curr], pts[next], tol);
            }
        } else {
            if (pts[next].fZ > tol) { // curr is OUT, next is IN
                *outP++ = clip(pts[curr], pts[next], tol);
            }
        }
    }

    const int count = outP - outPoints;
    SkASSERT(count == 0 || count == 3 || count == 4);
    for (int i = 0; i < count; ++i) {
        float scale = 1.0f / outPoints[i].fZ;
        dst[i].set(outPoints[i].fX * scale, outPoints[i].fY * scale);
    }
    return count;
}

struct Matrix43 {
    float fMat[12];    // column major

    Sk4f map(float x, float y) const {
        return Sk4f::Load(&fMat[0]) * x + Sk4f::Load(&fMat[4]) * y + Sk4f::Load(&fMat[8]);
    }

    // Pass a by value, so we don't have to worry about aliasing with this
    void setConcat(const Matrix43 a, const SkMatrix& b) {
        SkASSERT(!b.hasPerspective());

        fMat[ 0] = a.dot(0, b.getScaleX(), b.getSkewY());
        fMat[ 1] = a.dot(1, b.getScaleX(), b.getSkewY());
        fMat[ 2] = a.dot(2, b.getScaleX(), b.getSkewY());
        fMat[ 3] = a.dot(3, b.getScaleX(), b.getSkewY());

        fMat[ 4] = a.dot(0, b.getSkewX(), b.getScaleY());
        fMat[ 5] = a.dot(1, b.getSkewX(), b.getScaleY());
        fMat[ 6] = a.dot(2, b.getSkewX(), b.getScaleY());
        fMat[ 7] = a.dot(3, b.getSkewX(), b.getScaleY());

        fMat[ 8] = a.dot(0, b.getTranslateX(), b.getTranslateY()) + a.fMat[ 8];
        fMat[ 9] = a.dot(1, b.getTranslateX(), b.getTranslateY()) + a.fMat[ 9];
        fMat[10] = a.dot(2, b.getTranslateX(), b.getTranslateY()) + a.fMat[10];
        fMat[11] = a.dot(3, b.getTranslateX(), b.getTranslateY()) + a.fMat[11];
    }

private:
    float dot(int index, float x, float y) const {
        return fMat[index + 0] * x + fMat[index + 4] * y;
    }
};

static SkScan::HairRCProc ChooseHairProc(bool doAntiAlias) {
    return doAntiAlias ? SkScan::AntiHairLine : SkScan::HairLine;
}

static bool SK_WARN_UNUSED_RESULT
texture_to_matrix(const VertState& state, const SkPoint verts[], const SkPoint texs[],
                  SkMatrix* matrix) {
    SkPoint src[3], dst[3];

    src[0] = texs[state.f0];
    src[1] = texs[state.f1];
    src[2] = texs[state.f2];
    dst[0] = verts[state.f0];
    dst[1] = verts[state.f1];
    dst[2] = verts[state.f2];
    return matrix->setPolyToPoly(src, dst, 3);
}

class SkTriColorShader : public SkShaderBase {
public:
    SkTriColorShader(bool isOpaque, bool usePersp) : fIsOpaque(isOpaque), fUsePersp(usePersp) {}

    // This gets called for each triangle, without re-calling onAppendStages.
    bool update(const SkMatrix& ctmInv, const SkPoint pts[], const SkPMColor4f colors[],
                int index0, int index1, int index2);

protected:
#ifdef SK_ENABLE_LEGACY_SHADERCONTEXT
    Context* onMakeContext(const ContextRec& rec, SkArenaAlloc* alloc) const override {
        return nullptr;
    }
#endif
    bool onAppendStages(const SkStageRec& rec) const override {
        rec.fPipeline->append(SkRasterPipeline::seed_shader);
        if (fUsePersp) {
            rec.fPipeline->append(SkRasterPipeline::matrix_perspective, &fM33);
        }
        rec.fPipeline->append(SkRasterPipeline::matrix_4x3, &fM43);
        return true;
    }

private:
    bool isOpaque() const override { return fIsOpaque; }
    // For serialization.  This will never be called.
    Factory getFactory() const override { return nullptr; }
    const char* getTypeName() const override { return nullptr; }

    // If fUsePersp, we need both of these matrices,
    // otherwise we can combine them, and only use fM43

    Matrix43 fM43;
    SkMatrix fM33;
    const bool fIsOpaque;
    const bool fUsePersp;   // controls our stages, and what we do in update()

    typedef SkShaderBase INHERITED;
};

bool SkTriColorShader::update(const SkMatrix& ctmInv, const SkPoint pts[],
                              const SkPMColor4f colors[], int index0, int index1, int index2) {
    SkMatrix m, im;
    m.reset();
    m.set(0, pts[index1].fX - pts[index0].fX);
    m.set(1, pts[index2].fX - pts[index0].fX);
    m.set(2, pts[index0].fX);
    m.set(3, pts[index1].fY - pts[index0].fY);
    m.set(4, pts[index2].fY - pts[index0].fY);
    m.set(5, pts[index0].fY);
    if (!m.invert(&im)) {
        return false;
    }

    fM33.setConcat(im, ctmInv);

    Sk4f c0 = Sk4f::Load(colors[index0].vec()),
         c1 = Sk4f::Load(colors[index1].vec()),
         c2 = Sk4f::Load(colors[index2].vec());

    (c1 - c0).store(&fM43.fMat[0]);
    (c2 - c0).store(&fM43.fMat[4]);
    c0.store(&fM43.fMat[8]);

    if (!fUsePersp) {
        fM43.setConcat(fM43, fM33);
    }
    return true;
}

// Convert the SkColors into float colors. The conversion depends on some conditions:
// - If the pixmap has a dst colorspace, we have to be "color-correct".
//   Do we map into dst-colorspace before or after we interpolate?
// - We have to decide when to apply per-color alpha (before or after we interpolate)
//
// For now, we will take a simple approach, but recognize this is just a start:
// - convert colors into dst colorspace before interpolation (matches gradients)
// - apply per-color alpha before interpolation (matches old version of vertices)
//
static SkPMColor4f* convert_colors(const SkColor src[], int count, SkColorSpace* deviceCS,
                                   SkArenaAlloc* alloc) {
    SkPMColor4f* dst = alloc->makeArray<SkPMColor4f>(count);
    SkImageInfo srcInfo = SkImageInfo::Make(count, 1, kBGRA_8888_SkColorType,
                                            kUnpremul_SkAlphaType, SkColorSpace::MakeSRGB());
    SkImageInfo dstInfo = SkImageInfo::Make(count, 1, kRGBA_F32_SkColorType,
                                            kPremul_SkAlphaType, sk_ref_sp(deviceCS));
    SkConvertPixels(dstInfo, dst, 0, srcInfo, src, 0);
    return dst;
}

static bool compute_is_opaque(const SkColor colors[], int count) {
    uint32_t c = ~0;
    for (int i = 0; i < count; ++i) {
        c &= colors[i];
    }
    return SkColorGetA(c) == 0xFF;
}

void SkDraw::drawVertices(SkVertices::VertexMode vmode, int vertexCount,
                          const SkPoint vertices[], const SkPoint textures[],
                          const SkColor colors[], const SkVertices::BoneIndices boneIndices[],
                          const SkVertices::BoneWeights boneWeights[], SkBlendMode bmode,
                          const uint16_t indices[], int indexCount,
                          const SkPaint& paint, const SkVertices::Bone bones[],
                          int boneCount) const {
    SkASSERT(0 == vertexCount || vertices);

    // abort early if there is nothing to draw
    if (vertexCount < 3 || (indices && indexCount < 3) || fRC->isEmpty()) {
        return;
    }
    SkMatrix ctmInv;
    if (!fMatrix->invert(&ctmInv)) {
        return;
    }

    // make textures and shader mutually consistent
    SkShader* shader = paint.getShader();
    if (!(shader && textures)) {
        shader = nullptr;
        textures = nullptr;
    }

    // We can simplify things for certain blendmodes. This is for speed, and SkComposeShader
    // itself insists we don't pass kSrc or kDst to it.
    //
    if (colors && textures) {
        switch (bmode) {
            case SkBlendMode::kSrc:
                colors = nullptr;
                break;
            case SkBlendMode::kDst:
                textures = nullptr;
                break;
            default: break;
        }
    }

    // we don't use the shader if there are no textures
    if (!textures) {
        shader = nullptr;
    }

    constexpr size_t kDefVertexCount = 16;
    constexpr size_t kOuterSize = sizeof(SkTriColorShader) +
                                 sizeof(SkShader_Blend) +
                                 (2 * sizeof(SkPoint) + sizeof(SkColor4f)) * kDefVertexCount;
    SkSTArenaAlloc<kOuterSize> outerAlloc;

    // deform vertices using the skeleton if it is passed in
    if (bones && boneCount) {
        // allocate space for the deformed vertices
        SkPoint* deformed = outerAlloc.makeArray<SkPoint>(vertexCount);

        // deform the vertices
        if (boneIndices && boneWeights) {
            for (int i = 0; i < vertexCount; i ++) {
                const SkVertices::BoneIndices& indices = boneIndices[i];
                const SkVertices::BoneWeights& weights = boneWeights[i];

                // apply the world transform
                SkPoint worldPoint = bones[0].mapPoint(vertices[i]);

                // apply bone deformations
                deformed[i] = SkPoint::Make(0.0f, 0.0f);
                for (uint32_t j = 0; j < 4; j ++) {
                    // get the attachment data
                    uint32_t index = indices[j];
                    float weight = weights[j];

                    // skip the bone if there is no weight
                    if (weight == 0.0f) {
                        continue;
                    }
                    SkASSERT(index != 0);

                    // deformed += M * v * w
                    deformed[i] += bones[index].mapPoint(worldPoint) * weight;
                }
            }
        } else {
            // no bones, so only apply world transform
            SkMatrix worldTransform = SkMatrix::I();
            worldTransform.setAffine(bones[0].values);
            worldTransform.mapPoints(deformed, vertices, vertexCount);
        }

        // change the vertices to point to deformed
        vertices = deformed;
    }

    /*  We need to know if we have perspective or not, so we can know what stage(s) we will need,
        and how to prep our "uniforms" before each triangle in the tricolorshader.

        We could just check the matrix on each triangle to decide, but we have to be sure to always
        make the same decision, since we create 1 or 2 stages only once for the entire patch.

        To be safe, we just make that determination here, and pass it into the tricolorshader.
     */
    const bool usePerspective = fMatrix->hasPerspective();

    SkPoint* devVerts = nullptr;
    SkPoint3* dev3 = nullptr;

    if (usePerspective) {
        dev3 = outerAlloc.makeArray<SkPoint3>(vertexCount);
        fMatrix->mapHomogeneousPoints(dev3, vertices, vertexCount);
        // similar to the bounds check for 2d points (below)
        if (!SkScalarsAreFinite((const SkScalar*)dev3, vertexCount * 3)) {
            return;
        }
    } else {
        devVerts = outerAlloc.makeArray<SkPoint>(vertexCount);
        fMatrix->mapPoints(devVerts, vertices, vertexCount);

        SkRect bounds;
        // this also sets bounds to empty if we see a non-finite value
        bounds.setBounds(devVerts, vertexCount);
        if (bounds.isEmpty()) {
            return;
        }
    }

    VertState       state(vertexCount, indices, indexCount);
    VertState::Proc vertProc = state.chooseProc(vmode);

    // Draw hairlines to show the skeleton
    if (!(colors || textures)) {
        // no colors[] and no texture, stroke hairlines with paint's color.
        SkPaint p;
        p.setStyle(SkPaint::kStroke_Style);
        SkAutoBlitterChoose blitter(*this, nullptr, p);
        // Abort early if we failed to create a shader context.
        if (blitter->isNullBlitter()) {
            return;
        }
        SkScan::HairRCProc hairProc = ChooseHairProc(paint.isAntiAlias());
        const SkRasterClip& clip = *fRC;
        while (vertProc(&state)) {
            if (dev3) {
                SkPoint tmp[kMaxClippedTrianglePointCount + 2];
                int idx[] = { state.f0, state.f1, state.f2 };
                if (int n = clip_triangle(tmp, idx, dev3)) {
                    tmp[n] = tmp[0];    // close the poly
                    if (n == 3) {
                        n = 4;
                    } else {
                        SkASSERT(n == 4);
                        tmp[5] = tmp[2];    // add diagonal
                        n = 6;
                    }
                    hairProc(tmp, n, clip, blitter.get());
                }
            } else {
                SkPoint array[] = {
                    devVerts[state.f0], devVerts[state.f1], devVerts[state.f2], devVerts[state.f0]
                };
                hairProc(array, 4, clip, blitter.get());
            }
        }
        return;
    }

    SkTriColorShader* triShader = nullptr;
    SkPMColor4f*  dstColors = nullptr;

    if (colors) {
        dstColors = convert_colors(colors, vertexCount, fDst.colorSpace(), &outerAlloc);
        triShader = outerAlloc.make<SkTriColorShader>(compute_is_opaque(colors, vertexCount),
                                                      usePerspective);
        if (shader) {
            shader = outerAlloc.make<SkShader_Blend>(bmode,
                                                     sk_ref_sp(triShader), sk_ref_sp(shader),
                                                     nullptr);
        } else {
            shader = triShader;
        }
    }

    auto handle_devVerts = [&](SkBlitter* blitter) {
        SkPoint tmp[] = {
            devVerts[state.f0], devVerts[state.f1], devVerts[state.f2]
        };
        SkScan::FillTriangle(tmp, *fRC, blitter);
    };

    auto handle_dev3 = [&](SkBlitter* blitter) {
        SkPoint tmp[kMaxClippedTrianglePointCount];
        int idx[] = { state.f0, state.f1, state.f2 };
        if (int n = clip_triangle(tmp, idx, dev3)) {
            // TODO: SkScan::FillConvexPoly(tmp, n, ...);
            SkASSERT(n == 3 || n == 4);
            SkScan::FillTriangle(tmp, *fRC, blitter);
            if (n == 4) {
                tmp[1] = tmp[2];
                tmp[2] = tmp[3];
                SkScan::FillTriangle(tmp, *fRC, blitter);
            }
        }
    };

    SkPaint p(paint);
    p.setShader(sk_ref_sp(shader));

    if (!textures) {    // only tricolor shader
        auto blitter = SkCreateRasterPipelineBlitter(fDst, p, *fMatrix, &outerAlloc);
        while (vertProc(&state)) {
            if (!triShader->update(ctmInv, vertices, dstColors, state.f0, state.f1, state.f2)) {
                continue;
            }

            if (dev3) {
                handle_dev3(blitter);
            } else {
                handle_devVerts(blitter);
            }
        }
        return;
    }

    SkRasterPipeline pipeline(&outerAlloc);
    SkStageRec rec = {
        &pipeline, &outerAlloc, fDst.colorType(), fDst.colorSpace(), p, nullptr, *fMatrix
    };
    if (auto updater = as_SB(shader)->appendUpdatableStages(rec)) {
        bool isOpaque = shader->isOpaque();
        if (triShader) {
            isOpaque = false;   // unless we want to walk all the colors, and see if they are
                                // all opaque (and the blendmode will keep them that way
        }

        auto blitter = SkCreateRasterPipelineBlitter(fDst, p, pipeline, isOpaque, &outerAlloc);
        while (vertProc(&state)) {
            if (triShader && !triShader->update(ctmInv, vertices, dstColors,
                                                state.f0, state.f1, state.f2)) {
                continue;
            }

            SkMatrix localM;
            if (!texture_to_matrix(state, vertices, textures, &localM) ||
                !updater->update(*fMatrix, &localM)) {
                continue;
            }

            if (dev3) {
                handle_dev3(blitter);
            } else {
                handle_devVerts(blitter);
            }
        }
    } else {
        // must rebuild pipeline for each triangle, to pass in the computed ctm
        while (vertProc(&state)) {
            if (triShader && !triShader->update(ctmInv, vertices, dstColors,
                                                state.f0, state.f1, state.f2)) {
                continue;
            }

            SkSTArenaAlloc<2048> innerAlloc;

            const SkMatrix* ctm = fMatrix;
            SkMatrix tmpCtm;
            if (textures) {
                SkMatrix localM;
                if (!texture_to_matrix(state, vertices, textures, &localM)) {
                    continue;
                }
                tmpCtm = SkMatrix::Concat(*fMatrix, localM);
                ctm = &tmpCtm;
            }

            auto blitter = SkCreateRasterPipelineBlitter(fDst, p, *ctm, &innerAlloc);
            if (dev3) {
                handle_dev3(blitter);
            } else {
                handle_devVerts(blitter);
            }
        }
    }
}
