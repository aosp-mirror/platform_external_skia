/*
 * Copyright 2022 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkSpan.h"
#include "include/private/SkSLDefines.h"
#include "include/private/SkSLIRNode.h"
#include "include/private/SkSLLayout.h"
#include "include/private/SkSLModifiers.h"
#include "include/private/SkSLStatement.h"
#include "include/private/SkTArray.h"
#include "include/private/SkTHash.h"
#include "include/sksl/SkSLOperator.h"
#include "include/sksl/SkSLPosition.h"
#include "src/sksl/SkSLCompiler.h"
#include "src/sksl/codegen/SkSLRasterPipelineBuilder.h"
#include "src/sksl/codegen/SkSLRasterPipelineCodeGenerator.h"
#include "src/sksl/ir/SkSLBinaryExpression.h"
#include "src/sksl/ir/SkSLBlock.h"
#include "src/sksl/ir/SkSLConstructorCompound.h"
#include "src/sksl/ir/SkSLConstructorSplat.h"
#include "src/sksl/ir/SkSLExpression.h"
#include "src/sksl/ir/SkSLExpressionStatement.h"
#include "src/sksl/ir/SkSLFunctionDeclaration.h"
#include "src/sksl/ir/SkSLFunctionDefinition.h"
#include "src/sksl/ir/SkSLIfStatement.h"
#include "src/sksl/ir/SkSLLiteral.h"
#include "src/sksl/ir/SkSLReturnStatement.h"
#include "src/sksl/ir/SkSLType.h"
#include "src/sksl/ir/SkSLVarDeclarations.h"
#include "src/sksl/ir/SkSLVariable.h"
#include "src/sksl/ir/SkSLVariableReference.h"

#include <optional>
#include <string>
#include <vector>

namespace SkSL {
namespace RP {

class Generator {
public:
    Generator(const SkSL::Program& program) : fProgram(program) {}

    /** Converts the SkSL main() function into a set of Instructions. */
    bool writeProgram(const FunctionDefinition& function);

    /**
     * Converts an SkSL function into a set of Instructions. Returns nullopt if the function
     * contained unsupported statements or expressions.
     */
    std::optional<SlotRange> writeFunction(const IRNode& callSite,
                                           const FunctionDefinition& function,
                                           SkSpan<const SlotRange> args);

    /** Implements low-level slot creation; slots will not be known to the debugger. */
    SlotRange createSlots(int numSlots);

    /** Creates slots associated with an SkSL variable or return value. */
    SlotRange createSlots(std::string name,
                          const Type& type,
                          Position pos,
                          bool isFunctionReturnValue);

    /** Looks up the slots associated with an SkSL variable; creates the slot if necessary. */
    SlotRange getSlots(const Variable& v);

    /** Returns the number of slots needed by the program. */
    int slotCount() const { return fSlotCount; }

    /**
     * Looks up the slots associated with an SkSL function's return value; creates the range if
     * necessary. Note that recursion is never supported, so we don't need to maintain return values
     * in a stack; we can just statically allocate one slot per function call-site.
     */
    SlotRange getFunctionSlots(const IRNode& callSite, const FunctionDeclaration& f);

    /** The Builder stitches our instructions together into Raster Pipeline code. */
    Builder* builder() { return &fBuilder; }

    /** Appends a statement to the program. */
    bool writeStatement(const Statement& s);
    bool writeBlock(const Block& b);
    bool writeExpressionStatement(const ExpressionStatement& e);
    bool writeIfStatement(const IfStatement& i);
    bool writeReturnStatement(const ReturnStatement& r);
    bool writeVarDeclaration(const VarDeclaration& v);

    /** Pushes an expression to the value stack. */
    bool pushAssignmentExpression(const BinaryExpression& e);
    bool pushExpression(const Expression& e);
    bool pushBinaryExpression(const BinaryExpression& e);
    bool pushConstructorCompound(const ConstructorCompound& c);
    bool pushConstructorSplat(const ConstructorSplat& c);
    bool pushLiteral(const Literal& l);
    bool pushVariableReference(const VariableReference& v);

    /** Copies an expression from the value stack and copies it into slots. */
    void copyToSlotRange(SlotRange r) { fBuilder.copy_stack_to_slots(r); }

    /** Pops an expression from the value stack and copies it into slots. */
    void popToSlotRange(SlotRange r) { fBuilder.pop_slots(r); }
    void popToSlotRangeUnmasked(SlotRange r) { fBuilder.pop_slots_unmasked(r); }

    /** Pops an expression from the value stack and discards it. */
    void discardExpression(int slots) { fBuilder.discard_stack(slots); }

    /** Zeroes out a range of slots. */
    void zeroSlotRangeUnmasked(SlotRange r) { fBuilder.zero_slots_unmasked(r); }

    /** Expression utilities. */
    struct BinaryOps {
        BuilderOp fFloatOp;
        BuilderOp fSignedOp;
        BuilderOp fUnsignedOp;
        BuilderOp fBooleanOp;
    };

    bool assign(const Expression& e);
    bool binaryOp(SkSL::Type::NumberKind numberKind, int slots, const BinaryOps& ops);
    void foldWithOp(BuilderOp op, int elements);

