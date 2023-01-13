/*
 * Copyright 2022 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkStream.h"
#include "include/private/SkSLString.h"
#include "include/private/base/SkMalloc.h"
#include "include/private/base/SkTo.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkOpts.h"
#include "src/sksl/codegen/SkSLRasterPipelineBuilder.h"
#include "src/sksl/tracing/SkRPDebugTrace.h"
#include "src/sksl/tracing/SkSLDebugInfo.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace SkSL {
namespace RP {

using SkRP = SkRasterPipeline;

#define ALL_MULTI_SLOT_UNARY_OP_CASES        \
         BuilderOp::abs_float:               \
    case BuilderOp::abs_int:                 \
    case BuilderOp::bitwise_not_int:         \
    case BuilderOp::cast_to_float_from_int:  \
    case BuilderOp::cast_to_float_from_uint: \
    case BuilderOp::cast_to_int_from_float:  \
    case BuilderOp::cast_to_uint_from_float: \
    case BuilderOp::ceil_float:              \
    case BuilderOp::floor_float              \

#define ALL_MULTI_SLOT_BINARY_OP_CASES  \
         BuilderOp::add_n_floats:       \
    case BuilderOp::add_n_ints:         \
    case BuilderOp::sub_n_floats:       \
    case BuilderOp::sub_n_ints:         \
    case BuilderOp::mul_n_floats:       \
    case BuilderOp::mul_n_ints:         \
    case BuilderOp::div_n_floats:       \
    case BuilderOp::div_n_ints:         \
    case BuilderOp::div_n_uints:        \
    case BuilderOp::bitwise_and_n_ints: \
    case BuilderOp::bitwise_or_n_ints:  \
    case BuilderOp::bitwise_xor_n_ints: \
    case BuilderOp::min_n_floats:       \
    case BuilderOp::min_n_ints:         \
    case BuilderOp::min_n_uints:        \
    case BuilderOp::max_n_floats:       \
    case BuilderOp::max_n_ints:         \
    case BuilderOp::max_n_uints:        \
    case BuilderOp::cmple_n_floats:     \
    case BuilderOp::cmple_n_ints:       \
    case BuilderOp::cmple_n_uints:      \
    case BuilderOp::cmplt_n_floats:     \
    case BuilderOp::cmplt_n_ints:       \
    case BuilderOp::cmplt_n_uints:      \
    case BuilderOp::cmpeq_n_floats:     \
    case BuilderOp::cmpeq_n_ints:       \
    case BuilderOp::cmpne_n_floats:     \
    case BuilderOp::cmpne_n_ints

#define ALL_MULTI_SLOT_TERNARY_OP_CASES \
         BuilderOp::mix_n_floats

void Builder::unary_op(BuilderOp op, int32_t slots) {
    switch (op) {
        case ALL_MULTI_SLOT_UNARY_OP_CASES:
            fInstructions.push_back({op, {}, slots});
            break;

        default:
            SkDEBUGFAIL("not a unary op");
            break;
    }
}

void Builder::binary_op(BuilderOp op, int32_t slots) {
    switch (op) {
        case ALL_MULTI_SLOT_BINARY_OP_CASES:
            fInstructions.push_back({op, {}, slots});
            break;

        default:
            SkDEBUGFAIL("not a binary op");
            break;
    }
}

void Builder::ternary_op(BuilderOp op, int32_t slots) {
    switch (op) {
        case ALL_MULTI_SLOT_TERNARY_OP_CASES:
            fInstructions.push_back({op, {}, slots});
            break;

        default:
            SkDEBUGFAIL("not a ternary op");
            break;
    }
}

void Builder::push_duplicates(int count) {
    SkASSERT(count >= 0);
    if (count >= 3) {
        // Use a swizzle to splat the input into a 4-slot value.
        this->swizzle(/*inputSlots=*/1, {0, 0, 0, 0});
        count -= 3;
    }
    for (; count >= 4; count -= 4) {
        // Clone the splatted value four slots at a time.
        this->push_clone(/*numSlots=*/4);
    }
    // Use a swizzle or clone to handle the trailing items.
    switch (count) {
        case 3:  this->swizzle(/*inputSlots=*/1, {0, 0, 0, 0}); break;
        case 2:  this->swizzle(/*inputSlots=*/1, {0, 0, 0});    break;
        case 1:  this->push_clone(/*numSlots=*/1);              break;
        default: break;
    }
}

void Builder::swizzle(int inputSlots, SkSpan<const int8_t> components) {
    // Consumes `inputSlots` elements on the stack, then generates `components.size()` elements.
    SkASSERT(components.size() >= 1 && components.size() <= 4);
    // Squash .xwww into 0x3330, or .zyx into 0x012. (Packed nybbles, in reverse order.)
    int componentBits = 0;
    for (auto iter = components.rbegin(); iter != components.rend(); ++iter) {
        SkASSERT(*iter >= 0 && *iter < inputSlots);
        componentBits <<= 4;
        componentBits |= *iter;
    }

    int op = (int)BuilderOp::swizzle_1 + components.size() - 1;
    fInstructions.push_back({(BuilderOp)op, {}, inputSlots, componentBits});
}

std::unique_ptr<Program> Builder::finish(int numValueSlots,
                                         int numUniformSlots,
                                         SkRPDebugTrace* debugTrace) {
    return std::make_unique<Program>(std::move(fInstructions), numValueSlots, numUniformSlots,
                                     fNumLabels, fNumBranches, debugTrace);
}

void Program::optimize() {
    // TODO(johnstiles): perform any last-minute cleanup of the instruction stream here
}

static int stack_usage(const Instruction& inst) {
    switch (inst.fOp) {
        case BuilderOp::push_literal_f:
        case BuilderOp::push_condition_mask:
        case BuilderOp::push_loop_mask:
        case BuilderOp::push_return_mask:
            return 1;

        case BuilderOp::push_slots:
        case BuilderOp::push_uniform:
        case BuilderOp::push_zeros:
        case BuilderOp::push_clone:
        case BuilderOp::push_clone_from_stack:
            return inst.fImmA;

        case BuilderOp::pop_condition_mask:
        case BuilderOp::pop_loop_mask:
        case BuilderOp::pop_return_mask:
            return -1;

        case ALL_MULTI_SLOT_BINARY_OP_CASES:
        case BuilderOp::discard_stack:
        case BuilderOp::select:
            return -inst.fImmA;

        case ALL_MULTI_SLOT_TERNARY_OP_CASES:
            return 2 * -inst.fImmA;

        case BuilderOp::swizzle_1:
            return 1 - inst.fImmA;
        case BuilderOp::swizzle_2:
            return 2 - inst.fImmA;
        case BuilderOp::swizzle_3:
            return 3 - inst.fImmA;
        case BuilderOp::swizzle_4:
            return 4 - inst.fImmA;

        case ALL_MULTI_SLOT_UNARY_OP_CASES:
        default:
            return 0;
    }
}

Program::StackDepthMap Program::tempStackMaxDepths() {
    StackDepthMap largest;
    StackDepthMap current;

    int curIdx = 0;
    for (const Instruction& inst : fInstructions) {
        if (inst.fOp == BuilderOp::set_current_stack) {
            curIdx = inst.fImmA;
        }
        current[curIdx] += stack_usage(inst);
        largest[curIdx] = std::max(current[curIdx], largest[curIdx]);
        SkASSERTF(current[curIdx] >= 0, "unbalanced temp stack push/pop on stack %d", curIdx);
    }

    for (const auto& [stackIdx, depth] : current) {
        (void)stackIdx;
        SkASSERTF(depth == 0, "unbalanced temp stack push/pop");
    }

    return largest;
}

Program::Program(SkTArray<Instruction> instrs,
                 int numValueSlots,
                 int numUniformSlots,
                 int numLabels,
                 int numBranches,
                 SkRPDebugTrace* debugTrace)
        : fInstructions(std::move(instrs))
        , fNumValueSlots(numValueSlots)
        , fNumUniformSlots(numUniformSlots)
        , fNumLabels(numLabels)
        , fNumBranches(numBranches)
        , fDebugTrace(debugTrace) {
    this->optimize();

    fTempStackMaxDepths = this->tempStackMaxDepths();

    fNumTempStackSlots = 0;
    for (const auto& [stackIdx, depth] : fTempStackMaxDepths) {
        (void)stackIdx;
        fNumTempStackSlots += depth;
    }

    // These are not used in SKSL_STANDALONE yet.
    (void)fDebugTrace;
    (void)fNumUniformSlots;
}

