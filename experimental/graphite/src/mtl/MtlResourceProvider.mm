/*
 * Copyright 2021 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "experimental/graphite/src/mtl/MtlResourceProvider.h"

#include "experimental/graphite/src/mtl/MtlCommandBuffer.h"
#include "experimental/graphite/src/mtl/MtlGpu.h"

namespace skgpu::mtl {

ResourceProvider::ResourceProvider(const Gpu* gpu)
    : fGpu(gpu) {
}

std::unique_ptr<skgpu::CommandBuffer> ResourceProvider::createCommandBuffer() {
    return CommandBuffer::Make(fGpu->queue());
}

} // namespace skgpu::mtl
