/*
 * Copyright 2021 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/private/SkTOptional.h"
#include "src/sksl/tracing/SkVMDebugTrace.h"
#include "src/utils/SkBitSet.h"

namespace SkSL {

/**
 * Plays back a SkVM debug trace, allowing its contents to be viewed like a traditional debugger.
 */
class SkVMDebugTracePlayer {
public:
    /** Resets playback. */
    void reset(sk_sp<SkVMDebugTrace> trace);

    /** Advances the simulation to the next Line op. */
    void step();

    /** Advances the simulation to the next Line op, skipping past matched Enter/Exit pairs. */
    void stepOver();

    /** Advances the simulation until we exit from the current stack frame. */
    void stepOut();

    /** Returns true if we have reached the end of the trace. */
    bool traceHasCompleted() const;

    /** Retrieves the cursor position. */
    size_t cursor() { return fCursor; }

    /** Retrieves the current line. */
    int32_t getCurrentLine() const;

    /** Returns the call stack as an array of FunctionInfo indices. */
    std::vector<int> getCallStack() const;

    /** Returns the size of the call stack. */
    int getStackDepth() const;

    /** Returns every line number actually reached in the debug trace. */
    const std::unordered_set<int>& getLineNumbersReached() const { return fLineNumbers; }

    /** Returns variables from a stack frame, or from global scope. */
    struct VariableData {
        int      fSlotIndex;
        bool     fDirty;  // has this slot been written-to since the last step call?
        int32_t  fValue;  // caller must type-pun bits to float/bool based on slot type
    };
    std::vector<VariableData> getLocalVariables(int stackFrameIndex) const;
    std::vector<VariableData> getGlobalVariables() const;

private:
    /**
     * Executes the trace op at the passed-in cursor position. Returns true if we've reached a line
     * or exit trace op, which indicate a stopping point.
     */
    bool execute(size_t position);

    /**
     * Cleans up temporary state between steps, such as the dirty mask and function return values.
     */
    void tidy();

    /** Updates fWriteTime for the entire variable at a given slot. */
    void updateVariableWriteTime(int slotIdx, size_t writeTime);

    /** Returns a vector of the indices and values of each slot that is enabled in `bits`. */
    std::vector<VariableData> getVariablesForDisplayMask(const SkBitSet& bits) const;

    struct StackFrame {
        int32_t   fFunction;     // from fFuncInfo
        int32_t   fLine;         // our current line number within the function
        SkBitSet  fDisplayMask;  // the variable slots which have been touched in this function
    };
    struct Slot {
        int32_t   fValue;        // values in each slot
        int       fScope;        // the scope value of each slot
        size_t    fWriteTime;    // when was the variable in this slot most recently written?
                                 // (by cursor position)
    };
    sk_sp<SkVMDebugTrace>       fDebugTrace;
    size_t                      fCursor = 0;      // position of the read head
    int                         fScope = 0;       // the current scope depth (as tracked by
                                                  // trace_scope)
    std::vector<Slot>           fSlots;           // the array of all slots
    std::vector<StackFrame>     fStack;           // the execution stack
    skstd::optional<SkBitSet>   fDirtyMask;       // variable slots touched during the most-recently
                                                  // executed step
    skstd::optional<SkBitSet>   fReturnValues;    // variable slots containing return values
    std::unordered_set<int>     fLineNumbers;     // every line number reached during execution
};

}  // namespace SkSL
