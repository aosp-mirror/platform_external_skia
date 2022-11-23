/*
 * Copyright 2022 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkArenaAlloc.h"
#include "src/core/SkOpts.h"
#include "src/core/SkRasterPipeline.h"
#include "src/sksl/codegen/SkSLRasterPipelineBuilder.h"
#include "tests/Test.h"

struct TestingOnly_SkRasterPipelineInspector {
    using StageList = SkRasterPipeline::StageList;
    static StageList* GetStageList(SkRasterPipeline* p) { return p->fStages; }
};

static SkSL::RP::SlotRange two_slots_at(SkSL::RP::Slot index) {
    return SkSL::RP::SlotRange{index, 2};
}

static SkSL::RP::SlotRange three_slots_at(SkSL::RP::Slot index) {
    return SkSL::RP::SlotRange{index, 3};
}

static SkSL::RP::SlotRange four_slots_at(SkSL::RP::Slot index) {
    return SkSL::RP::SlotRange{index, 4};
}

static SkSL::RP::SlotRange five_slots_at(SkSL::RP::Slot index) {
    return SkSL::RP::SlotRange{index, 5};
}

template <typename T>
static bool contains_value(void* ctx, T val) {
    static_assert(sizeof(T) <= sizeof(void*));
    return 0 == memcmp(&ctx, &val, sizeof(T));
}

DEF_TEST(RasterPipelineBuilder, r) {
    // Create a very simple nonsense program.
    SkSL::RP::Builder builder;
    builder.store_src_rg(two_slots_at(0));
    builder.store_src(four_slots_at(2));
    builder.store_dst(four_slots_at(6));
    builder.init_lane_masks();
    builder.load_src(four_slots_at(1));
    builder.load_dst(four_slots_at(3));
    std::unique_ptr<SkSL::RP::Program> program = builder.finish();

    // Instantiate this program.
    SkArenaAlloc alloc(/*firstHeapAllocation=*/1000);
    SkRasterPipeline pipeline(&alloc);
    program->appendStages(&pipeline, &alloc);

    // Double check that the resulting stage list contains the correct ops.
    // (Note that the RasterPipeline stage list is stored in backwards order.)
    const auto* stages = TestingOnly_SkRasterPipelineInspector::GetStageList(&pipeline);
    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::load_dst);
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::load_src);
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::init_lane_masks);
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::store_dst);
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::store_src);
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::store_src_rg);

    // Double check that the resulting stage list contains the correct context pointers.
    // All of the ops here hold a pointer directly to their associated slot, and slots are always
    // assigned contiguously and in order, and never rearranged. We should be able to verify that
    // they are all where we expect them to be.
    const auto* firstStage = stages;
    const float* slot0 = (const float*)firstStage->ctx;
    const int N = SkOpts::raster_pipeline_highp_stride;

    stages = TestingOnly_SkRasterPipelineInspector::GetStageList(&pipeline);
    REPORTER_ASSERT(r, stages->ctx == slot0 + (3 * N));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->ctx == slot0 + (1 * N));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->ctx == nullptr);
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->ctx == slot0 + (6 * N));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->ctx == slot0 + (2 * N));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->ctx == slot0 + (0 * N));
}

DEF_TEST(RasterPipelineBuilderImmediate, r) {
    // Create a very simple nonsense program.
    SkSL::RP::Builder builder;
    builder.immediate_f(333.0f);
    builder.immediate_f(0.0f);
    builder.immediate_f(-5555.0f);
    builder.immediate_i(-123);
    builder.immediate_u(456);
    std::unique_ptr<SkSL::RP::Program> program = builder.finish();

    // Instantiate this program.
    SkArenaAlloc alloc(/*firstHeapAllocation=*/1000);
    SkRasterPipeline pipeline(&alloc);
    program->appendStages(&pipeline, &alloc);

    // Double check that the resulting stage list contains the expected immediate values.
    const auto* stages = TestingOnly_SkRasterPipelineInspector::GetStageList(&pipeline);
    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::immediate_f);
    REPORTER_ASSERT(r, contains_value<uint32_t>(stages->ctx, 456));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::immediate_f);
    REPORTER_ASSERT(r, contains_value<int32_t>(stages->ctx, -123));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::immediate_f);
    REPORTER_ASSERT(r, contains_value<float>(stages->ctx, -5555.0f));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::immediate_f);
    REPORTER_ASSERT(r, contains_value<float>(stages->ctx, 0.0f));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::immediate_f);
    REPORTER_ASSERT(r, contains_value<float>(stages->ctx, 333.0f));
}

