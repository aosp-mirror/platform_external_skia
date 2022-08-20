/*
 * Copyright 2022 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef skgpu_graphite_MtlQueueManager_DEFINED
#define skgpu_graphite_MtlQueueManager_DEFINED

#include "src/gpu/graphite/QueueManager.h"

namespace skgpu::graphite {

class MtlSharedContext;
class SharedContext;

class MtlQueueManager : public QueueManager {
public:
    MtlQueueManager(const SharedContext*);
    ~MtlQueueManager() override {}

private:
    const MtlSharedContext* mtlSharedContext() const;

    sk_sp<CommandBuffer> getNewCommandBuffer() override;
    OutstandingSubmission onSubmitToGpu() override;
};

} // namespace skgpu::graphite

#endif // skgpu_graphite_MtlQueueManager_DEFINED