void Program::append(SkRasterPipeline* pipeline, SkRasterPipeline::Stage stage, void* ctx) {
#if !defined(SKSL_STANDALONE)
    pipeline->append(stage, ctx);
#endif
}

void Program::rewindPipeline(SkRasterPipeline* pipeline) {
#if !defined(SKSL_STANDALONE)
#if !SK_HAS_MUSTTAIL
    pipeline->append_stack_rewind();
#endif
#endif
}

int Program::getNumPipelineStages(SkRasterPipeline* pipeline) {
#if !defined(SKSL_STANDALONE)
    return pipeline->getNumStages();
#else
    return 0;
#endif
}

void Program::appendCopy(SkRasterPipeline* pipeline,
                         SkArenaAlloc* alloc,
                         SkRasterPipeline::Stage baseStage,
                         float* dst, int dstStride,
                         const float* src, int srcStride,
                         int numSlots) {
    SkASSERT(numSlots >= 0);
    while (numSlots > 4) {
        this->appendCopy(pipeline, alloc, baseStage, dst, dstStride, src, srcStride,/*numSlots=*/4);
        dst += 4 * dstStride;
        src += 4 * srcStride;
        numSlots -= 4;
    }

    if (numSlots > 0) {
        SkASSERT(numSlots <= 4);
        auto stage = (SkRasterPipeline::Stage)((int)baseStage + numSlots - 1);
        auto* ctx = alloc->make<SkRasterPipeline_BinaryOpCtx>();
        ctx->dst = dst;
        ctx->src = src;
        this->append(pipeline, stage, ctx);
    }
}

void Program::appendCopySlotsUnmasked(SkRasterPipeline* pipeline,
                                      SkArenaAlloc* alloc,
                                      float* dst,
                                      const float* src,
                                      int numSlots) {
    this->appendCopy(pipeline, alloc,
                     SkRasterPipeline::copy_slot_unmasked,
                     dst, /*dstStride=*/SkOpts::raster_pipeline_highp_stride,
                     src, /*srcStride=*/SkOpts::raster_pipeline_highp_stride,
                     numSlots);
}

void Program::appendCopySlotsMasked(SkRasterPipeline* pipeline,
                                    SkArenaAlloc* alloc,
                                    float* dst,
                                    const float* src,
                                    int numSlots) {
    this->appendCopy(pipeline, alloc,
                     SkRasterPipeline::copy_slot_masked,
                     dst, /*dstStride=*/SkOpts::raster_pipeline_highp_stride,
                     src, /*srcStride=*/SkOpts::raster_pipeline_highp_stride,
                     numSlots);
}

void Program::appendCopyConstants(SkRasterPipeline* pipeline,
                                  SkArenaAlloc* alloc,
                                  float* dst,
                                  const float* src,
                                  int numSlots) {
    this->appendCopy(pipeline, alloc,
                     SkRasterPipeline::copy_constant,
                     dst, /*dstStride=*/SkOpts::raster_pipeline_highp_stride,
                     src, /*srcStride=*/1,
                     numSlots);
}

void Program::appendMultiSlotUnaryOp(SkRasterPipeline* pipeline, SkRasterPipeline::Stage baseStage,
                                     float* dst, int numSlots) {
    SkASSERT(numSlots >= 0);
    while (numSlots > 4) {
        this->appendMultiSlotUnaryOp(pipeline, baseStage, dst, /*numSlots=*/4);
        dst += 4 * SkOpts::raster_pipeline_highp_stride;
        numSlots -= 4;
    }

    SkASSERT(numSlots <= 4);
    auto stage = (SkRasterPipeline::Stage)((int)baseStage + numSlots - 1);
    this->append(pipeline, stage, dst);
}

void Program::appendAdjacentMultiSlotBinaryOp(SkRasterPipeline* pipeline, SkArenaAlloc* alloc,
                                              SkRasterPipeline::Stage baseStage,
                                              float* dst, const float* src, int numSlots) {
    // The source and destination must be directly next to one another.
    SkASSERT(numSlots >= 0);
    SkASSERT((dst + SkOpts::raster_pipeline_highp_stride * numSlots) == src);

    if (numSlots > 4) {
        auto ctx = alloc->make<SkRasterPipeline_BinaryOpCtx>();
        ctx->dst = dst;
        ctx->src = src;
        this->append(pipeline, baseStage, ctx);
        return;
    }
    if (numSlots > 0) {
        auto specializedStage = (SkRasterPipeline::Stage)((int)baseStage + numSlots);
        this->append(pipeline, specializedStage, dst);
    }
}

void Program::appendAdjacentMultiSlotTernaryOp(SkRasterPipeline* pipeline, SkArenaAlloc* alloc,
                                               SkRasterPipeline::Stage baseStage, float* dst,
                                               const float* src0, const float* src1, int numSlots) {
    // The float pointers must all be immediately adjacent to each other.
    SkASSERT(numSlots >= 0);
    SkASSERT((dst  + SkOpts::raster_pipeline_highp_stride * numSlots) == src0);
    SkASSERT((src0 + SkOpts::raster_pipeline_highp_stride * numSlots) == src1);

    if (numSlots > 4) {
        auto ctx = alloc->make<SkRasterPipeline_TernaryOpCtx>();
        ctx->dst = dst;
        ctx->src0 = src0;
        ctx->src1 = src1;
        this->append(pipeline, baseStage, ctx);
        return;
    }
    if (numSlots > 0) {
        auto specializedStage = (SkRasterPipeline::Stage)((int)baseStage + numSlots);
        this->append(pipeline, specializedStage, dst);
    }
}

template <typename T>
[[maybe_unused]] static void* context_bit_pun(T val) {
    static_assert(sizeof(T) <= sizeof(void*));
    void* contextBits = nullptr;
    memcpy(&contextBits, &val, sizeof(val));
    return contextBits;
}

Program::SlotData Program::allocateSlotData(SkArenaAlloc* alloc) {
    // Allocate a contiguous slab of slot data for values and stack entries.
    const int N = SkOpts::raster_pipeline_highp_stride;
    const int vectorWidth = N * sizeof(float);
    const int allocSize = vectorWidth * (fNumValueSlots + fNumTempStackSlots);
    float* slotPtr = static_cast<float*>(alloc->makeBytesAlignedTo(allocSize, vectorWidth));
    sk_bzero(slotPtr, allocSize);

    // Store the temp stack immediately after the values.
    SlotData s;
    s.values = SkSpan{slotPtr,        N * fNumValueSlots};
    s.stack  = SkSpan{s.values.end(), N * fNumTempStackSlots};
    return s;
}

void Program::appendStages(SkRasterPipeline* pipeline,
                           SkArenaAlloc* alloc,
                           SkSpan<const float> uniforms) {
    this->appendStages(pipeline, alloc, uniforms, this->allocateSlotData(alloc));
}

