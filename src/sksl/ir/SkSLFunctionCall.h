/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SKSL_FUNCTIONCALL
#define SKSL_FUNCTIONCALL

#include "src/sksl/SkSLDefines.h"
#include "src/sksl/SkSLPosition.h"
#include "src/sksl/ir/SkSLExpression.h"
#include "src/sksl/ir/SkSLIRNode.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace SkSL {

class Context;
class FunctionDeclaration;
class Type;
enum class ModuleType : int8_t;
enum class OperatorPrecedence : uint8_t;

/**
 * A function invocation.
 */
class FunctionCall final : public Expression {
public:
    inline static constexpr Kind kIRNodeKind = Kind::kFunctionCall;

    FunctionCall(Position pos, const Type* type, const FunctionDeclaration* function,
                 ExpressionArray arguments, uint32_t stableID)
        : INHERITED(pos, kIRNodeKind, type)
        , fFunction(*function)
        , fArguments(std::move(arguments))
        , fStableID(stableID) {}

    // Resolves generic types, performs type conversion on arguments, determines return type, and
    // chooses a unique stable ID. Reports errors via the ErrorReporter.
    static std::unique_ptr<Expression> Convert(const Context& context,
                                               Position pos,
                                               const FunctionDeclaration& function,
                                               ExpressionArray arguments);

    static std::unique_ptr<Expression> Convert(const Context& context,
                                               Position pos,
                                               std::unique_ptr<Expression> functionValue,
                                               ExpressionArray arguments);

    // Creates a function call with a given stable ID; reports errors via ASSERT.
    static std::unique_ptr<Expression> Make(const Context& context,
                                            Position pos,
                                            const Type* returnType,
                                            const FunctionDeclaration& function,
                                            ExpressionArray arguments,
                                            uint32_t stableID);

    static const FunctionDeclaration* FindBestFunctionForCall(const Context& context,
                                                              const FunctionDeclaration* overloads,
                                                              const ExpressionArray& arguments);

    // Given a module type and an offset into the code, returns a stable ID.
    static uint32_t MakeStableID(ModuleType moduleType, Position pos);

    const FunctionDeclaration& function() const {
        return fFunction;
    }

    ExpressionArray& arguments() {
        return fArguments;
    }

    const ExpressionArray& arguments() const {
        return fArguments;
    }

    uint32_t stableID() const {
        return fStableID;
    }

    std::unique_ptr<Expression> clone(Position pos) const override;

    std::string description(OperatorPrecedence) const override;

private:
    const FunctionDeclaration& fFunction;
    ExpressionArray fArguments;

    // The stable ID is a 32-bit value which uniquely identifies this FunctionCall across an entire
    // SkSL program. It is preserved across calls to Clone() or Make(), unlike a pointer address.
    uint32_t fStableID;

    using INHERITED = Expression;
};

}  // namespace SkSL

#endif