private:
    [[maybe_unused]] const SkSL::Program& fProgram;
    Builder fBuilder;

    SkTHashMap<const IRNode*, SlotRange> fSlotMap;
    int fSlotCount = 0;

    SkTArray<SlotRange> fFunctionStack;
};

struct LValue {
    virtual ~LValue() = default;

    /**
     * Returns an LValue for the passed-in expression; if the expression isn't supported as an
     * LValue, returns nullptr.
     */
    static std::unique_ptr<LValue> Make(const Expression& e);

    /** Copies the top-of-stack value into this lvalue, without discarding it from the stack. */
    virtual bool store(Generator* gen) = 0;
};

struct VariableLValue : public LValue {
    VariableLValue(const Variable* v) : fVariable(v) {}

    bool store(Generator* gen) override {
        gen->copyToSlotRange(gen->getSlots(*fVariable));
        return true;
    }

    const Variable* fVariable;
};

std::unique_ptr<LValue> LValue::Make(const Expression& e) {
    if (e.is<VariableReference>()) {
        return std::make_unique<VariableLValue>(e.as<VariableReference>().variable());
    }
    // TODO(skia:13676): add support for other kinds of lvalues
    return nullptr;
}

SlotRange Generator::createSlots(int numSlots) {
    SlotRange range = {fSlotCount, numSlots};
    fSlotCount += numSlots;
    return range;
}

SlotRange Generator::createSlots(std::string name,
                                 const Type& type,
                                 Position pos,
                                 bool isFunctionReturnValue) {
    // TODO(skia:13676): `name`, `pos` and `isFunctionReturnValue` will be used by the debugger.
    // For now, ignore these and just create the raw slots.
    return this->createSlots(type.slotCount());
}

SlotRange Generator::getSlots(const Variable& v) {
    SlotRange* entry = fSlotMap.find(&v);
    if (entry != nullptr) {
        return *entry;
    }
    SlotRange range = this->createSlots(std::string(v.name()),
                                        v.type(),
                                        v.fPosition,
                                        /*isFunctionReturnValue=*/false);
    fSlotMap.set(&v, range);
    return range;
}

SlotRange Generator::getFunctionSlots(const IRNode& callSite, const FunctionDeclaration& f) {
    SlotRange* entry = fSlotMap.find(&callSite);
    if (entry != nullptr) {
        return *entry;
    }
    SlotRange range = this->createSlots("[" + std::string(f.name()) + "].result",
                                        f.returnType(),
                                        f.fPosition,
                                        /*isFunctionReturnValue=*/true);
    fSlotMap.set(&callSite, range);
    return range;
}

