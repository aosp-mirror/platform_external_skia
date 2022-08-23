/*
 * Copyright 2021 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tests/Test.h"

#include "include/gpu/graphite/BackendTexture.h"
#include "include/gpu/graphite/Context.h"
#include "include/gpu/graphite/Recorder.h"
#include "include/gpu/graphite/SkStuff.h"
#include "src/gpu/graphite/Caps.h"
#include "src/gpu/graphite/ContextPriv.h"
#include "src/gpu/graphite/ResourceTypes.h"

#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"

using namespace skgpu::graphite;

namespace {
    const SkISize kSize = {16, 16};
}

DEF_GRAPHITE_TEST_FOR_CONTEXTS(BackendTextureTest, reporter, context) {
    auto caps = context->priv().caps();
    auto recorder = context->makeRecorder();

    TextureInfo info = caps->getDefaultSampledTextureInfo(kRGBA_8888_SkColorType,
                                                          /*levelCount=*/1,
                                                          Protected::kNo,
                                                          Renderable::kNo);
    REPORTER_ASSERT(reporter, info.isValid());

    auto texture1 = recorder->createBackendTexture(kSize, info);
    REPORTER_ASSERT(reporter, texture1.isValid());

    // We make a copy to do the remaining tests so we still have texture1 to safely delete the
    // backend object.
    auto texture1Copy = texture1;
    REPORTER_ASSERT(reporter, texture1Copy.isValid());
    REPORTER_ASSERT(reporter, texture1 == texture1Copy);

    auto texture2 = recorder->createBackendTexture(kSize, info);
    REPORTER_ASSERT(reporter, texture2.isValid());

    REPORTER_ASSERT(reporter, texture1Copy != texture2);

    // Test state after assignment
    texture1Copy = texture2;
    REPORTER_ASSERT(reporter, texture1Copy.isValid());
    REPORTER_ASSERT(reporter, texture1Copy == texture2);

    BackendTexture invalidTexture;
    REPORTER_ASSERT(reporter, !invalidTexture.isValid());

    texture1Copy = invalidTexture;
    REPORTER_ASSERT(reporter, !texture1Copy.isValid());

    texture1Copy = texture1;
    REPORTER_ASSERT(reporter, texture1Copy.isValid());
    REPORTER_ASSERT(reporter, texture1 == texture1Copy);

    recorder->deleteBackendTexture(texture1);
    recorder->deleteBackendTexture(texture2);

    // Test that deleting is safe from the Context or a different Recorder.
    texture1 = recorder->createBackendTexture(kSize, info);
    context->deleteBackendTexture(texture1);

    auto recorder2 = context->makeRecorder();
    texture1 = recorder->createBackendTexture(kSize, info);
    recorder2->deleteBackendTexture(texture1);
}

// Tests the wrapping of a BackendTexture in an SkSurface
DEF_GRAPHITE_TEST_FOR_CONTEXTS(SurfaceBackendTextureTest, reporter, context) {
    // TODO: Right now this just tests very basic combinations of surfaces. This should be expanded
    // to conver a much broader set of things once we add more support in Graphite for different
    // formats, color types, etc.

    auto caps = context->priv().caps();
    std::unique_ptr<Recorder> recorder = context->makeRecorder();

    TextureInfo info = caps->getDefaultSampledTextureInfo(kRGBA_8888_SkColorType,
                                                          /*levelCount=*/1,
                                                          Protected::kNo,
                                                          Renderable::kYes);

    auto texture = recorder->createBackendTexture(kSize, info);
    REPORTER_ASSERT(reporter, texture.isValid());

    sk_sp<SkSurface> surface = MakeGraphiteFromBackendTexture(recorder.get(),
                                                              texture,
                                                              kRGBA_8888_SkColorType,
                                                              /*colorSpace=*/nullptr,
                                                              /*props=*/nullptr);
    REPORTER_ASSERT(reporter, surface);

    surface.reset();

    // We should fail when trying to wrap the same texture in a surface with a non compatible
    // color type.
    surface = MakeGraphiteFromBackendTexture(recorder.get(),
                                             texture,
                                             kAlpha_8_SkColorType,
                                             /*colorSpace=*/nullptr,
                                             /*props=*/nullptr);
    REPORTER_ASSERT(reporter, !surface);

    recorder->deleteBackendTexture(texture);

    // We should fail to make a wrap non renderable texture in a surface.
    info = caps->getDefaultSampledTextureInfo(kRGBA_8888_SkColorType,
                                              /*levelCount=*/1,
                                              Protected::kNo,
                                              Renderable::kNo);
    texture = recorder->createBackendTexture(kSize, info);
    REPORTER_ASSERT(reporter, texture.isValid());

    surface = MakeGraphiteFromBackendTexture(recorder.get(),
                                             texture,
                                             kRGBA_8888_SkColorType,
                                             /*colorSpace=*/nullptr,
                                             /*props=*/nullptr);

    REPORTER_ASSERT(reporter, !surface);
    recorder->deleteBackendTexture(texture);
}

