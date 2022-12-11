/*
 * Copyright 2022 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkStream.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkOpts.h"
#include "src/core/SkRasterPipeline.h"
#include "src/sksl/codegen/SkSLRasterPipelineBuilder.h"
#include "tests/Test.h"

static void check(skiatest::Reporter* r, SkSL::RP::Program& program, std::string_view expected) {
    // Verify that the program matches expectations.
    SkDynamicMemoryWStream stream;
    program.dump(&stream);
    sk_sp<SkData> out = stream.detachAsData();
    std::string_view actual(static_cast<const char*>(out->data()), out->size());
    REPORTER_ASSERT(r, actual == expected, "Output did not match expectation:\n%.*s",
                    (int)actual.size(), actual.data());
}

static SkSL::RP::SlotRange one_slot_at(SkSL::RP::Slot index) {
    return SkSL::RP::SlotRange{index, 1};
}

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

DEF_TEST(RasterPipelineBuilder, r) {
    // Create a very simple nonsense program.
    SkSL::RP::Builder builder;
    builder.store_src_rg(two_slots_at(0));
    builder.store_src(four_slots_at(2));
    builder.store_dst(four_slots_at(6));
    builder.init_lane_masks();
    builder.mask_off_return_mask();
    builder.mask_off_loop_mask();
    builder.reenable_loop_mask(one_slot_at(4));
    builder.load_src(four_slots_at(1));
    builder.load_dst(four_slots_at(3));
    std::unique_ptr<SkSL::RP::Program> program = builder.finish(/*numValueSlots=*/10);

    check(r, *program,
R"(    1. store_src_rg                   v0..1 = src.rg
    2. store_src                      v2..5 = src.rgba
    3. store_dst                      v6..9 = dst.rgba
    4. init_lane_masks                CondMask = LoopMask = RetMask = true
    5. mask_off_return_mask           RetMask &= ~(CondMask & LoopMask & RetMask)
    6. mask_off_loop_mask             LoopMask &= ~(CondMask & LoopMask & RetMask)
    7. reenable_loop_mask             LoopMask |= v4
    8. load_src                       src.rgba = v1..4
    9. load_dst                       dst.rgba = v3..6
)");
}

DEF_TEST(RasterPipelineBuilderImmediate, r) {
    // Create a very simple nonsense program.
    SkSL::RP::Builder builder;
    builder.immediate_f(333.0f);
    builder.immediate_f(0.0f);
    builder.immediate_f(-5555.0f);
    builder.immediate_i(-123);
    builder.immediate_u(456);
    std::unique_ptr<SkSL::RP::Program> program = builder.finish(/*numValueSlots=*/0);

    check(r, *program,
R"(    1. immediate_f                    src.r = 0x43A68000 (333.0)
    2. immediate_f                    src.r = 0x00000000 (0.0)
    3. immediate_f                    src.r = 0xC5AD9800 (-5555.0)
    4. immediate_f                    src.r = 0xFFFFFF85
    5. immediate_f                    src.r = 0x000001C8 (6.389921e-43)
)");
}

DEF_TEST(RasterPipelineBuilderLoadStoreAccumulator, r) {
    // Create a very simple nonsense program.
    SkSL::RP::Builder builder;
    builder.load_unmasked(12);
    builder.store_unmasked(34);
    builder.store_unmasked(56);
    builder.store_masked(0);
    std::unique_ptr<SkSL::RP::Program> program = builder.finish(/*numValueSlots=*/57);

    check(r, *program,
R"(    1. load_unmasked                  src.r = v12
    2. store_unmasked                 v34 = src.r
    3. store_unmasked                 v56 = src.r
    4. store_masked                   v0 = Mask(src.r)
)");
}

DEF_TEST(RasterPipelineBuilderPushPopMaskRegisters, r) {
    // Create a very simple nonsense program.
    SkSL::RP::Builder builder;
    builder.push_condition_mask();  // push into 0
    builder.push_loop_mask();       // push into 1
    builder.push_return_mask();     // push into 2
    builder.merge_condition_mask(); // set the condition-mask to 1 & 2
    builder.pop_condition_mask();   // pop from 2
    builder.merge_loop_mask();      // mask off the loop-mask against 1
    builder.push_condition_mask();  // push into 2
    builder.pop_condition_mask();   // pop from 2
    builder.pop_loop_mask();        // pop from 1
    builder.pop_return_mask();      // pop from 0
    builder.push_condition_mask();  // push into 0
    builder.pop_condition_mask();   // pop from 0
    std::unique_ptr<SkSL::RP::Program> program = builder.finish(/*numValueSlots=*/0);

    check(r, *program,
R"(    1. store_condition_mask           $0 = CondMask
    2. store_loop_mask                $1 = LoopMask
    3. store_return_mask              $2 = RetMask
    4. merge_condition_mask           CondMask = $1 & $2
    5. load_condition_mask            CondMask = $2
    6. merge_loop_mask                LoopMask &= $1
    7. store_condition_mask           $2 = CondMask
    8. load_condition_mask            CondMask = $2
    9. load_loop_mask                 LoopMask = $1
   10. load_return_mask               RetMask = $0
   11. store_condition_mask           $0 = CondMask
   12. load_condition_mask            CondMask = $0
)");
}