std::optional<SlotRange> Generator::writeFunction(const IRNode& callSite,
                                                  const FunctionDefinition& function,
                                                  SkSpan<const SlotRange> args) {
    fFunctionStack.push_back(this->getFunctionSlots(callSite, function.declaration()));

    if (!this->writeStatement(*function.body())) {
        return std::nullopt;
    }

    SlotRange functionResult = fFunctionStack.back();
    fFunctionStack.pop_back();
    return functionResult;
}

bool Generator::writeStatement(const Statement& s) {
    switch (s.kind()) {
        case Statement::Kind::kBlock:
            return this->writeBlock(s.as<Block>());

        case Statement::Kind::kExpression:
            return this->writeExpressionStatement(s.as<ExpressionStatement>());

        case Statement::Kind::kIf:
            return this->writeIfStatement(s.as<IfStatement>());

        case Statement::Kind::kNop:
            return true;

        case Statement::Kind::kReturn:
            return this->writeReturnStatement(s.as<ReturnStatement>());

        case Statement::Kind::kVarDeclaration:
            return this->writeVarDeclaration(s.as<VarDeclaration>());

        default:
            // Unsupported statement
            return false;
    }
}

bool Generator::writeBlock(const Block& b) {
    for (const std::unique_ptr<Statement>& stmt : b.children()) {
        if (!this->writeStatement(*stmt)) {
            return false;
        }
    }
    return true;
}

bool Generator::writeExpressionStatement(const ExpressionStatement& e) {
    if (!this->pushExpression(*e.expression())) {
        return false;
    }
    this->discardExpression(e.expression()->type().slotCount());
    return true;
}

bool Generator::writeIfStatement(const IfStatement& i) {
    if (!this->pushExpression(*i.test())) {
        return false;
    }

    // Apply the test-expression as a condition, then run the if-true branch.
    fBuilder.push_condition_mask();
    if (!this->writeStatement(*i.ifTrue())) {
        return false;
    }
    fBuilder.pop_condition_mask();

    if (i.ifFalse()) {
        // The test condition is still at the top of the stack. Negate it, apply it as a condition
        // mask again, and run the if-false branch.
        fBuilder.unary_op(BuilderOp::bitwise_not, /*slots=*/1);
        fBuilder.push_condition_mask();
        if (!this->writeStatement(*i.ifFalse())) {
            return false;
        }
        fBuilder.pop_condition_mask();
    }

    // Jettison the test condition.
    this->discardExpression(/*slots=*/1);
    return true;
}

bool Generator::writeReturnStatement(const ReturnStatement& r) {
    if (r.expression()) {
        if (!this->pushExpression(*r.expression())) {
            return false;
        }
        this->popToSlotRange(fFunctionStack.back());
    }
    fBuilder.update_return_mask();
    return true;
}

bool Generator::writeVarDeclaration(const VarDeclaration& v) {
    if (v.value()) {
        if (!this->pushExpression(*v.value())) {
            return false;
        }
        this->popToSlotRangeUnmasked(this->getSlots(*v.var()));
    } else {
        this->zeroSlotRangeUnmasked(this->getSlots(*v.var()));
    }
    return true;
}

bool Generator::pushExpression(const Expression& e) {
    switch (e.kind()) {
        case Expression::Kind::kBinary:
            return this->pushBinaryExpression(e.as<BinaryExpression>());

        case Expression::Kind::kConstructorCompound:
            return this->pushConstructorCompound(e.as<ConstructorCompound>());

        case Expression::Kind::kConstructorSplat:
            return this->pushConstructorSplat(e.as<ConstructorSplat>());

        case Expression::Kind::kLiteral:
            return this->pushLiteral(e.as<Literal>());

        case Expression::Kind::kVariableReference:
            return this->pushVariableReference(e.as<VariableReference>());

        default:
            // Unsupported expression
            return false;
    }
}

bool Generator::binaryOp(SkSL::Type::NumberKind numberKind, int slots, const BinaryOps& ops) {
    BuilderOp op = BuilderOp::unsupported;
    switch (numberKind) {
        case Type::NumberKind::kFloat:    op = ops.fFloatOp;    break;
        case Type::NumberKind::kSigned:   op = ops.fSignedOp;   break;
        case Type::NumberKind::kUnsigned: op = ops.fUnsignedOp; break;
        case Type::NumberKind::kBoolean:  op = ops.fBooleanOp;  break;
        default:                          SkUNREACHABLE;
    }
    if (op == BuilderOp::unsupported) {
        return false;
    }
    fBuilder.binary_op(op, slots);
    return true;
}

