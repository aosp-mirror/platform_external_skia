/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SKSL_SETTING
#define SKSL_SETTING

#include "include/sksl/SkSLPosition.h"
#include "src/sksl/ir/SkSLExpression.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace SkSL {

class Context;
class Type;

/**
 * Represents a compile-time constant setting, such as sk_Caps.integerSupport. These IRNodes are
 * used when assembling a module. These nodes are replaced with the value of the setting during
 * compilation when ShaderCaps are available.
 */
class Setting final : public Expression {
public:
    inline static constexpr Kind kExpressionKind = Kind::kSetting;

    Setting(Position pos, std::string_view name, const Type* type)
        : INHERITED(pos, kExpressionKind, type)
        , fName(std::move(name)) {}

    // Creates the current value of the associated caps bit as a Literal if ShaderCaps are
    // available, or a Setting IRNode when ShaderCaps are not known. Reports errors via the
    // ErrorReporter. (There's no failsafe Make equivalent, because there really isn't a good
    // fallback expression to produce when the `name` lookup fails. We wouldn't even know the
    // expected type.)
    static std::unique_ptr<Expression> Convert(const Context& context, Position pos,
                                               const std::string_view& name);

    // Converts a Setting expression to its actual ShaderCaps value (boolean true/false).
    std::unique_ptr<Expression> toLiteral(const Context& context) const;

    std::unique_ptr<Expression> clone(Position pos) const override {
        return std::make_unique<Setting>(pos, this->name(), &this->type());
    }

    const std::string_view& name() const {
        return fName;
    }

    std::string description() const override {
        return std::string(this->name());
    }

private:
    std::string_view fName;

    using INHERITED = Expression;
};

}  // namespace SkSL

#endif