DEF_TEST(RasterPipelineBuilderLoadStoreAccumulator, r) {
    // Create a very simple nonsense program.
    SkSL::RP::Builder builder;
    builder.load_unmasked(12);
    builder.store_unmasked(34);
    builder.store_unmasked(56);
    builder.store_masked(0);
    std::unique_ptr<SkSL::RP::Program> program = builder.finish();

    // Instantiate this program.
    SkArenaAlloc alloc(/*firstHeapAllocation=*/1000);
    SkRasterPipeline pipeline(&alloc);
    program->appendStages(&pipeline, &alloc);

    // Double check that the resulting stage list contains the expected stores.
    const auto* stages = TestingOnly_SkRasterPipelineInspector::GetStageList(&pipeline);
    const float* slot0 = (const float*)stages->ctx;
    const int N = SkOpts::raster_pipeline_highp_stride;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::store_masked);
    REPORTER_ASSERT(r, stages->ctx == slot0 + (0 * N));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::store_unmasked);
    REPORTER_ASSERT(r, stages->ctx == slot0 + (56 * N));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::store_unmasked);
    REPORTER_ASSERT(r, stages->ctx == slot0 + (34 * N));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::load_unmasked);
    REPORTER_ASSERT(r, stages->ctx == slot0 + (12 * N));
}

DEF_TEST(RasterPipelineBuilderPushPopConditionMask, r) {
    // Create a very simple nonsense program.
    SkSL::RP::Builder builder;
    builder.push_condition_mask(); // push into 100
    builder.push_condition_mask(); // push into 101
    builder.push_condition_mask(); // push into 102
    builder.pop_condition_mask();  // pop  from 102
    builder.push_condition_mask(); // push into 102
    builder.pop_condition_mask();  // pop  from 102
    builder.pop_condition_mask();  // pop  from 101
    builder.pop_condition_mask();  // pop  from 100
    builder.push_condition_mask(); // push into 100
    builder.pop_condition_mask();  // pop  from 100
    builder.push_literal_f(0);     // reserve slot 98 for the temp stack
    builder.push_literal_f(0);     //  "        "  99  "   "   "      "
    builder.discard_stack(2);      // balance temp stack
    builder.store_unmasked(97);    // reserve slots 0-97 for values
    builder.store_unmasked(0);     // make it easy to find the first slot
    std::unique_ptr<SkSL::RP::Program> program = builder.finish();

    // Instantiate this program.
    SkArenaAlloc alloc(/*firstHeapAllocation=*/1000);
    SkRasterPipeline pipeline(&alloc);
    program->appendStages(&pipeline, &alloc);

    // Double check that the resulting stage list contains the expected pushes and pops.
    // (Note that, as always, stage lists are in reverse order.)
    const auto* stages = TestingOnly_SkRasterPipelineInspector::GetStageList(&pipeline);
    const float* slot0 = (const float*)stages->ctx;
    const int N = SkOpts::raster_pipeline_highp_stride;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::store_unmasked);
    REPORTER_ASSERT(r, stages->ctx == slot0 + (0 * N));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::store_unmasked);
    REPORTER_ASSERT(r, stages->ctx == slot0 + (97 * N));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::zero_slot_unmasked);
    REPORTER_ASSERT(r, stages->ctx == slot0 + (99 * N));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::zero_slot_unmasked);
    REPORTER_ASSERT(r, stages->ctx == slot0 + (98 * N));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::load_condition_mask);
    REPORTER_ASSERT(r, stages->ctx == slot0 + (100 * N));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::store_condition_mask);
    REPORTER_ASSERT(r, stages->ctx == slot0 + (100 * N));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::load_condition_mask);
    REPORTER_ASSERT(r, stages->ctx == slot0 + (100 * N));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::load_condition_mask);
    REPORTER_ASSERT(r, stages->ctx == slot0 + (101 * N));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::load_condition_mask);
    REPORTER_ASSERT(r, stages->ctx == slot0 + (102 * N));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::store_condition_mask);
    REPORTER_ASSERT(r, stages->ctx == slot0 + (102 * N));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::load_condition_mask);
    REPORTER_ASSERT(r, stages->ctx == slot0 + (102 * N));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::store_condition_mask);
    REPORTER_ASSERT(r, stages->ctx == slot0 + (102 * N));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::store_condition_mask);
    REPORTER_ASSERT(r, stages->ctx == slot0 + (101 * N));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::store_condition_mask);
    REPORTER_ASSERT(r, stages->ctx == slot0 + (100 * N));
}