bool Generator::assign(const Expression& e) {
    std::unique_ptr<LValue> lvalue = LValue::Make(e);
    return lvalue && lvalue->store(this);
}

void Generator::foldWithOp(BuilderOp op, int elements) {
    // Fold the top N elements on the stack using an op, e.g. (A && (B && C)) -> D.
    for (; elements > 1; elements--) {
        fBuilder.binary_op(op, /*slots=*/1);
    }
}

bool Generator::pushBinaryExpression(const BinaryExpression& e) {
    // TODO: add support for non-matching types (e.g. matrix-vector ops)
    if (!e.left()->type().matches(e.right()->type())) {
        return false;
    }

    // Handle simple assignment (`var = expr`).
    if (e.getOperator().kind() == OperatorKind::EQ) {
        return this->pushExpression(*e.right()) &&
               this->assign(*e.left());
    }

    const Type& type = e.left()->type();
    Type::NumberKind numberKind = type.componentType().numberKind();
    Operator basicOp = e.getOperator().removeAssignment();

    switch (basicOp.kind()) {
        case OperatorKind::GT:
        case OperatorKind::GTEQ:
            // We replace `x > y` with `y < x`, and `x >= y` with `y <= x`.
            this->pushExpression(*e.right());
            this->pushExpression(*e.left());
            break;

        default:
            this->pushExpression(*e.left());
            this->pushExpression(*e.right());
            break;
    }

    switch (basicOp.kind()) {
        case OperatorKind::PLUS: {
            static constexpr auto kPlus = BinaryOps{BuilderOp::add_n_floats,
                                                    BuilderOp::add_n_ints,
                                                    BuilderOp::add_n_ints,
                                                    BuilderOp::unsupported};
            if (!this->binaryOp(numberKind, type.slotCount(), kPlus)) {
                return false;
            }
            break;
        }
        case OperatorKind::LT:
        case OperatorKind::GT: {
            // TODO(skia:13676): add support for unsigned <
            static constexpr auto kLessThan = BinaryOps{BuilderOp::cmplt_n_floats,
                                                        BuilderOp::cmplt_n_ints,
                                                        BuilderOp::unsupported,
                                                        BuilderOp::unsupported};
            if (!this->binaryOp(numberKind, type.slotCount(), kLessThan)) {
                return false;
            }
            SkASSERT(type.slotCount() == 1);  // operator< only works with scalar types
            break;
        }
        case OperatorKind::LTEQ:
        case OperatorKind::GTEQ: {
            // TODO(skia:13676): add support for unsigned <=
            static constexpr auto kLessThanEquals = BinaryOps{BuilderOp::cmple_n_floats,
                                                              BuilderOp::cmple_n_ints,
                                                              BuilderOp::unsupported,
                                                              BuilderOp::unsupported};
            if (!this->binaryOp(numberKind, type.slotCount(), kLessThanEquals)) {
                return false;
            }
            SkASSERT(type.slotCount() == 1);  // operator<= only works with scalar types
            break;
        }
        case OperatorKind::EQEQ: {
            static constexpr auto kEquals = BinaryOps{BuilderOp::cmpeq_n_floats,
                                                      BuilderOp::cmpeq_n_ints,
                                                      BuilderOp::cmpeq_n_ints,
                                                      BuilderOp::cmpeq_n_ints};
            if (!this->binaryOp(numberKind, type.slotCount(), kEquals)) {
                return false;
            }
            this->foldWithOp(BuilderOp::bitwise_and, type.slotCount());  // fold vector result
            break;
        }
        case OperatorKind::NEQ: {
            static constexpr auto kNotEquals = BinaryOps{BuilderOp::cmpne_n_floats,
                                                         BuilderOp::cmpne_n_ints,
                                                         BuilderOp::cmpne_n_ints,
                                                         BuilderOp::cmpne_n_ints};
            if (!this->binaryOp(numberKind, type.slotCount(), kNotEquals)) {
                return false;
            }
            this->foldWithOp(BuilderOp::bitwise_or, type.slotCount());  // fold vector result
            break;
        }
        default:
            return false;
    }

    // Handle compound assignment (`var *= expr`).
    if (e.getOperator().isAssignment()) {
        return this->assign(*e.left());
    }

    return true;
}