DEF_TEST(RasterPipelineBuilderPushPopTempImmediates, r) {
    // Create a very simple nonsense program.
    SkSL::RP::Builder builder;
    builder.set_current_stack(1);
    builder.push_literal_i(999);   // push into 2
    builder.set_current_stack(0);
    builder.push_literal_f(13.5f); // push into 0
    builder.push_literal_i(-246);  // push into 1
    builder.discard_stack();       // discard 2
    builder.push_literal_u(357);   // push into 2
    builder.set_current_stack(1);
    builder.push_literal_i(999);   // push into 3
    builder.discard_stack(2);      // discard 2 and 3
    builder.set_current_stack(0);
    builder.discard_stack(2);      // discard 0 and 1
    std::unique_ptr<SkSL::RP::Program> program = builder.finish(/*numValueSlots=*/1);

    check(r, *program,
R"(    1. immediate_f                    src.r = 0x000003E7 (1.399897e-42)
    2. store_unmasked                 $2 = src.r
    3. immediate_f                    src.r = 0x41580000 (13.5)
    4. store_unmasked                 $0 = src.r
    5. immediate_f                    src.r = 0xFFFFFF0A
    6. store_unmasked                 $1 = src.r
    7. immediate_f                    src.r = 0x00000165 (5.002636e-43)
    8. store_unmasked                 $1 = src.r
    9. immediate_f                    src.r = 0x000003E7 (1.399897e-42)
   10. store_unmasked                 $3 = src.r
)");
}

DEF_TEST(RasterPipelineBuilderCopySlotsMasked, r) {
    // Create a very simple nonsense program.
    SkSL::RP::Builder builder;
    builder.copy_slots_masked(two_slots_at(0),  two_slots_at(2));
    builder.copy_slots_masked(four_slots_at(1), four_slots_at(5));
    std::unique_ptr<SkSL::RP::Program> program = builder.finish(/*numValueSlots=*/9);

    check(r, *program,
R"(    1. copy_2_slots_masked            v0..1 = Mask(v2..3)
    2. copy_4_slots_masked            v1..4 = Mask(v5..8)
)");
}

DEF_TEST(RasterPipelineBuilderCopySlotsUnmasked, r) {
    // Create a very simple nonsense program.
    SkSL::RP::Builder builder;
    builder.copy_slots_unmasked(three_slots_at(0),  three_slots_at(2));
    builder.copy_slots_unmasked(five_slots_at(1), five_slots_at(5));
    std::unique_ptr<SkSL::RP::Program> program = builder.finish(/*numValueSlots=*/10);

    check(r, *program,
R"(    1. copy_3_slots_unmasked          v0..2 = v2..4
    2. copy_4_slots_unmasked          v1..4 = v5..8
    3. copy_slot_unmasked             v5 = v9
)");
}

DEF_TEST(RasterPipelineBuilderPushPopSlots, r) {
    // Create a very simple nonsense program.
    SkSL::RP::Builder builder;
    builder.push_slots(four_slots_at(10));           // push from 10~13 into 50~53
    builder.pop_slots_unmasked(two_slots_at(20));    // pop from 52~53 into 20~21 (unmasked)
    builder.push_slots(three_slots_at(30));          // push from 30~32 into 52~54
    builder.pop_slots(five_slots_at(0));             // pop from 50~54 into 0~4 (masked)
    std::unique_ptr<SkSL::RP::Program> program = builder.finish(/*numValueSlots=*/50);

    check(r, *program,
R"(    1. copy_4_slots_unmasked          $0..3 = v10..13
    2. copy_2_slots_unmasked          v20..21 = $2..3
    3. copy_3_slots_unmasked          $2..4 = v30..32
    4. copy_4_slots_masked            v0..3 = Mask($0..3)
    5. copy_slot_masked               v4 = Mask($4)
)");
}

DEF_TEST(RasterPipelineBuilderDuplicateAndSelectSlots, r) {
    // Create a very simple nonsense program.
    SkSL::RP::Builder builder;
    builder.push_literal_f(1.0f);           // push into 1
    builder.duplicate(3);                   // duplicate into 2~4
    builder.select(2);                      // combine 1~2 and 3~4 into 1~2
    builder.select(1);                      // combine 1 and 2 into 1
    builder.discard_stack(1);               // balance stack
    std::unique_ptr<SkSL::RP::Program> program = builder.finish(/*numValueSlots=*/1);

    check(r, *program,
R"(    1. immediate_f                    src.r = 0x3F800000 (1.0)
    2. store_unmasked                 $0 = src.r
    3. load_unmasked                  src.r = $0
    4. store_unmasked                 $1 = src.r
    5. store_unmasked                 $2 = src.r
    6. store_unmasked                 $3 = src.r
    7. copy_2_slots_masked            $0..1 = Mask($2..3)
    8. copy_slot_masked               $0 = Mask($1)
)");
}