void Program::appendStages(SkRasterPipeline* pipeline,
                           SkArenaAlloc* alloc,
                           SkSpan<const float> uniforms,
                           const SlotData& slots) {
    SkASSERT(fNumUniformSlots == SkToInt(uniforms.size()));

    const int N = SkOpts::raster_pipeline_highp_stride;
    StackDepthMap tempStackDepth;
    int currentStack = 0;
    int mostRecentRewind = 0;

    // Allocate buffers for branch targets (used when running the program) and labels (only needed
    // during initial program construction).
    int* branchTargets = alloc->makeArrayDefault<int>(fNumBranches);
    SkTArray<int> labelOffsets;
    labelOffsets.push_back_n(fNumLabels, -1);
    SkTArray<int> branchGoesToLabel;
    branchGoesToLabel.push_back_n(fNumBranches, -1);
    int currentBranchOp = 0;

    // Assemble a map holding the current stack-top for each temporary stack. Position each temp
    // stack immediately after the previous temp stack; temp stacks are never allowed to overlap.
    int pos = 0;
    SkTHashMap<int, float*> tempStackMap;
    for (auto& [idx, depth] : fTempStackMaxDepths) {
        tempStackMap[idx] = slots.stack.begin() + (pos * N);
        pos += depth;
    }

    // We can reuse constants from our arena by placing them in this map.
    SkTHashMap<int, int*> constantLookupMap; // <constant value, pointer into arena>

    // Write each BuilderOp to the pipeline.
    for (const Instruction& inst : fInstructions) {
        auto SlotA    = [&]() { return &slots.values[N * inst.fSlotA]; };
        auto SlotB    = [&]() { return &slots.values[N * inst.fSlotB]; };
        auto UniformA = [&]() { return &uniforms[inst.fSlotA]; };
        float*& tempStackPtr = tempStackMap[currentStack];

        switch (inst.fOp) {
            case BuilderOp::label:
                // Write the absolute pipeline position into the label offset list. We will go over
                // the branch targets at the end and fix them up.
                SkASSERT(inst.fImmA >= 0 && inst.fImmA < fNumLabels);
                labelOffsets[inst.fImmA] = this->getNumPipelineStages(pipeline);
                break;

            case BuilderOp::jump:
            case BuilderOp::branch_if_any_active_lanes:
            case BuilderOp::branch_if_no_active_lanes:
                // If we have already encountered the label associated with this branch, this is a
                // backwards branch. Add a stack-rewind immediately before the branch to ensure that
                // long-running loops don't use an unbounded amount of stack space.
                if (labelOffsets[inst.fImmA] >= 0) {
                    this->rewindPipeline(pipeline);
                    mostRecentRewind = this->getNumPipelineStages(pipeline);
                }

                // Write the absolute pipeline position into the branch targets, because the
                // associated label might not have been reached yet. We will go back over the branch
                // targets at the end and fix them up.
                SkASSERT(inst.fImmA >= 0 && inst.fImmA < fNumLabels);
                SkASSERT(currentBranchOp >= 0 && currentBranchOp < fNumBranches);
                branchTargets[currentBranchOp] = this->getNumPipelineStages(pipeline);
                branchGoesToLabel[currentBranchOp] = inst.fImmA;
                this->append(pipeline, (SkRP::Stage)inst.fOp, &branchTargets[currentBranchOp]);
                ++currentBranchOp;
                break;

            case BuilderOp::init_lane_masks:
                this->append(pipeline, SkRP::init_lane_masks);
                break;

            case BuilderOp::store_src_rg:
                this->append(pipeline, SkRP::store_src_rg, SlotA());
                break;

            case BuilderOp::store_src:
                this->append(pipeline, SkRP::store_src, SlotA());
                break;

            case BuilderOp::store_dst:
                this->append(pipeline, SkRP::store_dst, SlotA());
                break;

            case BuilderOp::load_src:
                this->append(pipeline, SkRP::load_src, SlotA());
                break;

            case BuilderOp::load_dst:
                this->append(pipeline, SkRP::load_dst, SlotA());
                break;

            case BuilderOp::immediate_f: {
                this->append(pipeline, SkRP::immediate_f, context_bit_pun(inst.fImmA));
                break;
            }
            case BuilderOp::load_unmasked:
                this->append(pipeline, SkRP::load_unmasked, SlotA());
                break;

            case BuilderOp::store_unmasked:
                this->append(pipeline, SkRP::store_unmasked, SlotA());
                break;

            case BuilderOp::store_masked:
                this->append(pipeline, SkRP::store_masked, SlotA());
                break;

            case ALL_MULTI_SLOT_UNARY_OP_CASES: {
                float* dst = tempStackPtr - (inst.fImmA * N);
                this->appendMultiSlotUnaryOp(pipeline, (SkRP::Stage)inst.fOp, dst, inst.fImmA);
                break;
            }
            case ALL_MULTI_SLOT_BINARY_OP_CASES: {
                float* src = tempStackPtr - (inst.fImmA * N);
                float* dst = tempStackPtr - (inst.fImmA * 2 * N);
                this->appendAdjacentMultiSlotBinaryOp(pipeline, alloc, (SkRP::Stage)inst.fOp,
                                                      dst, src, inst.fImmA);
                break;
            }
            case ALL_MULTI_SLOT_TERNARY_OP_CASES: {
                float* src1 = tempStackPtr - (inst.fImmA * N);
                float* src0 = tempStackPtr - (inst.fImmA * 2 * N);
                float* dst  = tempStackPtr - (inst.fImmA * 3 * N);
                this->appendAdjacentMultiSlotTernaryOp(pipeline, alloc, (SkRP::Stage)inst.fOp,
                                                       dst, src0, src1, inst.fImmA);
                break;
            }
            case BuilderOp::select: {
                float* src = tempStackPtr - (inst.fImmA * N);
                float* dst = tempStackPtr - (inst.fImmA * 2 * N);
                this->appendCopySlotsMasked(pipeline, alloc, dst, src, inst.fImmA);
                break;
            }
            case BuilderOp::copy_slot_masked:
                this->appendCopySlotsMasked(pipeline, alloc, SlotA(), SlotB(), inst.fImmA);
                break;

            case BuilderOp::copy_slot_unmasked:
                this->appendCopySlotsUnmasked(pipeline, alloc, SlotA(), SlotB(), inst.fImmA);
                break;

            case BuilderOp::zero_slot_unmasked:
                this->appendMultiSlotUnaryOp(pipeline, SkRP::zero_slot_unmasked,
                                             SlotA(), inst.fImmA);
                break;

            case BuilderOp::swizzle_1:
            case BuilderOp::swizzle_2:
            case BuilderOp::swizzle_3:
            case BuilderOp::swizzle_4: {
                auto* ctx = alloc->make<SkRasterPipeline_SwizzleCtx>();
                ctx->ptr = tempStackPtr - (N * inst.fImmA);
                // Unpack component nybbles into byte-offsets pointing at stack slots.
                int components = inst.fImmB;
                for (size_t index = 0; index < std::size(ctx->offsets); ++index) {
                    ctx->offsets[index] = (components & 3) * N * sizeof(float);
                    components >>= 4;
                }
                this->append(pipeline, (SkRP::Stage)inst.fOp, ctx);
                break;
            }
            case BuilderOp::transpose: {
                auto* ctx = alloc->make<SkRasterPipeline_TransposeCtx>();
                ctx->ptr = tempStackPtr - (N * inst.fImmA * inst.fImmB);
                ctx->count = inst.fImmA * inst.fImmB;
                sk_bzero(ctx->offsets, std::size(ctx->offsets));
                size_t index = 0;
                for (int r = 0; r < inst.fImmB; ++r) {
                    for (int c = 0; c < inst.fImmA; ++c) {
                        ctx->offsets[index++] = ((c * inst.fImmB) + r) * N * sizeof(float);
                    }
                }
                this->append(pipeline, SkRP::Stage::transpose, ctx);
                break;
            }
            case BuilderOp::push_slots: {
                float* dst = tempStackPtr;
                this->appendCopySlotsUnmasked(pipeline, alloc, dst, SlotA(), inst.fImmA);
                break;
            }
            case BuilderOp::push_uniform: {
                float* dst = tempStackPtr;
                this->appendCopyConstants(pipeline, alloc, dst, UniformA(), inst.fImmA);
                break;
            }
            case BuilderOp::push_zeros: {
                float* dst = tempStackPtr;
                this->appendMultiSlotUnaryOp(pipeline, SkRP::zero_slot_unmasked, dst, inst.fImmA);
                break;
            }
            case BuilderOp::push_condition_mask: {
                float* dst = tempStackPtr;
                this->append(pipeline, SkRP::store_condition_mask, dst);
                break;
            }
            case BuilderOp::pop_condition_mask: {
                float* src = tempStackPtr - (1 * N);
                this->append(pipeline, SkRP::load_condition_mask, src);
                break;
            }
            case BuilderOp::merge_condition_mask: {
                float* ptr = tempStackPtr - (2 * N);
                this->append(pipeline, SkRP::merge_condition_mask, ptr);
                break;
            }
            case BuilderOp::push_loop_mask: {
                float* dst = tempStackPtr;
                this->append(pipeline, SkRP::store_loop_mask, dst);
                break;
            }
            case BuilderOp::pop_loop_mask: {
                float* src = tempStackPtr - (1 * N);
                this->append(pipeline, SkRP::load_loop_mask, src);
                break;
            }
            case BuilderOp::mask_off_loop_mask:
                this->append(pipeline, SkRP::mask_off_loop_mask);
                break;

            case BuilderOp::reenable_loop_mask:
                this->append(pipeline, SkRP::reenable_loop_mask, SlotA());
                break;

            case BuilderOp::merge_loop_mask: {
                float* src = tempStackPtr - (1 * N);
                this->append(pipeline, SkRP::merge_loop_mask, src);
                break;
            }
            case BuilderOp::push_return_mask: {
                float* dst = tempStackPtr;
                this->append(pipeline, SkRP::store_return_mask, dst);
                break;
            }
            case BuilderOp::pop_return_mask: {
                float* src = tempStackPtr - (1 * N);
                this->append(pipeline, SkRP::load_return_mask, src);
                break;
            }
            case BuilderOp::mask_off_return_mask:
                this->append(pipeline, SkRP::mask_off_return_mask);
                break;

            case BuilderOp::push_literal_f: {
                float* dst = tempStackPtr;
                if (inst.fImmA == 0) {
                    this->append(pipeline, SkRP::zero_slot_unmasked, dst);
                    break;
                }
                int* constantPtr;
                if (int** lookup = constantLookupMap.find(inst.fImmA)) {
                    constantPtr = *lookup;
                } else {
                    constantPtr = alloc->make<int>(inst.fImmA);
                    constantLookupMap[inst.fImmA] = constantPtr;
                }
                SkASSERT(constantPtr);
                this->appendCopyConstants(pipeline, alloc, dst, (float*)constantPtr,/*numSlots=*/1);
                break;
            }
            case BuilderOp::copy_stack_to_slots: {
                float* src = tempStackPtr - (inst.fImmB * N);
                this->appendCopySlotsMasked(pipeline, alloc, SlotA(), src, inst.fImmA);
                break;
            }
            case BuilderOp::copy_stack_to_slots_unmasked: {
                float* src = tempStackPtr - (inst.fImmB * N);
                this->appendCopySlotsUnmasked(pipeline, alloc, SlotA(), src, inst.fImmA);
                break;
            }
            case BuilderOp::push_clone: {
                float* src = tempStackPtr - (inst.fImmB * N);
                float* dst = tempStackPtr;
                this->appendCopySlotsUnmasked(pipeline, alloc, dst, src, inst.fImmA);
                break;
            }
            case BuilderOp::push_clone_from_stack: {
                float* sourceStackPtr = tempStackMap[inst.fImmB];
                float* src = sourceStackPtr - (inst.fImmA * N);
                float* dst = tempStackPtr;
                this->appendCopySlotsUnmasked(pipeline, alloc, dst, src, inst.fImmA);
                break;
            }
            case BuilderOp::discard_stack:
                break;

            case BuilderOp::set_current_stack:
                currentStack = inst.fImmA;
                break;

            default:
                SkDEBUGFAILF("Raster Pipeline: unsupported instruction %d", (int)inst.fOp);
                break;
        }

        tempStackPtr += stack_usage(inst) * N;
        SkASSERT(tempStackPtr >= slots.stack.begin());
        SkASSERT(tempStackPtr <= slots.stack.end());

        // Periodically rewind the stack every 500 instructions. When SK_HAS_MUSTTAIL is set,
        // rewinds are not actually used; the rewindPipeline call becomes a no-op. On platforms that
        // don't support SK_HAS_MUSTTAIL, rewinding the stack periodically can prevent a potential
        // stack overflow when running a long program.
        int numPipelineStages = this->getNumPipelineStages(pipeline);
        if (numPipelineStages - mostRecentRewind > 500) {
            this->rewindPipeline(pipeline);
            mostRecentRewind = numPipelineStages;
        }
    }

    // Fix up every branch target.
    for (int index = 0; index < fNumBranches; ++index) {
        int branchFromIdx = branchTargets[index];
        int branchToIdx = labelOffsets[branchGoesToLabel[index]];
        branchTargets[index] = branchToIdx - branchFromIdx;
    }
}