bool Generator::pushConstructorCompound(const ConstructorCompound& c) {
    for (const std::unique_ptr<Expression> &arg : c.arguments()) {
        if (!this->pushExpression(*arg)) {
            return false;
        }
    }
    return true;
}

bool Generator::pushConstructorSplat(const ConstructorSplat& c) {
    if (!this->pushExpression(*c.argument())) {
        return false;
    }
    fBuilder.duplicate(c.type().slotCount() - 1);
    return true;
}

bool Generator::pushLiteral(const Literal& l) {
    switch (l.type().numberKind()) {
        case Type::NumberKind::kFloat:
            fBuilder.push_literal_f(l.floatValue());
            return true;

        case Type::NumberKind::kSigned:
            fBuilder.push_literal_i(l.intValue());
            return true;

        case Type::NumberKind::kUnsigned:
            fBuilder.push_literal_u(l.intValue());
            return true;

        case Type::NumberKind::kBoolean:
            fBuilder.push_literal_i(l.boolValue() ? ~0 : 0);
            return true;

        default:
            SkUNREACHABLE;
    }
}

bool Generator::pushVariableReference(const VariableReference& v) {
    fBuilder.push_slots(this->getSlots(*v.variable()));
    return true;
}

bool Generator::writeProgram(const FunctionDefinition& function) {
    // Assign slots to the parameters of main; copy src and dst into those slots as appropriate.
    SkSTArray<2, SlotRange> args;
    for (const SkSL::Variable* param : function.declaration().parameters()) {
        switch (param->modifiers().fLayout.fBuiltin) {
            case SK_MAIN_COORDS_BUILTIN: {
                // Coordinates are passed via RG.
                SlotRange fragCoord = this->getSlots(*param);
                SkASSERT(fragCoord.count == 2);
                fBuilder.store_src_rg(fragCoord);
                args.push_back(fragCoord);
                break;
            }
            case SK_INPUT_COLOR_BUILTIN: {
                // Input colors are passed via RGBA.
                SlotRange srcColor = this->getSlots(*param);
                SkASSERT(srcColor.count == 4);
                fBuilder.store_src(srcColor);
                args.push_back(srcColor);
                break;
            }
            case SK_DEST_COLOR_BUILTIN: {
                // Dest colors are passed via dRGBA.
                SlotRange destColor = this->getSlots(*param);
                SkASSERT(destColor.count == 4);
                fBuilder.store_dst(destColor);
                args.push_back(destColor);
                break;
            }
            default: {
                SkDEBUGFAIL("Invalid parameter to main()");
                return false;
            }
        }
    }

    // Initialize the program.
    fBuilder.init_lane_masks();

    // Invoke main().
    std::optional<SlotRange> mainResult = this->writeFunction(function, function, args);
    if (!mainResult.has_value()) {
        return false;
    }

    // Move the result of main() from slots into RGBA. Allow dRGBA to remain in a trashed state.
    SkASSERT(mainResult->count == 4);
    fBuilder.load_src(*mainResult);
    return true;
}

}  // namespace RP

std::unique_ptr<RP::Program> MakeRasterPipelineProgram(const SkSL::Program& program,
                                                       const FunctionDefinition& function) {
    // TODO(skia:13676): add mechanism for uniform passing
    RP::Generator generator(program);
    if (!generator.writeProgram(function)) {
        return nullptr;
    }
    return generator.builder()->finish(generator.slotCount());
}

}  // namespace SkSL