DEF_TEST(RasterPipelineBuilderBranches, r) {
    // Create a very simple nonsense program.
    SkSL::RP::Builder builder;
    int label1 = builder.nextLabelID();
    int label2 = builder.nextLabelID();
    int label3 = builder.nextLabelID();

    builder.jump(label3);
    builder.label(label1);
    builder.immediate_f(1.0f);
    builder.label(label2);
    builder.immediate_f(2.0f);
    builder.branch_if_no_active_lanes(label2);
    builder.label(label3);
    builder.immediate_f(3.0f);
    builder.branch_if_any_active_lanes(label1);

    std::unique_ptr<SkSL::RP::Program> program = builder.finish(/*numValueSlots=*/1);

    check(r, *program,
R"(    1. jump                           jump +5 (#6)
    2. immediate_f                    src.r = 0x3F800000 (1.0)
    3. immediate_f                    src.r = 0x40000000 (2.0)
    4. stack_rewind
    5. branch_if_no_active_lanes      branch_if_no_active_lanes -2 (#3)
    6. immediate_f                    src.r = 0x40400000 (3.0)
    7. stack_rewind
    8. branch_if_any_active_lanes     branch_if_any_active_lanes -6 (#2)
)");
}

DEF_TEST(RasterPipelineBuilderUnaryAndBinaryOps, r) {
    using BuilderOp = SkSL::RP::BuilderOp;

    // Create a very simple nonsense program.
    SkSL::RP::Builder builder;
    builder.push_literal_f(0.0f);                  // push into 0
    builder.push_literal_f(1.0f);                  // push into 1
    builder.push_literal_f(2.0f);                  // push into 2
    builder.push_literal_f(3.0f);                  // push into 3
    builder.push_literal_f(4.0f);                  // push into 4
    builder.binary_op(BuilderOp::add_n_floats, 2); // compute (1,2)+(3,4) and store into 1~2
    builder.binary_op(BuilderOp::mul_n_floats, 1); // compute 1*2 and store into 1
    builder.push_literal_i(5);                     // push into 2
    builder.push_literal_i(6);                     // push into 3
    builder.push_literal_i(7);                     // push into 4
    builder.push_literal_i(8);                     // push into 5
    builder.push_literal_i(9);                     // push into 6
    builder.push_literal_i(10);                    // push into 7
    builder.binary_op(BuilderOp::div_n_floats, 3); // compute (2,3,4)/(5,6,7) and store into 2~4
    builder.binary_op(BuilderOp::sub_n_ints, 1);   // compute 3-4 and store into 3
    builder.binary_op(BuilderOp::bitwise_and, 1);  // compute 2&11 and store into 2
    builder.binary_op(BuilderOp::bitwise_xor, 1);  // compute 1^2 and store into 1
    builder.unary_op(BuilderOp::bitwise_not, 1);   // compute ~3 and store into 1
    builder.discard_stack(2);                      // balance stack
    std::unique_ptr<SkSL::RP::Program> program = builder.finish(/*numValueSlots=*/0);

    check(r, *program,
R"(    1. zero_slot_unmasked             $0 = 0
    2. immediate_f                    src.r = 0x3F800000 (1.0)
    3. store_unmasked                 $1 = src.r
    4. immediate_f                    src.r = 0x40000000 (2.0)
    5. store_unmasked                 $2 = src.r
    6. immediate_f                    src.r = 0x40400000 (3.0)
    7. store_unmasked                 $3 = src.r
    8. immediate_f                    src.r = 0x40800000 (4.0)
    9. store_unmasked                 $4 = src.r
   10. add_2_floats                   $1..2 += $3..4
   11. mul_float                      $1 *= $2
   12. immediate_f                    src.r = 0x00000005 (7.006492e-45)
   13. store_unmasked                 $2 = src.r
   14. immediate_f                    src.r = 0x00000006 (8.407791e-45)
   15. store_unmasked                 $3 = src.r
   16. immediate_f                    src.r = 0x00000007 (9.809089e-45)
   17. store_unmasked                 $4 = src.r
   18. immediate_f                    src.r = 0x00000008 (1.121039e-44)
   19. store_unmasked                 $5 = src.r
   20. immediate_f                    src.r = 0x00000009 (1.261169e-44)
   21. store_unmasked                 $6 = src.r
   22. immediate_f                    src.r = 0x0000000A (1.401298e-44)
   23. store_unmasked                 $7 = src.r
   24. div_3_floats                   $2..4 /= $5..7
   25. sub_int                        $3 -= $4
   26. bitwise_and                    $2 &= $3
   27. bitwise_xor                    $1 ^= $2
   28. bitwise_not                    $1 = ~$1
)");
}