void Program::dump(SkWStream* out) {
    // TODO: skslc will want to dump these programs; we'll need to include some portion of
    // SkRasterPipeline into skslc for this to work properly.

#if !defined(SKSL_STANDALONE)
    // Allocate memory for the slot and uniform data, even though the program won't ever be
    // executed. The program requires pointer ranges for managing its data, and ASAN will report
    // errors if those pointers are pointing at unallocated memory.
    SkArenaAlloc alloc(/*firstHeapAllocation=*/1000);
    const int N = SkOpts::raster_pipeline_highp_stride;
    SlotData slots = this->allocateSlotData(&alloc);
    float* uniformPtr = alloc.makeArray<float>(fNumUniformSlots);
    SkSpan<float> uniforms = SkSpan(uniformPtr, fNumUniformSlots);

    // Instantiate this program.
    SkRasterPipeline pipeline(&alloc);
    this->appendStages(&pipeline, &alloc, uniforms, slots);
    const SkRP::StageList* st = pipeline.getStageList();

    // The stage list is in reverse order, so let's flip it.
    struct Stage {
        SkRP::Stage op;
        void*       ctx;
    };
    SkTArray<Stage> stages;
    for (; st != nullptr; st = st->prev) {
        stages.push_back(Stage{st->stage, st->ctx});
    }
    std::reverse(stages.begin(), stages.end());

    // Emit the program's instruction list.
    for (int index = 0; index < stages.size(); ++index) {
        const Stage& stage = stages[index];

        // Interpret the context value as a branch offset.
        auto BranchOffset = [&](const void* ctx) -> std::string {
            const int *ctxAsInt = static_cast<const int*>(ctx);
            return SkSL::String::printf("%+d (#%d)", *ctxAsInt, *ctxAsInt + index + 1);
        };

        // Print a 32-bit immediate value of unknown type (int/float).
        auto Imm = [&](float immFloat) -> std::string {
            // Start with `0x3F800000` as a baseline.
            uint32_t immUnsigned;
            memcpy(&immUnsigned, &immFloat, sizeof(uint32_t));
            auto text = SkSL::String::printf("0x%08X", immUnsigned);

            // Extend it to `0x3F800000 (1.0)` for finite floating point values.
            if (std::isfinite(immFloat)) {
                text += " (";
                text += skstd::to_string(immFloat);
                text += ")";
            }
            return text;
        };

        // Interpret the context pointer as a 32-bit immediate value of unknown type (int/float).
        auto ImmCtx = [&](const void* ctx) -> std::string {
            float f;
            memcpy(&f, &ctx, sizeof(float));
            return Imm(f);
        };

        // Print `1` for single slots and `1..3` for ranges of slots.
        auto AsRange = [](int first, int count) -> std::string {
            std::string text = std::to_string(first);
            if (count > 1) {
                text += ".." + std::to_string(first + count - 1);
            }
            return text;
        };

        // Attempts to interpret the passed-in pointer as a uniform range.
        auto UniformPtrCtx = [&](const float* ptr, int numSlots) -> std::string {
            if (fDebugTrace) {
                // Handle pointers to named uniform slots.
                if (ptr >= uniforms.begin() && ptr < uniforms.end()) {
                    int slotIdx = ptr - uniforms.begin();
                    if (slotIdx < (int)fDebugTrace->fUniformInfo.size()) {
                        const SlotDebugInfo& slotInfo = fDebugTrace->fUniformInfo[slotIdx];
                        if (!slotInfo.name.empty()) {
                            // If we're covering the entire uniform, return `uniName`.
                            if (numSlots == slotInfo.columns * slotInfo.rows) {
                                return slotInfo.name;
                            }
                            // If we are only covering part of the uniform, return `uniName(1..2)`.
                            return slotInfo.name + "(" +
                                   AsRange(slotInfo.componentIndex, numSlots) + ")";
                        }
                    }
                }
            }
            // Handle pointers to uniforms (when no debug info exists).
            if (ptr >= uniforms.begin() && ptr < uniforms.end()) {
                int uniformIdx = ptr - uniforms.begin();
                return "u" + AsRange(uniformIdx, numSlots);
            }
            return {};
        };

        // Attempts to interpret the passed-in pointer as a value slot range.
        auto ValuePtrCtx = [&](const float* ptr, int numSlots) -> std::string {
            if (fDebugTrace) {
                // Handle pointers to named value slots.
                if (ptr >= slots.values.begin() && ptr < slots.values.end()) {
                    int slotIdx = ptr - slots.values.begin();
                    SkASSERT((slotIdx % N) == 0);
                    slotIdx /= N;
                    if (slotIdx < (int)fDebugTrace->fSlotInfo.size()) {
                        const SlotDebugInfo& slotInfo = fDebugTrace->fSlotInfo[slotIdx];
                        if (!slotInfo.name.empty()) {
                            // If we're covering the entire slot, return `valueName`.
                            if (numSlots == slotInfo.columns * slotInfo.rows) {
                                return slotInfo.name;
                            }
                            // If we are only covering part of the slot, return `valueName(1..2)`.
                            return slotInfo.name + "(" +
                                   AsRange(slotInfo.componentIndex, numSlots) + ")";
                        }
                    }
                }
            }
            // Handle pointers to value slots (when no debug info exists).
            if (ptr >= slots.values.begin() && ptr < slots.values.end()) {
                int valueIdx = ptr - slots.values.begin();
                SkASSERT((valueIdx % N) == 0);
                return "v" + AsRange(valueIdx / N, numSlots);
            }
            return {};
        };

        // Interpret the context value as a pointer to `count` immediate values.
        auto MultiImmCtx = [&](const float* ptr, int count) -> std::string {
            // If this is a uniform, print it by name.
            if (std::string text = UniformPtrCtx(ptr, count); !text.empty()) {
                return text;
            }
            // Emit a single unbracketed immediate.
            if (count == 1) {
                return Imm(*ptr);
            }
            // Emit a list like `[0x00000000 (0.0), 0x3F80000 (1.0)]`.
            std::string text = "[";
            auto separator = SkSL::String::Separator();
            while (count--) {
                text += separator();
                text += Imm(*ptr++);
            }
            return text + "]";
        };

        // Interpret the context value as a generic pointer.
        auto PtrCtx = [&](const void* ctx, int numSlots) -> std::string {
            const float *ctxAsSlot = static_cast<const float*>(ctx);
            // Check for uniform and value pointers.
            if (std::string uniform = UniformPtrCtx(ctxAsSlot, numSlots); !uniform.empty()) {
                return uniform;
            }
            if (std::string value = ValuePtrCtx(ctxAsSlot, numSlots); !value.empty()) {
                return value;
            }
            // Handle pointers to temporary stack slots.
            if (ctxAsSlot >= slots.stack.begin() && ctxAsSlot < slots.stack.end()) {
                int stackIdx = ctxAsSlot - slots.stack.begin();
                SkASSERT((stackIdx % N) == 0);
                return "$" + AsRange(stackIdx / N, numSlots);
            }
            // This pointer is out of our expected bounds; this generally isn't expected to happen.
            return "ExternalPtr(" + AsRange(0, numSlots) + ")";
        };

        // Interpret the context value as a pointer to two adjacent values.
        auto AdjacentPtrCtx = [&](const void* ctx,
                                  int numSlots) -> std::tuple<std::string, std::string> {
            const float *ctxAsSlot = static_cast<const float*>(ctx);
            return std::make_tuple(PtrCtx(ctxAsSlot, numSlots),
                                   PtrCtx(ctxAsSlot + (N * numSlots), numSlots));
        };

        // Interpret the context value as a pointer to three adjacent values.
        auto Adjacent3PtrCtx = [&](const void* ctx, int numSlots) ->
                                  std::tuple<std::string, std::string, std::string> {
            const float *ctxAsSlot = static_cast<const float*>(ctx);
            return std::make_tuple(PtrCtx(ctxAsSlot, numSlots),
                                   PtrCtx(ctxAsSlot + (N * numSlots), numSlots),
                                   PtrCtx(ctxAsSlot + (2 * N * numSlots), numSlots));
        };

        // Interpret the context value as a BinaryOp structure for copy_n_slots (numSlots is
        // dictated by the op itself).
        auto BinaryOpCtx = [&](const void* v,
                               int numSlots) -> std::tuple<std::string, std::string> {
            const auto *ctx = static_cast<const SkRasterPipeline_BinaryOpCtx*>(v);
            return std::make_tuple(PtrCtx(ctx->dst, numSlots),
                                   PtrCtx(ctx->src, numSlots));
        };

        // Interpret the context value as a BinaryOp structure for copy_n_constants (numSlots is
        // dictated by the op itself).
        auto CopyConstantCtx = [&](const void* v,
                                   int numSlots) -> std::tuple<std::string, std::string> {
            const auto *ctx = static_cast<const SkRasterPipeline_BinaryOpCtx*>(v);
            return std::make_tuple(PtrCtx(ctx->dst, numSlots),
                                   MultiImmCtx(ctx->src, numSlots));
        };

        // Interpret the context value as a BinaryOp structure (numSlots is inferred from the
        // distance between pointers).
        auto AdjacentBinaryOpCtx = [&](const void* v) -> std::tuple<std::string, std::string> {
            const auto *ctx = static_cast<const SkRasterPipeline_BinaryOpCtx*>(v);
            int numSlots = (ctx->src - ctx->dst) / N;
            return AdjacentPtrCtx(ctx->dst, numSlots);
        };

        // Interpret the context value as a TernaryOp structure (numSlots is inferred from the
        // distance between pointers).
        auto AdjacentTernaryOpCtx = [&](const void* v) ->
                                       std::tuple<std::string, std::string, std::string> {
            const auto* ctx = static_cast<const SkRasterPipeline_TernaryOpCtx*>(v);
            int numSlots = (ctx->src0 - ctx->dst) / N;
            return Adjacent3PtrCtx(ctx->dst, numSlots);
        };

        // Interpret the context value as a Swizzle structure. Note that the slot-width of the
        // source expression is not preserved in the instruction encoding, so we need to do our best
        // using the data we have. (e.g., myFloat4.y would be indistinguishable from myFloat2.y.)
        auto SwizzleCtx = [&](SkRP::Stage op,
                              const void* v) -> std::tuple<std::string, std::string> {
            const auto* ctx = static_cast<const SkRasterPipeline_SwizzleCtx*>(v);

            int destSlots = (int)op - (int)SkRP::swizzle_1 + 1;
            int highestComponent =
                    *std::max_element(std::begin(ctx->offsets), std::end(ctx->offsets)) /
                    (N * sizeof(float));

            std::string src = "(" + PtrCtx(ctx->ptr, std::max(destSlots, highestComponent + 1)) +
                              ").";
            for (int index = 0; index < destSlots; ++index) {
                if (ctx->offsets[index] == (0 * N * sizeof(float))) {
                    src.push_back('x');
                } else if (ctx->offsets[index] == (1 * N * sizeof(float))) {
                    src.push_back('y');
                } else if (ctx->offsets[index] == (2 * N * sizeof(float))) {
                    src.push_back('z');
                } else if (ctx->offsets[index] == (3 * N * sizeof(float))) {
                    src.push_back('w');
                } else {
                    src.push_back('?');
                }
            }

            return std::make_tuple(PtrCtx(ctx->ptr, destSlots), src);
        };

        // Interpret the context value as a Transpose structure.
        auto TransposeCtx = [&](SkRP::Stage op,
                                const void* v) -> std::tuple<std::string, std::string> {
            const auto* ctx = static_cast<const SkRasterPipeline_TransposeCtx*>(v);

            std::string dst = PtrCtx(ctx->ptr, ctx->count);
            std::string src = "(" + dst + ")[";
            for (int index = 0; index < ctx->count; ++index) {
                if (ctx->offsets[index] % (N * sizeof(float))) {
                    src.push_back('?');
                } else {
                    src += std::to_string(ctx->offsets[index] / (N * sizeof(float)));
                }
                src.push_back(' ');
            }
            src.back() = ']';
            return std::make_tuple(dst, src);
        };

        std::string opArg1, opArg2, opArg3;
        switch (stage.op) {
            case SkRP::immediate_f:
                opArg1 = ImmCtx(stage.ctx);
                break;

            case SkRP::swizzle_1:
            case SkRP::swizzle_2:
            case SkRP::swizzle_3:
            case SkRP::swizzle_4:
                std::tie(opArg1, opArg2) = SwizzleCtx(stage.op, stage.ctx);
                break;

            case SkRP::transpose:
                std::tie(opArg1, opArg2) = TransposeCtx(stage.op, stage.ctx);
                break;

            case SkRP::load_unmasked:
            case SkRP::load_condition_mask:
            case SkRP::store_condition_mask:
            case SkRP::load_loop_mask:
            case SkRP::store_loop_mask:
            case SkRP::merge_loop_mask:
            case SkRP::reenable_loop_mask:
            case SkRP::load_return_mask:
            case SkRP::store_return_mask:
            case SkRP::store_masked:
            case SkRP::store_unmasked:
            case SkRP::zero_slot_unmasked:
            case SkRP::bitwise_not_int:
            case SkRP::cast_to_float_from_int: case SkRP::cast_to_float_from_uint:
            case SkRP::cast_to_int_from_float: case SkRP::cast_to_uint_from_float:
            case SkRP::abs_float:              case SkRP::abs_int:
            case SkRP::ceil_float:
            case SkRP::floor_float:
                opArg1 = PtrCtx(stage.ctx, 1);
                break;

            case SkRP::store_src_rg:
            case SkRP::zero_2_slots_unmasked:
            case SkRP::bitwise_not_2_ints:
            case SkRP::cast_to_float_from_2_ints: case SkRP::cast_to_float_from_2_uints:
            case SkRP::cast_to_int_from_2_floats: case SkRP::cast_to_uint_from_2_floats:
            case SkRP::abs_2_floats:              case SkRP::abs_2_ints:
            case SkRP::ceil_2_floats:
            case SkRP::floor_2_floats:
                opArg1 = PtrCtx(stage.ctx, 2);
                break;

            case SkRP::zero_3_slots_unmasked:
            case SkRP::bitwise_not_3_ints:
            case SkRP::cast_to_float_from_3_ints: case SkRP::cast_to_float_from_3_uints:
            case SkRP::cast_to_int_from_3_floats: case SkRP::cast_to_uint_from_3_floats:
            case SkRP::abs_3_floats:              case SkRP::abs_3_ints:
            case SkRP::ceil_3_floats:
            case SkRP::floor_3_floats:
                opArg1 = PtrCtx(stage.ctx, 3);
                break;

            case SkRP::load_src:
            case SkRP::load_dst:
            case SkRP::store_src:
            case SkRP::store_dst:
            case SkRP::zero_4_slots_unmasked:
            case SkRP::bitwise_not_4_ints:
            case SkRP::cast_to_float_from_4_ints: case SkRP::cast_to_float_from_4_uints:
            case SkRP::cast_to_int_from_4_floats: case SkRP::cast_to_uint_from_4_floats:
            case SkRP::abs_4_floats:              case SkRP::abs_4_ints:
            case SkRP::ceil_4_floats:
            case SkRP::floor_4_floats:
                opArg1 = PtrCtx(stage.ctx, 4);
                break;

            case SkRP::copy_constant:
                std::tie(opArg1, opArg2) = CopyConstantCtx(stage.ctx, 1);
                break;

            case SkRP::copy_2_constants:
                std::tie(opArg1, opArg2) = CopyConstantCtx(stage.ctx, 2);
                break;

            case SkRP::copy_3_constants:
                std::tie(opArg1, opArg2) = CopyConstantCtx(stage.ctx, 3);
                break;

            case SkRP::copy_4_constants:
                std::tie(opArg1, opArg2) = CopyConstantCtx(stage.ctx, 4);
                break;

            case SkRP::copy_slot_masked:
            case SkRP::copy_slot_unmasked:
                std::tie(opArg1, opArg2) = BinaryOpCtx(stage.ctx, 1);
                break;

            case SkRP::copy_2_slots_masked:
            case SkRP::copy_2_slots_unmasked:
                std::tie(opArg1, opArg2) = BinaryOpCtx(stage.ctx, 2);
                break;

            case SkRP::copy_3_slots_masked:
            case SkRP::copy_3_slots_unmasked:
                std::tie(opArg1, opArg2) = BinaryOpCtx(stage.ctx, 3);
                break;

            case SkRP::copy_4_slots_masked:
            case SkRP::copy_4_slots_unmasked:
                std::tie(opArg1, opArg2) = BinaryOpCtx(stage.ctx, 4);
                break;

            case SkRP::merge_condition_mask:
            case SkRP::add_float:   case SkRP::add_int:
            case SkRP::sub_float:   case SkRP::sub_int:
            case SkRP::mul_float:   case SkRP::mul_int:
            case SkRP::div_float:   case SkRP::div_int:   case SkRP::div_uint:
                                    case SkRP::bitwise_and_int:
                                    case SkRP::bitwise_or_int:
                                    case SkRP::bitwise_xor_int:
            case SkRP::min_float:   case SkRP::min_int:   case SkRP::min_uint:
            case SkRP::max_float:   case SkRP::max_int:   case SkRP::max_uint:
            case SkRP::cmplt_float: case SkRP::cmplt_int: case SkRP::cmplt_uint:
            case SkRP::cmple_float: case SkRP::cmple_int: case SkRP::cmple_uint:
            case SkRP::cmpeq_float: case SkRP::cmpeq_int:
            case SkRP::cmpne_float: case SkRP::cmpne_int:
                std::tie(opArg1, opArg2) = AdjacentPtrCtx(stage.ctx, 1);
                break;

            case SkRP::mix_float:
                std::tie(opArg1, opArg2, opArg3) = Adjacent3PtrCtx(stage.ctx, 1);
                break;

            case SkRP::add_2_floats:   case SkRP::add_2_ints:
            case SkRP::sub_2_floats:   case SkRP::sub_2_ints:
            case SkRP::mul_2_floats:   case SkRP::mul_2_ints:
            case SkRP::div_2_floats:   case SkRP::div_2_ints:   case SkRP::div_2_uints:
                                       case SkRP::bitwise_and_2_ints:
                                       case SkRP::bitwise_or_2_ints:
                                       case SkRP::bitwise_xor_2_ints:
            case SkRP::min_2_floats:   case SkRP::min_2_ints:   case SkRP::min_2_uints:
            case SkRP::max_2_floats:   case SkRP::max_2_ints:   case SkRP::max_2_uints:
            case SkRP::cmplt_2_floats: case SkRP::cmplt_2_ints: case SkRP::cmplt_2_uints:
            case SkRP::cmple_2_floats: case SkRP::cmple_2_ints: case SkRP::cmple_2_uints:
            case SkRP::cmpeq_2_floats: case SkRP::cmpeq_2_ints:
            case SkRP::cmpne_2_floats: case SkRP::cmpne_2_ints:
                std::tie(opArg1, opArg2) = AdjacentPtrCtx(stage.ctx, 2);
                break;

            case SkRP::mix_2_floats:
                std::tie(opArg1, opArg2, opArg3) = Adjacent3PtrCtx(stage.ctx, 2);
                break;

            case SkRP::add_3_floats:   case SkRP::add_3_ints:
            case SkRP::sub_3_floats:   case SkRP::sub_3_ints:
            case SkRP::mul_3_floats:   case SkRP::mul_3_ints:
            case SkRP::div_3_floats:   case SkRP::div_3_ints:   case SkRP::div_3_uints:
                                       case SkRP::bitwise_and_3_ints:
                                       case SkRP::bitwise_or_3_ints:
                                       case SkRP::bitwise_xor_3_ints:
            case SkRP::min_3_floats:   case SkRP::min_3_ints:   case SkRP::min_3_uints:
            case SkRP::max_3_floats:   case SkRP::max_3_ints:   case SkRP::max_3_uints:
            case SkRP::cmplt_3_floats: case SkRP::cmplt_3_ints: case SkRP::cmplt_3_uints:
            case SkRP::cmple_3_floats: case SkRP::cmple_3_ints: case SkRP::cmple_3_uints:
            case SkRP::cmpeq_3_floats: case SkRP::cmpeq_3_ints:
            case SkRP::cmpne_3_floats: case SkRP::cmpne_3_ints:
                std::tie(opArg1, opArg2) = AdjacentPtrCtx(stage.ctx, 3);
                break;

            case SkRP::mix_3_floats:
                std::tie(opArg1, opArg2, opArg3) = Adjacent3PtrCtx(stage.ctx, 3);
                break;

            case SkRP::add_4_floats:   case SkRP::add_4_ints:
            case SkRP::sub_4_floats:   case SkRP::sub_4_ints:
            case SkRP::mul_4_floats:   case SkRP::mul_4_ints:
            case SkRP::div_4_floats:   case SkRP::div_4_ints:   case SkRP::div_4_uints:
                                       case SkRP::bitwise_and_4_ints:
                                       case SkRP::bitwise_or_4_ints:
                                       case SkRP::bitwise_xor_4_ints:
            case SkRP::min_4_floats:   case SkRP::min_4_ints:   case SkRP::min_4_uints:
            case SkRP::max_4_floats:   case SkRP::max_4_ints:   case SkRP::max_4_uints:
            case SkRP::cmplt_4_floats: case SkRP::cmplt_4_ints: case SkRP::cmplt_4_uints:
            case SkRP::cmple_4_floats: case SkRP::cmple_4_ints: case SkRP::cmple_4_uints:
            case SkRP::cmpeq_4_floats: case SkRP::cmpeq_4_ints:
            case SkRP::cmpne_4_floats: case SkRP::cmpne_4_ints:
                std::tie(opArg1, opArg2) = AdjacentPtrCtx(stage.ctx, 4);
                break;

            case SkRP::mix_4_floats:
                std::tie(opArg1, opArg2, opArg3) = Adjacent3PtrCtx(stage.ctx, 4);
                break;

            case SkRP::add_n_floats:   case SkRP::add_n_ints:
            case SkRP::sub_n_floats:   case SkRP::sub_n_ints:
            case SkRP::mul_n_floats:   case SkRP::mul_n_ints:
            case SkRP::div_n_floats:   case SkRP::div_n_ints:   case SkRP::div_n_uints:
                                       case SkRP::bitwise_and_n_ints:
                                       case SkRP::bitwise_or_n_ints:
                                       case SkRP::bitwise_xor_n_ints:
            case SkRP::min_n_floats:   case SkRP::min_n_ints:   case SkRP::min_n_uints:
            case SkRP::max_n_floats:   case SkRP::max_n_ints:   case SkRP::max_n_uints:
            case SkRP::cmplt_n_floats: case SkRP::cmplt_n_ints: case SkRP::cmplt_n_uints:
            case SkRP::cmple_n_floats: case SkRP::cmple_n_ints: case SkRP::cmple_n_uints:
            case SkRP::cmpeq_n_floats: case SkRP::cmpeq_n_ints:
            case SkRP::cmpne_n_floats: case SkRP::cmpne_n_ints:
                std::tie(opArg1, opArg2) = AdjacentBinaryOpCtx(stage.ctx);
                break;

            case SkRP::mix_n_floats:
                std::tie(opArg1, opArg2, opArg3) = AdjacentTernaryOpCtx(stage.ctx);
                break;

            case SkRP::jump:
            case SkRP::branch_if_any_active_lanes:
            case SkRP::branch_if_no_active_lanes:
                opArg1 = BranchOffset(stage.ctx);
                break;

            default:
                break;
        }

        const char* opName = SkRasterPipeline::GetStageName(stage.op);
        std::string opText;
        switch (stage.op) {
            case SkRP::init_lane_masks:
                opText = "CondMask = LoopMask = RetMask = true";
                break;

            case SkRP::load_condition_mask:
                opText = "CondMask = " + opArg1;
                break;

            case SkRP::store_condition_mask:
                opText = opArg1 + " = CondMask";
                break;

            case SkRP::merge_condition_mask:
                opText = "CondMask = " + opArg1 + " & " + opArg2;
                break;

            case SkRP::load_loop_mask:
                opText = "LoopMask = " + opArg1;
                break;

            case SkRP::store_loop_mask:
                opText = opArg1 + " = LoopMask";
                break;

            case SkRP::mask_off_loop_mask:
                opText = "LoopMask &= ~(CondMask & LoopMask & RetMask)";
                break;

            case SkRP::reenable_loop_mask:
                opText = "LoopMask |= " + opArg1;
                break;

            case SkRP::merge_loop_mask:
                opText = "LoopMask &= " + opArg1;
                break;

            case SkRP::load_return_mask:
                opText = "RetMask = " + opArg1;
                break;

            case SkRP::store_return_mask:
                opText = opArg1 + " = RetMask";
                break;

            case SkRP::mask_off_return_mask:
                opText = "RetMask &= ~(CondMask & LoopMask & RetMask)";
                break;

            case SkRP::immediate_f:
            case SkRP::load_unmasked:
                opText = "src.r = " + opArg1;
                break;

            case SkRP::store_unmasked:
                opText = opArg1 + " = src.r";
                break;

            case SkRP::store_src_rg:
                opText = opArg1 + " = src.rg";
                break;

            case SkRP::store_src:
                opText = opArg1 + " = src.rgba";
                break;

            case SkRP::store_dst:
                opText = opArg1 + " = dst.rgba";
                break;

            case SkRP::load_src:
                opText = "src.rgba = " + opArg1;
                break;

            case SkRP::load_dst:
                opText = "dst.rgba = " + opArg1;
                break;

            case SkRP::store_masked:
                opText = opArg1 + " = Mask(src.r)";
                break;

            case SkRP::bitwise_and_int:
            case SkRP::bitwise_and_2_ints:
            case SkRP::bitwise_and_3_ints:
            case SkRP::bitwise_and_4_ints:
            case SkRP::bitwise_and_n_ints:
                opText = opArg1 + " &= " + opArg2;
                break;

            case SkRP::bitwise_or_int:
            case SkRP::bitwise_or_2_ints:
            case SkRP::bitwise_or_3_ints:
            case SkRP::bitwise_or_4_ints:
            case SkRP::bitwise_or_n_ints:
                opText = opArg1 + " |= " + opArg2;
                break;

            case SkRP::bitwise_xor_int:
            case SkRP::bitwise_xor_2_ints:
            case SkRP::bitwise_xor_3_ints:
            case SkRP::bitwise_xor_4_ints:
            case SkRP::bitwise_xor_n_ints:
                opText = opArg1 + " ^= " + opArg2;
                break;

            case SkRP::bitwise_not_int:
            case SkRP::bitwise_not_2_ints:
            case SkRP::bitwise_not_3_ints:
            case SkRP::bitwise_not_4_ints:
                opText = opArg1 + " = ~" + opArg1;
                break;

            case SkRP::cast_to_float_from_int:
            case SkRP::cast_to_float_from_2_ints:
            case SkRP::cast_to_float_from_3_ints:
            case SkRP::cast_to_float_from_4_ints:
                opText = opArg1 + " = IntToFloat(" + opArg1 + ")";
                break;

            case SkRP::cast_to_float_from_uint:
            case SkRP::cast_to_float_from_2_uints:
            case SkRP::cast_to_float_from_3_uints:
            case SkRP::cast_to_float_from_4_uints:
                opText = opArg1 + " = UintToFloat(" + opArg1 + ")";
                break;

            case SkRP::cast_to_int_from_float:
            case SkRP::cast_to_int_from_2_floats:
            case SkRP::cast_to_int_from_3_floats:
            case SkRP::cast_to_int_from_4_floats:
                opText = opArg1 + " = FloatToInt(" + opArg1 + ")";
                break;

            case SkRP::cast_to_uint_from_float:
            case SkRP::cast_to_uint_from_2_floats:
            case SkRP::cast_to_uint_from_3_floats:
            case SkRP::cast_to_uint_from_4_floats:
                opText = opArg1 + " = FloatToUint(" + opArg1 + ")";
                break;

            case SkRP::copy_slot_masked:      case SkRP::copy_2_slots_masked:
            case SkRP::copy_3_slots_masked:   case SkRP::copy_4_slots_masked:
                opText = opArg1 + " = Mask(" + opArg2 + ")";
                break;

            case SkRP::copy_constant:         case SkRP::copy_2_constants:
            case SkRP::copy_3_constants:      case SkRP::copy_4_constants:
            case SkRP::copy_slot_unmasked:    case SkRP::copy_2_slots_unmasked:
            case SkRP::copy_3_slots_unmasked: case SkRP::copy_4_slots_unmasked:
            case SkRP::swizzle_1:             case SkRP::swizzle_2:
            case SkRP::swizzle_3:             case SkRP::swizzle_4:
            case SkRP::transpose:
                opText = opArg1 + " = " + opArg2;
                break;

            case SkRP::zero_slot_unmasked:    case SkRP::zero_2_slots_unmasked:
            case SkRP::zero_3_slots_unmasked: case SkRP::zero_4_slots_unmasked:
                opText = opArg1 + " = 0";
                break;

            case SkRP::abs_float:    case SkRP::abs_int:
            case SkRP::abs_2_floats: case SkRP::abs_2_ints:
            case SkRP::abs_3_floats: case SkRP::abs_3_ints:
            case SkRP::abs_4_floats: case SkRP::abs_4_ints:
                opText = opArg1 + " = abs(" + opArg1 + ")";
                break;

            case SkRP::ceil_float:
            case SkRP::ceil_2_floats:
            case SkRP::ceil_3_floats:
            case SkRP::ceil_4_floats:
                opText = opArg1 + " = ceil(" + opArg1 + ")";
                break;

            case SkRP::floor_float:
            case SkRP::floor_2_floats:
            case SkRP::floor_3_floats:
            case SkRP::floor_4_floats:
                opText = opArg1 + " = floor(" + opArg1 + ")";
                break;

            case SkRP::add_float:    case SkRP::add_int:
            case SkRP::add_2_floats: case SkRP::add_2_ints:
            case SkRP::add_3_floats: case SkRP::add_3_ints:
            case SkRP::add_4_floats: case SkRP::add_4_ints:
            case SkRP::add_n_floats: case SkRP::add_n_ints:
                opText = opArg1 + " += " + opArg2;
                break;

            case SkRP::sub_float:    case SkRP::sub_int:
            case SkRP::sub_2_floats: case SkRP::sub_2_ints:
            case SkRP::sub_3_floats: case SkRP::sub_3_ints:
            case SkRP::sub_4_floats: case SkRP::sub_4_ints:
            case SkRP::sub_n_floats: case SkRP::sub_n_ints:
                opText = opArg1 + " -= " + opArg2;
                break;

            case SkRP::mul_float:    case SkRP::mul_int:
            case SkRP::mul_2_floats: case SkRP::mul_2_ints:
            case SkRP::mul_3_floats: case SkRP::mul_3_ints:
            case SkRP::mul_4_floats: case SkRP::mul_4_ints:
            case SkRP::mul_n_floats: case SkRP::mul_n_ints:
                opText = opArg1 + " *= " + opArg2;
                break;

            case SkRP::div_float:    case SkRP::div_int:    case SkRP::div_uint:
            case SkRP::div_2_floats: case SkRP::div_2_ints: case SkRP::div_2_uints:
            case SkRP::div_3_floats: case SkRP::div_3_ints: case SkRP::div_3_uints:
            case SkRP::div_4_floats: case SkRP::div_4_ints: case SkRP::div_4_uints:
            case SkRP::div_n_floats: case SkRP::div_n_ints: case SkRP::div_n_uints:
                opText = opArg1 + " /= " + opArg2;
                break;

            case SkRP::min_float:    case SkRP::min_int:    case SkRP::min_uint:
            case SkRP::min_2_floats: case SkRP::min_2_ints: case SkRP::min_2_uints:
            case SkRP::min_3_floats: case SkRP::min_3_ints: case SkRP::min_3_uints:
            case SkRP::min_4_floats: case SkRP::min_4_ints: case SkRP::min_4_uints:
            case SkRP::min_n_floats: case SkRP::min_n_ints: case SkRP::min_n_uints:
                opText = opArg1 + " = min(" + opArg1 + ", " + opArg2 + ")";
                break;

            case SkRP::max_float:    case SkRP::max_int:    case SkRP::max_uint:
            case SkRP::max_2_floats: case SkRP::max_2_ints: case SkRP::max_2_uints:
            case SkRP::max_3_floats: case SkRP::max_3_ints: case SkRP::max_3_uints:
            case SkRP::max_4_floats: case SkRP::max_4_ints: case SkRP::max_4_uints:
            case SkRP::max_n_floats: case SkRP::max_n_ints: case SkRP::max_n_uints:
                opText = opArg1 + " = max(" + opArg1 + ", " + opArg2 + ")";
                break;

            case SkRP::cmplt_float:    case SkRP::cmplt_int:    case SkRP::cmplt_uint:
            case SkRP::cmplt_2_floats: case SkRP::cmplt_2_ints: case SkRP::cmplt_2_uints:
            case SkRP::cmplt_3_floats: case SkRP::cmplt_3_ints: case SkRP::cmplt_3_uints:
            case SkRP::cmplt_4_floats: case SkRP::cmplt_4_ints: case SkRP::cmplt_4_uints:
            case SkRP::cmplt_n_floats: case SkRP::cmplt_n_ints: case SkRP::cmplt_n_uints:
                opText = opArg1 + " = lessThan(" + opArg1 + ", " + opArg2 + ")";
                break;

            case SkRP::cmple_float:    case SkRP::cmple_int:    case SkRP::cmple_uint:
            case SkRP::cmple_2_floats: case SkRP::cmple_2_ints: case SkRP::cmple_2_uints:
            case SkRP::cmple_3_floats: case SkRP::cmple_3_ints: case SkRP::cmple_3_uints:
            case SkRP::cmple_4_floats: case SkRP::cmple_4_ints: case SkRP::cmple_4_uints:
            case SkRP::cmple_n_floats: case SkRP::cmple_n_ints: case SkRP::cmple_n_uints:
                opText = opArg1 + " = lessThanEqual(" + opArg1 + ", " + opArg2 + ")";
                break;

            case SkRP::cmpeq_float:    case SkRP::cmpeq_int:
            case SkRP::cmpeq_2_floats: case SkRP::cmpeq_2_ints:
            case SkRP::cmpeq_3_floats: case SkRP::cmpeq_3_ints:
            case SkRP::cmpeq_4_floats: case SkRP::cmpeq_4_ints:
            case SkRP::cmpeq_n_floats: case SkRP::cmpeq_n_ints:
                opText = opArg1 + " = equal(" + opArg1 + ", " + opArg2 + ")";
                break;

            case SkRP::cmpne_float:    case SkRP::cmpne_int:
            case SkRP::cmpne_2_floats: case SkRP::cmpne_2_ints:
            case SkRP::cmpne_3_floats: case SkRP::cmpne_3_ints:
            case SkRP::cmpne_4_floats: case SkRP::cmpne_4_ints:
            case SkRP::cmpne_n_floats: case SkRP::cmpne_n_ints:
                opText = opArg1 + " = notEqual(" + opArg1 + ", " + opArg2 + ")";
                break;

            case SkRP::mix_float:
            case SkRP::mix_2_floats:
            case SkRP::mix_3_floats:
            case SkRP::mix_4_floats:
            case SkRP::mix_n_floats:
                opText = opArg1 + " = mix(" + opArg1 + ", " + opArg2 + ", " + opArg3 + ")";
                break;

            case SkRP::jump:
            case SkRP::branch_if_any_active_lanes:
            case SkRP::branch_if_no_active_lanes:
                opText = std::string(opName) + " " + opArg1;
                break;

            default:
                break;
        }

        std::string line = !opText.empty()
                ? SkSL::String::printf("% 5d. %-30s %s\n", index + 1, opName, opText.c_str())
                : SkSL::String::printf("% 5d. %s\n", index + 1, opName);

        out->writeText(line.c_str());
    }
#endif
}

}  // namespace RP
}  // namespace SkSL