DEF_TEST(RasterPipelineBuilderPushPopTempImmediates, r) {
    // Create a very simple nonsense program.
    SkSL::RP::Builder builder;
    builder.push_literal_f(13.5f); // push into 1
    builder.push_literal_i(-246);  // push into 2
    builder.discard_stack();       // discard 2
    builder.push_literal_u(357);   // push into 2
    builder.discard_stack(2);      // discard 1 and 2
    builder.load_unmasked(0);      // make it easy to find the first slot
    std::unique_ptr<SkSL::RP::Program> program = builder.finish();

    // Instantiate this program.
    SkArenaAlloc alloc(/*firstHeapAllocation=*/1000);
    SkRasterPipeline pipeline(&alloc);
    program->appendStages(&pipeline, &alloc);

    // Double check that the resulting stage list contains the expected temp-value pushes.
    // `discard_stack` isn't in the list because it doesn't create any ops.
    // (Note that, as always, stage lists are in reverse order.)
    const auto* stages = TestingOnly_SkRasterPipelineInspector::GetStageList(&pipeline);
    const float* slot0 = (const float*)stages->ctx;
    const int N = SkOpts::raster_pipeline_highp_stride;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::load_unmasked);
    REPORTER_ASSERT(r, stages->ctx == slot0 + (0 * N));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::store_unmasked);
    REPORTER_ASSERT(r, stages->ctx == slot0 + (2 * N));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::immediate_f);
    REPORTER_ASSERT(r, contains_value<uint32_t>(stages->ctx, 357));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::store_unmasked);
    REPORTER_ASSERT(r, stages->ctx == slot0 + (2 * N));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::immediate_f);
    REPORTER_ASSERT(r, contains_value<int32_t>(stages->ctx, -246));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::store_unmasked);
    REPORTER_ASSERT(r, stages->ctx == slot0 + (1 * N));
    stages = stages->prev;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::immediate_f);
    REPORTER_ASSERT(r, contains_value<float>(stages->ctx, 13.5f));
}

DEF_TEST(RasterPipelineBuilderCopySlotsMasked, r) {
    // Create a very simple nonsense program.
    SkSL::RP::Builder builder;
    builder.copy_slots_masked(two_slots_at(0),  two_slots_at(2));
    builder.copy_slots_masked(four_slots_at(1), four_slots_at(5));
    builder.load_unmasked(0);  // make it easy to find the first slot
    std::unique_ptr<SkSL::RP::Program> program = builder.finish();

    // Instantiate this program.
    SkArenaAlloc alloc(/*firstHeapAllocation=*/1000);
    SkRasterPipeline pipeline(&alloc);
    program->appendStages(&pipeline, &alloc);

    // Double check that the resulting stage list contains the expected stores.
    const auto* stages = TestingOnly_SkRasterPipelineInspector::GetStageList(&pipeline);
    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::load_unmasked);
    const float* slot0 = (const float*)stages->ctx;
    const int N = SkOpts::raster_pipeline_highp_stride;

    stages = stages->prev;
    const auto* ctx = static_cast<const SkRasterPipeline_CopySlotsCtx*>(stages->ctx);

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::copy_4_slots_masked);
    REPORTER_ASSERT(r, ctx->dst == slot0 + (1 * N));
    REPORTER_ASSERT(r, ctx->src == slot0 + (5 * N));
    stages = stages->prev;
    ctx = static_cast<const SkRasterPipeline_CopySlotsCtx*>(stages->ctx);

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::copy_2_slots_masked);
    REPORTER_ASSERT(r, ctx->dst == slot0 + (0 * N));
    REPORTER_ASSERT(r, ctx->src == slot0 + (2 * N));
}

