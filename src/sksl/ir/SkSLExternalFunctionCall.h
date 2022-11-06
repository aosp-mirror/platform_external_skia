/*
 * Copyright 2019 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SKSL_EXTERNALFUNCTIONCALL
#define SKSL_EXTERNALFUNCTIONCALL

#include "include/private/SkSLString.h"
#include "include/private/SkTArray.h"
#include "include/sksl/SkSLOperator.h"
#include "src/sksl/ir/SkSLExpression.h"
#include "src/sksl/ir/SkSLExternalFunction.h"
#include "src/sksl/ir/SkSLFunctionDeclaration.h"

namespace SkSL {

/**
 * An external function invocation.
 */
class ExternalFunctionCall final : public Expression {
public:
    inline static constexpr Kind kIRNodeKind = Kind::kExternalFunctionCall;

    ExternalFunctionCall(Position pos, const ExternalFunction* function, ExpressionArray arguments)
        : INHERITED(pos, kIRNodeKind, &function->type())
        , fFunction(*function)
        , fArguments(std::move(arguments)) {}

    ExpressionArray& arguments() {
        return fArguments;
    }

    const ExpressionArray& arguments() const {
        return fArguments;
    }

    const ExternalFunction& function() const {
        return fFunction;
    }

    std::unique_ptr<Expression> clone(Position pos) const override {
        return std::make_unique<ExternalFunctionCall>(pos, &this->function(),
                                                      this->arguments().clone());
    }

    std::string description(OperatorPrecedence) const override {
        std::string result = std::string(this->function().name()) + "(";
        auto separator = SkSL::String::Separator();
        for (const std::unique_ptr<Expression>& arg : this->arguments()) {
            result += separator();
            result += arg->description(OperatorPrecedence::kSequence);
        }
        result += ")";
        return result;
    }

private:
    const ExternalFunction& fFunction;
    ExpressionArray fArguments;

    using INHERITED = Expression;
};

}  // namespace SkSL

#endif
