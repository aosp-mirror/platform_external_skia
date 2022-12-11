/*
 * Copyright 2022 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkTypes.h"
#include "include/private/SkTArray.h"
#include "include/private/SkTHash.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkUtils.h"

#include <cstdint>
#include <initializer_list>
#include <memory>

class SkArenaAlloc;
class SkWStream;

namespace SkSL {

class SkRPDebugTrace;

namespace RP {

// A single scalar in our program consumes one slot.
using Slot = int;
constexpr Slot NA = -1;

// Scalars, vectors, and matrices can be represented as a range of slot indices.
struct SlotRange {
    Slot index = 0;
    int count = 0;
};

// Ops that the builder will contextually rewrite into different RasterPipeline stages.
enum class BuilderOp {
    // We support all the native Raster Pipeline stages.
    #define M(stage) stage,
        SK_RASTER_PIPELINE_STAGES_ALL(M)
    #undef M
    // We also support Builder-specific ops; these are converted into real RP stages during
    // `appendStages`.
    push_literal_f,
    push_slots,
    copy_stack_to_slots,
    copy_stack_to_slots_unmasked,
    discard_stack,
    duplicate,
    select,
    push_condition_mask,
    pop_condition_mask,
    push_loop_mask,
    pop_loop_mask,
    push_return_mask,
    pop_return_mask,
    set_current_stack,
    label,
    unsupported
};

// Represents a single raster-pipeline SkSL instruction.
struct Instruction {
    Instruction(BuilderOp op, std::initializer_list<Slot> slots) : fOp(op), fImmA(0) {
        auto iter = slots.begin();
        if (iter != slots.end()) { fSlotA = *iter++; }
        if (iter != slots.end()) { fSlotB = *iter++; }
        if (iter != slots.end()) { fSlotC = *iter++; }
        SkASSERT(iter == slots.end());
    }

    Instruction(BuilderOp op, std::initializer_list<Slot> slots, int i) : fOp(op), fImmA(i) {
        auto iter = slots.begin();
        if (iter != slots.end()) { fSlotA = *iter++; }
        if (iter != slots.end()) { fSlotB = *iter++; }
        if (iter != slots.end()) { fSlotC = *iter++; }
        SkASSERT(iter == slots.end());
    }

    BuilderOp fOp;
    Slot      fSlotA = NA;
    Slot      fSlotB = NA;
    Slot      fSlotC = NA;
    int       fImmA = 0;
};

class Program {
public:
    Program(SkTArray<Instruction> instrs,
            int numValueSlots,
            int numLabels,
            int numBranches,
            SkRPDebugTrace* debugTrace);

    void appendStages(SkRasterPipeline* pipeline, SkArenaAlloc* alloc);
    void dump(SkWStream* s);

private:
    using StackDepthMap = SkTHashMap<int, int>; // <stack index, depth of stack>

    float* allocateSlotData(SkArenaAlloc* alloc);
    void appendStages(SkRasterPipeline* pipeline, SkArenaAlloc* alloc, float* slotPtr);
    void optimize();
    int numValueSlots();
    StackDepthMap tempStackMaxDepths();

    SkTArray<Instruction> fInstructions;
    int fNumValueSlots = 0;
    int fNumTempStackSlots = 0;
    int fNumLabels = 0;
    int fNumBranches = 0;
    SkTHashMap<int, int> fTempStackMaxDepths;
    SkRPDebugTrace* fDebugTrace = nullptr;
};

class Builder {
public:
    /** Finalizes and optimizes the program. */
    std::unique_ptr<Program> finish(int numValueSlots, SkRPDebugTrace* debugTrace = nullptr);

    /**
     * Peels off a label ID for use in the program. Set the label's position in the program with
     * the `label` instruction. Actually branch to the target with an instruction like
     * `branch_if_any_active_lanes` or `jump`.
     */
    int nextLabelID() {
        return fNumLabels++;
    }

    /** Assemble a program from the Raster Pipeline instructions below. */
    void init_lane_masks() {
        fInstructions.push_back({BuilderOp::init_lane_masks, {}});
    }

    void store_src_rg(SlotRange slots) {
        SkASSERT(slots.count == 2);
        fInstructions.push_back({BuilderOp::store_src_rg, {slots.index}});
    }

    void store_src(SlotRange slots) {
        SkASSERT(slots.count == 4);
        fInstructions.push_back({BuilderOp::store_src, {slots.index}});
    }

    void store_dst(SlotRange slots) {
        SkASSERT(slots.count == 4);
        fInstructions.push_back({BuilderOp::store_dst, {slots.index}});
    }

    void load_src(SlotRange slots) {
        SkASSERT(slots.count == 4);
        fInstructions.push_back({BuilderOp::load_src, {slots.index}});
    }

    void load_dst(SlotRange slots) {
        SkASSERT(slots.count == 4);
        fInstructions.push_back({BuilderOp::load_dst, {slots.index}});
    }

    void set_current_stack(int stackIdx) {
        fInstructions.push_back({BuilderOp::set_current_stack, {}, stackIdx});
    }

    void label(int labelID) {
        SkASSERT(labelID >= 0 && labelID < fNumLabels);
        fInstructions.push_back({BuilderOp::label, {}, labelID});
    }

    void jump(int labelID) {
        SkASSERT(labelID >= 0 && labelID < fNumLabels);
        fInstructions.push_back({BuilderOp::jump, {}, labelID});
        ++fNumBranches;
    }

    void branch_if_any_active_lanes(int labelID) {
        SkASSERT(labelID >= 0 && labelID < fNumLabels);
        fInstructions.push_back({BuilderOp::branch_if_any_active_lanes, {}, labelID});
        ++fNumBranches;
    }

    void branch_if_no_active_lanes(int labelID) {
        SkASSERT(labelID >= 0 && labelID < fNumLabels);
        fInstructions.push_back({BuilderOp::branch_if_no_active_lanes, {}, labelID});
        ++fNumBranches;
    }

    // We use the same SkRasterPipeline op regardless of the literal type, and bitcast the value.
    void immediate_f(float val) {
        fInstructions.push_back({BuilderOp::immediate_f, {}, sk_bit_cast<int32_t>(val)});
    }

    void immediate_i(int32_t val) {
        fInstructions.push_back({BuilderOp::immediate_f, {}, val});
    }

    void immediate_u(uint32_t val) {
        fInstructions.push_back({BuilderOp::immediate_f, {}, sk_bit_cast<int32_t>(val)});
    }

    void push_literal_f(float val) {
        fInstructions.push_back({BuilderOp::push_literal_f, {}, sk_bit_cast<int32_t>(val)});
    }

    void push_literal_i(int32_t val) {
        fInstructions.push_back({BuilderOp::push_literal_f, {}, val});
    }

    void push_literal_u(uint32_t val) {
        fInstructions.push_back({BuilderOp::push_literal_f, {}, sk_bit_cast<int32_t>(val)});
    }

    void push_slots(SlotRange src) {
        // Translates into copy_slots_unmasked (from values into temp stack) in Raster Pipeline.
        fInstructions.push_back({BuilderOp::push_slots, {src.index}, src.count});
    }

    void copy_stack_to_slots(SlotRange dst) {
        // Translates into copy_slots_masked (from temp stack to values) in Raster Pipeline.
        // Does not discard any values on the temp stack.
        fInstructions.push_back({BuilderOp::copy_stack_to_slots, {dst.index}, dst.count});
    }

    void copy_stack_to_slots_unmasked(SlotRange dst) {
        // Translates into copy_slots_unmasked (from temp stack to values) in Raster Pipeline.
        // Does not discard any values on the temp stack.
        fInstructions.push_back({BuilderOp::copy_stack_to_slots_unmasked, {dst.index}, dst.count});
    }

    // Performs a unary op (like `bitwise_not`), given a slot count of `slots`. The stack top is
    // replaced with the result.
    void unary_op(BuilderOp op, int32_t slots);

    // Performs a binary op (like `add_n_floats` or `cmpeq_n_ints`), given a slot count of
    // `slots`. Both input values are consumed, and the result is pushed onto the stack.
    void binary_op(BuilderOp op, int32_t slots);

    void discard_stack(int32_t count = 1) {
        // Shrinks the temp stack, discarding values on top.
        fInstructions.push_back({BuilderOp::discard_stack, {}, count});
    }

    void pop_slots(SlotRange dst) {
        // The opposite of push_slots; copies values from the temp stack into value slots, then
        // shrinks the temp stack.
        this->copy_stack_to_slots(dst);
        this->discard_stack(dst.count);
    }

    void duplicate(int count) {
        // Creates duplicates of the top item on the temp stack.
        SkASSERT(count >= 0);
        fInstructions.push_back({BuilderOp::duplicate, {}, count});
    }

    void select(int slots) {
        // Overlays the top two entries on the stack, making one hybrid entry. The execution mask
        // is used to select which lanes are preserved.
        SkASSERT(slots > 0);
        fInstructions.push_back({BuilderOp::select, {}, slots});
    }

    void pop_slots_unmasked(SlotRange dst) {
        // The opposite of push_slots; copies values from the temp stack into value slots, then
        // shrinks the temp stack.
        this->copy_stack_to_slots_unmasked(dst);
        this->discard_stack(dst.count);
    }

    void load_unmasked(Slot slot) {
        fInstructions.push_back({BuilderOp::load_unmasked, {slot}});
    }

    void store_unmasked(Slot slot) {
        fInstructions.push_back({BuilderOp::store_unmasked, {slot}});
    }

    void store_masked(Slot slot) {
        fInstructions.push_back({BuilderOp::store_masked, {slot}});
    }

    void copy_slots_masked(SlotRange dst, SlotRange src) {
        SkASSERT(dst.count == src.count);
        fInstructions.push_back({BuilderOp::copy_slot_masked, {dst.index, src.index}, dst.count});
    }

    void copy_slots_unmasked(SlotRange dst, SlotRange src) {
        SkASSERT(dst.count == src.count);
        fInstructions.push_back({BuilderOp::copy_slot_unmasked, {dst.index, src.index}, dst.count});
    }

    void zero_slots_unmasked(SlotRange dst) {
        fInstructions.push_back({BuilderOp::zero_slot_unmasked, {dst.index}, dst.count});
    }

    void push_condition_mask() {
        fInstructions.push_back({BuilderOp::push_condition_mask, {}});
    }

    void pop_condition_mask() {
        fInstructions.push_back({BuilderOp::pop_condition_mask, {}});
    }

    void merge_condition_mask() {
        fInstructions.push_back({BuilderOp::merge_condition_mask, {}});
    }

    void push_loop_mask() {
        fInstructions.push_back({BuilderOp::push_loop_mask, {}});
    }

    void pop_loop_mask() {
        fInstructions.push_back({BuilderOp::pop_loop_mask, {}});
    }

    void mask_off_loop_mask() {
        fInstructions.push_back({BuilderOp::mask_off_loop_mask, {}});
    }

    void reenable_loop_mask(SlotRange src) {
        SkASSERT(src.count == 1);
        fInstructions.push_back({BuilderOp::reenable_loop_mask, {src.index}});
    }

    void merge_loop_mask() {
        fInstructions.push_back({BuilderOp::merge_loop_mask, {}});
    }

    void push_return_mask() {
        fInstructions.push_back({BuilderOp::push_return_mask, {}});
    }

    void pop_return_mask() {
        fInstructions.push_back({BuilderOp::pop_return_mask, {}});
    }

    void mask_off_return_mask() {
        fInstructions.push_back({BuilderOp::mask_off_return_mask, {}});
    }

private:
    SkTArray<Instruction> fInstructions;
    int fNumLabels = 0;
    int fNumBranches = 0;
};

}  // namespace RP
}  // namespace SkSL