DEF_TEST(RasterPipelineBuilderCopySlotsUnmasked, r) {
    // Create a very simple nonsense program.
    SkSL::RP::Builder builder;
    builder.copy_slots_unmasked(three_slots_at(0),  three_slots_at(2));
    builder.copy_slots_unmasked(five_slots_at(1), five_slots_at(5));
    builder.store_unmasked(0);  // make it easy to find the first slot
    std::unique_ptr<SkSL::RP::Program> program = builder.finish();

    // Instantiate this program.
    SkArenaAlloc alloc(/*firstHeapAllocation=*/1000);
    SkRasterPipeline pipeline(&alloc);
    program->appendStages(&pipeline, &alloc);

    // Double check that the resulting stage list contains the expected stores.
    const auto* stages = TestingOnly_SkRasterPipelineInspector::GetStageList(&pipeline);
    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::store_unmasked);
    const float* slot0 = (const float*)stages->ctx;
    const int N = SkOpts::raster_pipeline_highp_stride;

    stages = stages->prev;
    const auto* ctx = static_cast<const SkRasterPipeline_CopySlotsCtx*>(stages->ctx);

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::copy_slot_unmasked);
    REPORTER_ASSERT(r, ctx->dst == slot0 + (5 * N));
    REPORTER_ASSERT(r, ctx->src == slot0 + (9 * N));
    stages = stages->prev;
    ctx = static_cast<const SkRasterPipeline_CopySlotsCtx*>(stages->ctx);

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::copy_4_slots_unmasked);
    REPORTER_ASSERT(r, ctx->dst == slot0 + (1 * N));
    REPORTER_ASSERT(r, ctx->src == slot0 + (5 * N));
    stages = stages->prev;
    ctx = static_cast<const SkRasterPipeline_CopySlotsCtx*>(stages->ctx);

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::copy_3_slots_unmasked);
    REPORTER_ASSERT(r, ctx->dst == slot0 + (0 * N));
    REPORTER_ASSERT(r, ctx->src == slot0 + (2 * N));
}

DEF_TEST(RasterPipelineBuilderPushPopSlots, r) {
    // Create a very simple nonsense program.
    SkSL::RP::Builder builder;
    builder.load_unmasked(49);              // dedicate slots 0-49 for values
    builder.push_slots(four_slots_at(10));  // push from 10~13 into 50~53
    builder.pop_slots(two_slots_at(20));    // pop from 52~53 into 20~21
    builder.push_slots(three_slots_at(30)); // push from 30~32 into 52~54
    builder.pop_slots(five_slots_at(0));    // pop from 50~54 into 0~4
    builder.load_unmasked(0);               // make it easy to find the first slot
    std::unique_ptr<SkSL::RP::Program> program = builder.finish();

    // Instantiate this program.
    SkArenaAlloc alloc(/*firstHeapAllocation=*/1000);
    SkRasterPipeline pipeline(&alloc);
    program->appendStages(&pipeline, &alloc);

    // Double check that the resulting stage list contains the expected pushes and pops, represented
    // as copy-slots. (Note that, as always, stage lists are in reverse order.)
    const auto* stages = TestingOnly_SkRasterPipelineInspector::GetStageList(&pipeline);
    const float* slot0 = (const float*)stages->ctx;
    const int N = SkOpts::raster_pipeline_highp_stride;

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::load_unmasked);
    REPORTER_ASSERT(r, stages->ctx == slot0 + (0 * N));
    stages = stages->prev;
    const auto* ctx = static_cast<const SkRasterPipeline_CopySlotsCtx*>(stages->ctx);

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::copy_slot_masked);
    REPORTER_ASSERT(r, ctx->src == slot0 + (54 * N));
    REPORTER_ASSERT(r, ctx->dst == slot0 + (4 * N));
    stages = stages->prev;
    ctx = static_cast<const SkRasterPipeline_CopySlotsCtx*>(stages->ctx);

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::copy_4_slots_masked);
    REPORTER_ASSERT(r, ctx->src == slot0 + (50 * N));
    REPORTER_ASSERT(r, ctx->dst == slot0 + (0 * N));
    stages = stages->prev;
    ctx = static_cast<const SkRasterPipeline_CopySlotsCtx*>(stages->ctx);

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::copy_3_slots_unmasked);
    REPORTER_ASSERT(r, ctx->src == slot0 + (30 * N));
    REPORTER_ASSERT(r, ctx->dst == slot0 + (52 * N));
    stages = stages->prev;
    ctx = static_cast<const SkRasterPipeline_CopySlotsCtx*>(stages->ctx);

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::copy_2_slots_masked);
    REPORTER_ASSERT(r, ctx->src == slot0 + (52 * N));
    REPORTER_ASSERT(r, ctx->dst == slot0 + (20 * N));
    stages = stages->prev;
    ctx = static_cast<const SkRasterPipeline_CopySlotsCtx*>(stages->ctx);

    REPORTER_ASSERT(r, stages->stage == SkRasterPipeline::copy_4_slots_unmasked);
    REPORTER_ASSERT(r, ctx->src == slot0 + (10 * N));
    REPORTER_ASSERT(r, ctx->dst == slot0 + (50 * N));
}
