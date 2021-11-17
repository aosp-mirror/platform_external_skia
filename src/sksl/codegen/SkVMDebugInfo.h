/*
 * Copyright 2021 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SKVMDEBUGINFO
#define SKVMDEBUGINFO

#include "src/sksl/ir/SkSLType.h"

#include <string>
#include <vector>

class SkStream;
class SkWStream;

namespace SkSL {

struct SkVMSlotInfo {
    /** The full name of this variable (without component): (e.g. `myArray[3].myStruct.myVector`) */
    std::string             name;
    /** The dimensions of this variable: 1x1 is a scalar, Nx1 is a vector, NxM is a matrix. */
    uint8_t                 columns = 1, rows = 1;
    /** Which component of the variable is this slot? (e.g. `vec4.z` is component 2) */
    uint8_t                 componentIndex = 0;
    /** What kind of numbers belong in this slot? */
    SkSL::Type::NumberKind  numberKind = SkSL::Type::NumberKind::kNonnumeric;
    /** Where is this variable located in the program? */
    int                     line;
};

class SkVMDebugInfo {
public:
    /** Serialization for .trace files. */
    bool readTrace(SkStream* r);
    void writeTrace(SkWStream* w) const;

    /** Write a human-readable dump of the DebugInfo to a .skvm file. */
    void dump(SkWStream* o) const;

    /** A 1:1 mapping of slot numbers to debug information. */
    std::vector<SkVMSlotInfo> fSlotInfo;
};

}  // namespace SkSL

#endif
