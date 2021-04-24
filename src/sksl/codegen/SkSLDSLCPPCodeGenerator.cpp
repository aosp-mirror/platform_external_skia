/*
 * Copyright 2021 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/sksl/codegen/SkSLDSLCPPCodeGenerator.h"

#include "include/private/SkSLSampleUsage.h"
#include "src/sksl/SkSLAnalysis.h"
#include "src/sksl/SkSLCPPUniformCTypes.h"
#include "src/sksl/SkSLCompiler.h"
#include "src/sksl/codegen/SkSLHCodeGenerator.h"
#include "src/sksl/ir/SkSLBlock.h"
#include "src/sksl/ir/SkSLEnum.h"
#include "src/sksl/ir/SkSLExpressionStatement.h"

#include <algorithm>

#if defined(SKSL_STANDALONE) || GR_TEST_UTILS

namespace SkSL {

static bool needs_uniform_var(const Variable& var) {
    return var.modifiers().fFlags & Modifiers::kUniform_Flag;
}

static const char* get_scalar_type_name(const Context& context, const Type& type) {
    if (type == *context.fTypes.fHalf) {
        return "Half";
    } else if (type == *context.fTypes.fFloat) {
        return "Float";
    } else if (type.isSigned()) {
        return "Int";
    } else if (type.isBoolean()) {
        return "Bool";
    }
    // TODO: support for unsigned types
    SkDEBUGFAIL("unsupported scalar type");
    return "Float";
}

DSLCPPCodeGenerator::DSLCPPCodeGenerator(const Context* context, const Program* program,
                                         ErrorReporter* errors, String name, OutputStream* out)
    : INHERITED(context, program, errors, out)
    , fName(std::move(name))
    , fFullName(String::printf("Gr%s", fName.c_str()))
    , fSectionAndParameterHelper(program, *errors) {
    fLineEnding = "\n";
    fTextureFunctionOverride = "sample";
}

void DSLCPPCodeGenerator::writef(const char* s, va_list va) {
    static constexpr int BUFFER_SIZE = 1024;
    va_list copy;
    va_copy(copy, va);
    char buffer[BUFFER_SIZE];
    int length = std::vsnprintf(buffer, BUFFER_SIZE, s, va);
    if (length < BUFFER_SIZE) {
        fOut->write(buffer, length);
    } else {
        std::unique_ptr<char[]> heap(new char[length + 1]);
        vsprintf(heap.get(), s, copy);
        fOut->write(heap.get(), length);
    }
    va_end(copy);
}

void DSLCPPCodeGenerator::writef(const char* s, ...) {
    va_list va;
    va_start(va, s);
    this->writef(s, va);
    va_end(va);
}

void DSLCPPCodeGenerator::writeHeader() {
}

bool DSLCPPCodeGenerator::usesPrecisionModifiers() const {
    return false;
}

static String default_value(const Type& type) {
    if (type.isBoolean()) {
        return "false";
    }
    switch (type.typeKind()) {
        case Type::TypeKind::kScalar: return "0";
        case Type::TypeKind::kVector: return type.name() + "(0)";
        case Type::TypeKind::kMatrix: return type.name() + "(1)";
        default: SK_ABORT("unsupported default_value type");
    }
}

static String default_value(const Variable& var) {
    if (var.modifiers().fLayout.fCType == SkSL::Layout::CType::kSkPMColor4f) {
        return "{SK_FloatNaN, SK_FloatNaN, SK_FloatNaN, SK_FloatNaN}";
    }
    return default_value(var.type());
}

static bool is_private(const Variable& var) {
    const Modifiers& modifiers = var.modifiers();
    return !(modifiers.fFlags & Modifiers::kUniform_Flag) &&
           !(modifiers.fFlags & Modifiers::kIn_Flag) &&
           var.storage() == Variable::Storage::kGlobal &&
           modifiers.fLayout.fBuiltin == -1;
}

static bool is_uniform_in(const Variable& var) {
    const Modifiers& modifiers = var.modifiers();
    return (modifiers.fFlags & Modifiers::kUniform_Flag) &&
           (modifiers.fFlags & Modifiers::kIn_Flag);
}

String DSLCPPCodeGenerator::formatRuntimeValue(const Type& type,
                                               const Layout& layout,
                                               const String& cppCode,
                                               std::vector<String>* formatArgs) {
    if (type.isArray()) {
        String result("[");
        const char* separator = "";
        for (int i = 0; i < type.columns(); i++) {
            result += separator + this->formatRuntimeValue(type.componentType(), layout,
                                                           "(" + cppCode + ")[" + to_string(i) +
                                                           "]", formatArgs);
            separator = ",";
        }
        result += "]";
        return result;
    }
    if (type.isFloat()) {
        formatArgs->push_back(cppCode);
        return "%f";
    }
    if (type == *fContext.fTypes.fInt) {
        formatArgs->push_back(cppCode);
        return "%d";
    }
    if (type == *fContext.fTypes.fBool) {
        formatArgs->push_back("!!(" + cppCode + ")");
        return "%d";
    }
    if (type == *fContext.fTypes.fFloat2 || type == *fContext.fTypes.fHalf2) {
        formatArgs->push_back(cppCode + ".fX");
        formatArgs->push_back(cppCode + ".fY");
        return type.name() + "(%f, %f)";
    }
    if (type == *fContext.fTypes.fFloat3 || type == *fContext.fTypes.fHalf3) {
        formatArgs->push_back(cppCode + ".fX");
        formatArgs->push_back(cppCode + ".fY");
        formatArgs->push_back(cppCode + ".fZ");
        return type.name() + "(%f, %f, %f)";
    }
    if (type == *fContext.fTypes.fFloat4 || type == *fContext.fTypes.fHalf4) {
        switch (layout.fCType) {
            case Layout::CType::kSkPMColor:
                formatArgs->push_back("SkGetPackedR32(" + cppCode + ") / 255.0");
                formatArgs->push_back("SkGetPackedG32(" + cppCode + ") / 255.0");
                formatArgs->push_back("SkGetPackedB32(" + cppCode + ") / 255.0");
                formatArgs->push_back("SkGetPackedA32(" + cppCode + ") / 255.0");
                break;
            case Layout::CType::kSkPMColor4f:
                formatArgs->push_back(cppCode + ".fR");
                formatArgs->push_back(cppCode + ".fG");
                formatArgs->push_back(cppCode + ".fB");
                formatArgs->push_back(cppCode + ".fA");
                break;
            case Layout::CType::kSkV4:
                formatArgs->push_back(cppCode + ".x");
                formatArgs->push_back(cppCode + ".y");
                formatArgs->push_back(cppCode + ".z");
                formatArgs->push_back(cppCode + ".w");
                break;
            case Layout::CType::kSkRect:
            case Layout::CType::kDefault:
                formatArgs->push_back(cppCode + ".left()");
                formatArgs->push_back(cppCode + ".top()");
                formatArgs->push_back(cppCode + ".right()");
                formatArgs->push_back(cppCode + ".bottom()");
                break;
            default:
                SkASSERT(false);
        }
        return type.name() + "(%f, %f, %f, %f)";
    }
    if (type.isMatrix()) {
        SkASSERT(type.componentType() == *fContext.fTypes.fFloat ||
                 type.componentType() == *fContext.fTypes.fHalf);

        String format = type.name() + "(";
        for (int c = 0; c < type.columns(); ++c) {
            for (int r = 0; r < type.rows(); ++r) {
                formatArgs->push_back(String::printf("%s.rc(%d, %d)", cppCode.c_str(), r, c));
                format += "%f, ";
            }
        }

        // Replace trailing ", " with ")".
        format.pop_back();
        format.back() = ')';
        return format;
    }
    if (type.isEnum()) {
        formatArgs->push_back("(int) " + cppCode);
        return "%d";
    }
    if (type == *fContext.fTypes.fInt4 ||
        type == *fContext.fTypes.fShort4 ||
        type == *fContext.fTypes.fByte4) {
        formatArgs->push_back(cppCode + ".left()");
        formatArgs->push_back(cppCode + ".top()");
        formatArgs->push_back(cppCode + ".right()");
        formatArgs->push_back(cppCode + ".bottom()");
        return type.name() + "(%d, %d, %d, %d)";
    }

    SkDEBUGFAILF("unsupported runtime value type '%s'\n", String(type.name()).c_str());
    return "";
}

void DSLCPPCodeGenerator::writeSwizzle(const Swizzle& swizzle) {
    // Confirm that the component array only contains X/Y/Z/W.
    SkASSERT(std::all_of(swizzle.components().begin(), swizzle.components().end(),
             [](int8_t component) {
                 return component >= SwizzleComponent::X && component <= SwizzleComponent::W;
             }));

    if (fCPPMode) {
        // no support for multiple swizzle components yet
        SkASSERT(swizzle.components().size() == 1);
        this->writeExpression(*swizzle.base(), Precedence::kPostfix);
        switch (swizzle.components().front()) {
            case SwizzleComponent::X: this->write(".left()");   break;
            case SwizzleComponent::Y: this->write(".top()");    break;
            case SwizzleComponent::Z: this->write(".right()");  break;
            case SwizzleComponent::W: this->write(".bottom()"); break;
        }
    } else {
        if (swizzle.components().size() == 1) {
            // For single-element swizzles, we can generate nicer-looking code.
            this->writeExpression(*swizzle.base(), Precedence::kPostfix);
            switch (swizzle.components().front()) {
                case SwizzleComponent::X: this->write(".x()"); break;
                case SwizzleComponent::Y: this->write(".y()"); break;
                case SwizzleComponent::Z: this->write(".z()"); break;
                case SwizzleComponent::W: this->write(".w()"); break;
            }
        } else {
            this->write("Swizzle(");
            this->writeExpression(*swizzle.base(), Precedence::kSequence);
            for (int8_t component : swizzle.components()) {
                switch (component) {
                    case SwizzleComponent::X: this->write(", X"); break;
                    case SwizzleComponent::Y: this->write(", Y"); break;
                    case SwizzleComponent::Z: this->write(", Z"); break;
                    case SwizzleComponent::W: this->write(", W"); break;
                }
            }
            this->write(")");
        }
    }
}

void DSLCPPCodeGenerator::writeTernaryExpression(const TernaryExpression& t,
                                                 Precedence parentPrecedence) {
    if (fCPPMode) {
        INHERITED::writeTernaryExpression(t, parentPrecedence);
    } else {
        this->write("Select(");
        this->writeExpression(*t.test(), Precedence::kSequence);
        this->write(", /*If True:*/ ");
        this->writeExpression(*t.ifTrue(), Precedence::kSequence);
        this->write(", /*If False:*/ ");
        this->writeExpression(*t.ifFalse(), Precedence::kSequence);
        this->write(")");
    }
}

void DSLCPPCodeGenerator::writeVariableReference(const VariableReference& ref) {
    const Variable& var = *ref.variable();

    if (!fCPPMode) {
        if (var.modifiers().fLayout.fBuiltin == SK_MAIN_COORDS_BUILTIN) {
            this->write("sk_SampleCoord()");
            fAccessSampleCoordsDirectly = true;
            return;
        }
    }

    this->write(this->getVariableCppName(var));
}

int DSLCPPCodeGenerator::getChildFPIndex(const Variable& var) const {
    int index = 0;
    for (const ProgramElement* p : fProgram.elements()) {
        if (p->is<GlobalVarDeclaration>()) {
            const VarDeclaration& decl =
                    p->as<GlobalVarDeclaration>().declaration()->as<VarDeclaration>();
            if (&decl.var() == &var) {
                return index;
            } else if (decl.var().type().isFragmentProcessor()) {
                ++index;
            }
        }
    }
    SkDEBUGFAILF("child fragment processor for '%s' not found", var.description().c_str());
    return 0;
}

void DSLCPPCodeGenerator::writeFunctionCall(const FunctionCall& c) {
    const FunctionDeclaration& function = c.function();
    if (function.isBuiltin() && function.name() == "sample") {
        // The first argument to sample() must be a fragment processor. (Old-school samplers are no
        // longer supported in FP files.)
        const ExpressionArray& arguments = c.arguments();
        SkASSERT(arguments.size() >= 1 && arguments.size() <= 3);
        const Expression& fpArgument = *arguments.front();
        SkASSERT(fpArgument.type().isFragmentProcessor());

        // We can't look up the child FP index unless the fragment-processor is a real variable.
        if (!fpArgument.is<VariableReference>()) {
            fErrors.error(fpArgument.fOffset,
                          "sample()'s fragmentProcessor argument must be a variable reference");
            return;
        }

        // Pass the index of the fragment processor, and all the other arguments as-is.
        int childFPIndex = this->getChildFPIndex(*fpArgument.as<VariableReference>().variable());
        this->writef("SampleChild(%d", childFPIndex);

        for (int index = 1; index < arguments.count(); ++index) {
            this->write(", ");
            this->writeExpression(*arguments[index], Precedence::kSequence);
        }
        this->write(")");
        return;
    }

    if (function.isBuiltin()) {
        static const auto* kBuiltinNames = new std::unordered_map<String, String>{
                {"abs", "Abs"},
                {"all", "All"},
                {"any", "Any"},
                {"ceil", "Ceil"},
                {"clamp", "Clamp"},
                {"cos", "Cos"},
                {"cross", "Cross"},
                {"degrees", "Degrees"},
                {"distance", "Distance"},
                {"dot", "Dot"},
                {"equal", "Equal"},
                {"exp", "Exp"},
                {"exp2", "Exp2"},
                {"faceforward", "Faceforward"},
                {"floor", "Floor"},
                {"fract", "Fract"},
                {"greaterThan", "GreaterThan"},
                {"greaterThanEqual", "GreaterThanEqual"},
                {"inversesqrt", "Inversesqrt"},
                {"inverse", "Inverse"},
                {"length", "Length"},
                {"lessThan", "LessThan"},
                {"lessThanEqual", "LessThanEqual"},
                {"log", "Log"},
                {"max", "Max"},
                {"min", "Min"},
                {"mix", "Mix"},
                {"mod", "Mod"},
                {"normalize", "Normalize"},
                {"not", "Not"},
                {"pow", "Pow"},
                {"radians", "Radians"},
                {"reflect", "Reflect"},
                {"refract", "Refract"},
                {"saturate", "Saturate"},
                {"sign", "Sign"},
                {"sin", "Sin"},
                {"smoothstep", "Smoothstep"},
                {"sqrt", "Sqrt"},
                {"step", "Step"},
                {"tan", "Tan"},
                {"unpremul", "Unpremul"}};

        auto iter = kBuiltinNames->find(function.name());
        if (iter == kBuiltinNames->end()) {
            fErrors.error(c.fOffset, "unrecognized built-in function '" + function.name() + "'");
            return;
        }

        this->write(iter->second);
        this->write("(");
        const char* separator = "";
        for (const std::unique_ptr<Expression>& argument : c.arguments()) {
            this->write(separator);
            separator = ", ";
            this->writeExpression(*argument, Precedence::kSequence);
        }
        this->write(")");
        return;
    }

    SK_ABORT("not yet implemented: helper function support for DSL");
}

void DSLCPPCodeGenerator::prepareHelperFunction(const FunctionDeclaration& decl) {
    if (decl.isBuiltin() || decl.isMain()) {
        return;
    }

    SK_ABORT("not yet implemented: helper functions in DSL");
}

void DSLCPPCodeGenerator::prototypeHelperFunction(const FunctionDeclaration& decl) {
    SK_ABORT("not yet implemented: function prototypes in DSL");
}

void DSLCPPCodeGenerator::writeFunction(const FunctionDefinition& f) {
    const FunctionDeclaration& decl = f.declaration();
    if (decl.isBuiltin()) {
        return;
    }
    fFunctionHeader.clear();
    OutputStream* oldOut = fOut;
    StringStream buffer;
    fOut = &buffer;
    if (decl.isMain()) {
        fInMain = true;
        this->writeFunctionBody(f.body()->as<Block>());
        fInMain = false;

        fOut = oldOut;
        this->write(fFunctionHeader);
        this->write(buffer.str());
    } else {
        SK_ABORT("not yet implemented: helper functions in DSL");
    }
}

void DSLCPPCodeGenerator::writeFunctionBody(const Block& b) {
    // At the top level of a function, DSL statements need to be emitted as individual C++
    // statements instead of being comma-separated expressions in a Block. (You could technically
    // emit the entire function as one big comma-separated Block, and it would work, but you'd wrap
    // everything with an extra unnecessary scope.)
    for (const std::unique_ptr<Statement>& stmt : b.children()) {
        if (!stmt->isEmpty()) {
            this->writeStatement(*stmt);
            this->write(";\n");
        }
    }
}

void DSLCPPCodeGenerator::writeBlock(const Block& b) {
    if (b.isEmpty()) {
        // Write empty Blocks as an empty Statement, whether or not it was scoped.
        // This is the simplest way to emit a valid Statement for an unscoped empty Block.
        this->write("Statement()");
        return;
    }

    if (b.isScope()) {
        this->write("Block(");
    }

    const char* separator = "";
    for (const std::unique_ptr<Statement>& stmt : b.children()) {
        if (!stmt->isEmpty()) {
            this->write(separator);
            separator = ", ";

            this->writeStatement(*stmt);
        }
    }

    if (b.isScope()) {
        this->write(")");
    }
}

void DSLCPPCodeGenerator::writeReturnStatement(const ReturnStatement& r) {
    this->write("Return(");
    if (r.expression()) {
        this->writeExpression(*r.expression(), Precedence::kTopLevel);
    }
    this->write(")");
}

void DSLCPPCodeGenerator::writeIfStatement(const IfStatement& stmt) {
    this->write("If(");
    this->writeExpression(*stmt.test(), Precedence::kTopLevel);
    this->write(", /*Then:*/ ");
    this->writeStatement(*stmt.ifTrue());
    if (stmt.ifFalse()) {
        this->write(", /*Else:*/ ");
        this->writeStatement(*stmt.ifFalse());
    }
    this->write(")");
}

static bool variable_exists_with_name(const std::unordered_map<const Variable*, String>& varMap,
                                      const String& trialName) {
    for (const auto& [varPtr, varName] : varMap) {
        if (varName == trialName) {
            return true;
        }
    }
    return false;
}

const char* DSLCPPCodeGenerator::getVariableCppName(const Variable& var) {
    String& cppName = fVariableCppNames[&var];
    if (cppName.empty()) {
        if (!variable_exists_with_name(fVariableCppNames, var.name())) {
            // Nothing needs to change; use the name as-is.
            cppName = var.name();
        } else {
            // Another variable with the same name exists in the function, at a different scope.
            // Append a numeric prefix to disambiguate the two. (This check could be more efficient,
            // but it really doesn't matter much; this case is rare, and we don't compile DSL CPPs
            // at Skia runtime.)
            for (int prefix=0;; ++prefix) {
                String prefixedName = String::printf("_%d_%.*s", prefix, (int)var.name().size(),
                                                                 var.name().data());
                if (!variable_exists_with_name(fVariableCppNames, prefixedName)) {
                    cppName = std::move(prefixedName);
                    break;
                }
            }
        }
    }

    return cppName.c_str();
}

void DSLCPPCodeGenerator::writeVar(const Variable& var) {
    this->write("Var ");
    this->write(this->getVariableCppName(var));
    this->write("(");
    this->write(this->getDSLModifiers(var.modifiers()));
    this->write(", ");
    this->write(this->getDSLType(var.type()));
    this->write(", \"");
    this->write(this->getVariableCppName(var));
    this->write("\"");
    if (var.initialValue()) {
        this->write(", ");
        this->writeExpression(*var.initialValue(), Precedence::kTopLevel);
    }
    this->write(");\n");
}

void DSLCPPCodeGenerator::writeVarDeclaration(const VarDeclaration& varDecl, bool global) {
    if (!global) {
        const Variable& var = varDecl.var();
        {
            // We want to divert our output into fFunctionHeader, but fFunctionHeader is just a
            // String, not a StringStream. So instead, we divert into a temporary stream and append
            // that stream into fFunctionHeader afterwards.
            StringStream stream;
            AutoOutputStream divert(this, &stream);

            this->writeVar(var);

            fFunctionHeader += stream.str();
        }

        this->write("Declare(");
        this->write(this->getVariableCppName(var));
        this->write(")");
    }
}

void DSLCPPCodeGenerator::writeForStatement(const ForStatement& f) {
    // Emit loops of the form 'for (; test;)' as 'while (test)', which is probably how they started.
    if (!f.initializer() && f.test() && !f.next()) {
        this->write("While(");
        this->writeExpression(*f.test(), Precedence::kTopLevel);
        this->write(", ");
        this->writeStatement(*f.statement());
        this->write(")");
        return;
    }

    this->write("For(");
    if (f.initializer() && !f.initializer()->isEmpty()) {
        this->writeStatement(*f.initializer());
        this->write(", ");
    } else {
        this->write("Statement(), ");
    }
    if (f.test()) {
        this->writeExpression(*f.test(), Precedence::kTopLevel);
        this->write(", ");
    } else {
        this->write("Expression(), ");
    }
    if (f.next()) {
        this->writeExpression(*f.next(), Precedence::kTopLevel);
        this->write(", /*Body:*/ ");
    } else {
        this->write("Expression(), /*Body:*/ ");
    }
    this->writeStatement(*f.statement());
    this->write(")");
}

void DSLCPPCodeGenerator::writeDoStatement(const DoStatement& d) {
    this->write("Do(");
    this->writeStatement(*d.statement());
    this->write(", /*While:*/ ");
    this->writeExpression(*d.test(), Precedence::kTopLevel);
    this->write(")");
}

void DSLCPPCodeGenerator::writeSwitchStatement(const SwitchStatement& s) {
    this->write("Switch(");
    this->writeExpression(*s.value(), Precedence::kTopLevel);
    for (const std::unique_ptr<Statement>& stmt : s.cases()) {
        const SwitchCase& c = stmt->as<SwitchCase>();
        if (c.value()) {
            this->write(",\n    Case(");
            this->writeExpression(*c.value(), Precedence::kTopLevel);
            if (!c.statement()->isEmpty()) {
                this->write(", ");
                this->writeStatement(*c.statement());
            }
        } else {
            this->write(",\n    Default(");
            if (!c.statement()->isEmpty()) {
                this->writeStatement(*c.statement());
            }
        }
        this->write(")");
    }
    this->write(")");
}

void DSLCPPCodeGenerator::writeCastConstructor(const AnyConstructor& c,
                                               Precedence parentPrecedence) {
    return this->writeAnyConstructor(c, parentPrecedence);
}

void DSLCPPCodeGenerator::writeAnyConstructor(const AnyConstructor& c,
                                              Precedence parentPrecedence) {
    if (c.type().isArray() || c.type().isStruct()) {
        SK_ABORT("not yet supported: array/struct construction in DSL");
    }

    INHERITED::writeAnyConstructor(c, parentPrecedence);
}

String DSLCPPCodeGenerator::getTypeName(const Type& type) {
    switch (type.typeKind()) {
        case Type::TypeKind::kScalar:
            return get_scalar_type_name(fContext, type);

        case Type::TypeKind::kVector: {
            const Type& component = type.componentType();
            const char* baseName = get_scalar_type_name(fContext, component);
            return String::printf("%s%d", baseName, type.columns());
        }
        case Type::TypeKind::kMatrix: {
            const Type& component = type.componentType();
            const char* baseName = get_scalar_type_name(fContext, component);
            return String::printf("%s%dx%d", baseName, type.columns(), type.rows());
        }
        case Type::TypeKind::kEnum:
            return "Int";

        default:
            SK_ABORT("not yet supported: getTypeName of %s", type.displayName().c_str());
            return type.name();
    }
}

String DSLCPPCodeGenerator::getDSLType(const Type& type) {
    switch (type.typeKind()) {
        case Type::TypeKind::kScalar:
            return String::printf("DSLType(k%s_Type)", get_scalar_type_name(fContext, type));

        case Type::TypeKind::kVector: {
            const Type& component = type.componentType();
            const char* baseName = get_scalar_type_name(fContext, component);
            return String::printf("DSLType(k%s%d_Type)", baseName, type.columns());
        }
        case Type::TypeKind::kMatrix: {
            const Type& component = type.componentType();
            const char* baseName = get_scalar_type_name(fContext, component);
            return String::printf("DSLType(k%s%dx%d_Type)", baseName, type.columns(), type.rows());
        }
        case Type::TypeKind::kEnum:
            return "DSLType(kInt_Type)";

        case Type::TypeKind::kArray: {
            const Type& component = type.componentType();
            SkASSERT(type.columns() != Type::kUnsizedArray);
            return String::printf("Array(%s, %d)", this->getDSLType(component).c_str(),
                                                   type.columns());
        }
        default:
            SK_ABORT("not yet supported: getDSLType of %s", type.displayName().c_str());
            return type.name();
    }
}

String DSLCPPCodeGenerator::getDSLModifiers(const Modifiers& modifiers) {
    String text;

    // Uniform variables can have `in uniform` flags in FP file; that's not how they are
    // represented in DSL, however. Transform `in uniform` modifiers to just `uniform`.
    if (modifiers.fFlags & Modifiers::kUniform_Flag) {
        text += "kUniform_Modifier | ";
    } else if (modifiers.fFlags & Modifiers::kIn_Flag) {
        text += "kIn_Modifier | ";
    }
    if (modifiers.fFlags & Modifiers::kConst_Flag) {
        text += "kConst_Modifier | ";
    }
    if (modifiers.fFlags & Modifiers::kOut_Flag) {
        text += "kOut_Modifier | ";
    }
    if (modifiers.fFlags & Modifiers::kFlat_Flag) {
        text += "kFlat_Modifier | ";
    }
    if (modifiers.fFlags & Modifiers::kNoPerspective_Flag) {
        text += "kNoPerspective_Modifier | ";
    }

    if (text.empty()) {
        return "kNo_Modifier";
    }

    // Eliminate trailing ` | `.
    text.pop_back();
    text.pop_back();
    text.pop_back();
    return text;
}

void DSLCPPCodeGenerator::writeStatement(const Statement& s) {
    switch (s.kind()) {
        case Statement::Kind::kBlock:
            this->writeBlock(s.as<Block>());
            break;
        case Statement::Kind::kExpression:
            this->writeExpression(*s.as<ExpressionStatement>().expression(), Precedence::kTopLevel);
            break;
        case Statement::Kind::kReturn:
            this->writeReturnStatement(s.as<ReturnStatement>());
            break;
        case Statement::Kind::kVarDeclaration:
            this->writeVarDeclaration(s.as<VarDeclaration>(), false);
            break;
        case Statement::Kind::kIf:
            this->writeIfStatement(s.as<IfStatement>());
            break;
        case Statement::Kind::kFor:
            this->writeForStatement(s.as<ForStatement>());
            break;
        case Statement::Kind::kDo:
            this->writeDoStatement(s.as<DoStatement>());
            break;
        case Statement::Kind::kSwitch:
            this->writeSwitchStatement(s.as<SwitchStatement>());
            break;
        case Statement::Kind::kBreak:
            this->write("Break()");
            break;
        case Statement::Kind::kContinue:
            this->write("Continue()");
            break;
        case Statement::Kind::kDiscard:
            this->write("Discard()");
            break;
        case Statement::Kind::kInlineMarker:
        case Statement::Kind::kNop:
            this->write("Statement()");
            break;
        default:
            SkDEBUGFAILF("unsupported statement: %s", s.description().c_str());
            break;
    }
}

void DSLCPPCodeGenerator::writeFloatLiteral(const FloatLiteral& f) {
    this->write(to_string(f.value()));
    this->write("f");
}

void DSLCPPCodeGenerator::writeSetting(const Setting& s) {
    this->writef("sk_Caps.%s()", s.name().c_str());
}

bool DSLCPPCodeGenerator::writeSection(const char* name, const char* prefix) {
    const Section* s = fSectionAndParameterHelper.getSection(name);
    if (s) {
        this->writef("%s%s", prefix, s->text().c_str());
        return true;
    }
    return false;
}

void DSLCPPCodeGenerator::writeProgramElement(const ProgramElement& p) {
    switch (p.kind()) {
        case ProgramElement::Kind::kSection:
            return;
        case ProgramElement::Kind::kGlobalVar: {
            const GlobalVarDeclaration& decl = p.as<GlobalVarDeclaration>();
            const Variable& var = decl.declaration()->as<VarDeclaration>().var();
            if (var.modifiers().fFlags & (Modifiers::kIn_Flag | Modifiers::kUniform_Flag) ||
                -1 != var.modifiers().fLayout.fBuiltin) {
                return;
            }
            break;
        }
        case ProgramElement::Kind::kFunctionPrototype: {
            // Function prototypes are handled at the C++ level (in writeEmitCode).
            // We don't want prototypes to be emitted inside the FP's main() function.
            return;
        }
        default:
            break;
    }
    INHERITED::writeProgramElement(p);
}

void DSLCPPCodeGenerator::addUniform(const Variable& var) {
    if (!needs_uniform_var(var)) {
        return;
    }
    if (var.modifiers().fLayout.fWhen.fLength) {
        this->writef("        if (%s) {\n    ", String(var.modifiers().fLayout.fWhen).c_str());
    }
    this->writeVar(var);

    this->writef("%.*sVar = VarUniformHandle(%s);\n",
                 (int)var.name().size(), var.name().data(), this->getVariableCppName(var));

    if (var.modifiers().fLayout.fWhen.fLength) {
        this->write("        }\n");
    }
}

void DSLCPPCodeGenerator::writeInputVars() {
}

void DSLCPPCodeGenerator::writePrivateVars() {
    for (const ProgramElement* p : fProgram.elements()) {
        if (p->is<GlobalVarDeclaration>()) {
            const GlobalVarDeclaration& global = p->as<GlobalVarDeclaration>();
            const Variable& var = global.declaration()->as<VarDeclaration>().var();
            if (is_private(var)) {
                if (var.type().isFragmentProcessor()) {
                    fErrors.error(global.fOffset,
                                  "fragmentProcessor variables must be declared 'in'");
                    return;
                }
                this->writef("%s %s = %s;\n",
                             HCodeGenerator::FieldType(fContext, var.type(),
                                                       var.modifiers().fLayout).c_str(),
                             this->getVariableCppName(var),
                             default_value(var).c_str());
            } else if (var.modifiers().fLayout.fFlags & Layout::kTracked_Flag) {
                // An auto-tracked uniform in variable, so add a field to hold onto the prior
                // state. Note that tracked variables must be uniform in's and that is validated
                // before writePrivateVars() is called.
                const UniformCTypeMapper* mapper = UniformCTypeMapper::Get(fContext, var);
                SkASSERT(mapper);

                String name = HCodeGenerator::FieldName(String(var.name()).c_str());
                // The member statement is different if the mapper reports a default value
                if (mapper->defaultValue().size() > 0) {
                    this->writef("%s %sPrev = %s;\n",
                                    Layout::CTypeToStr(mapper->ctype()), name.c_str(),
                                    mapper->defaultValue().c_str());
                } else {
                    this->writef("%s %sPrev;\n",
                                    Layout::CTypeToStr(mapper->ctype()), name.c_str());
                }
            }
        }
    }
}

void DSLCPPCodeGenerator::writePrivateVarValues() {
    for (const ProgramElement* p : fProgram.elements()) {
        if (p->is<GlobalVarDeclaration>()) {
            const GlobalVarDeclaration& global = p->as<GlobalVarDeclaration>();
            const VarDeclaration& decl = global.declaration()->as<VarDeclaration>();
            if (is_private(decl.var()) && decl.value()) {
                this->writef("%s = ", String(decl.var().name()).c_str());
                fCPPMode = true;
                this->writeExpression(*decl.value(), Precedence::kAssignment);
                fCPPMode = false;
                this->write(";\n");
            }
        }
    }
}

static bool is_accessible(const Variable& var) {
    const Type& type = var.type();
    return !type.isFragmentProcessor() &&
           Type::TypeKind::kOther != type.typeKind();
}

bool DSLCPPCodeGenerator::writeEmitCode(std::vector<const Variable*>& uniforms) {
    this->writef("    void emitCode(EmitArgs& args) override {\n"
                 "        const %s& _outer = args.fFp.cast<%s>();\n"
                 "        (void) _outer;\n"
                 "\n"
                 "        using namespace SkSL::dsl;\n"
                 "        StartFragmentProcessor(this, &args);\n",
                 fFullName.c_str(), fFullName.c_str());
    for (const ProgramElement* p : fProgram.elements()) {
        if (p->is<GlobalVarDeclaration>()) {
            const GlobalVarDeclaration& global = p->as<GlobalVarDeclaration>();
            const VarDeclaration& decl = global.declaration()->as<VarDeclaration>();
            const Variable& var = decl.var();
            if (var.modifiers().fFlags & Modifiers::kUniform_Flag) {
                continue;
            }
            if (SectionAndParameterHelper::IsParameter(var) && is_accessible(var)) {
                // `formatRuntimeValue` generates a C++ format string (which we don't need, since
                // we're not formatting anything at runtime) and a vector of the arguments within
                // the variable (which we do need, to fill in the Var's initial value).
                std::vector<String> argumentList;
                (void) this->formatRuntimeValue(var.type(), var.modifiers().fLayout,
                                                "_outer." + var.name(), &argumentList);

                const char* varCppName = this->getVariableCppName(var);
                this->writef("Var %s(kConst_Modifier, %s, \"%s\", %s(",
                             varCppName, this->getDSLType(var.type()).c_str(),
                             varCppName, this->getTypeName(var.type()).c_str());
                const char* separator = "";
                for (const String& arg : argumentList) {
                    this->write(separator);
                    this->write(arg);
                    separator = ", ";
                }
                this->writef("));\n"
                             "Declare(%s);\n", varCppName);
            }
        }
    }

    this->writePrivateVarValues();
    for (const Variable* u : uniforms) {
        this->addUniform(*u);
    }
    this->writeSection(kEmitCodeSection);

    // Generate mangled names and argument lists for helper functions.
    std::unordered_set<const FunctionDeclaration*> definedHelpers;
    for (const ProgramElement* p : fProgram.elements()) {
        if (p->is<FunctionDefinition>()) {
            const FunctionDeclaration* decl = &p->as<FunctionDefinition>().declaration();
            definedHelpers.insert(decl);
            this->prepareHelperFunction(*decl);
        }
    }

    // Emit prototypes for defined helper functions that originally had prototypes in the FP file.
    // (If a function was prototyped but never defined, we skip it, since it wasn't prepared above.)
    for (const ProgramElement* p : fProgram.elements()) {
        if (p->is<FunctionPrototype>()) {
            const FunctionDeclaration* decl = &p->as<FunctionPrototype>().declaration();
            if (definedHelpers.find(decl) != definedHelpers.end()) {
                this->prototypeHelperFunction(*decl);
            }
        }
    }

    bool result = INHERITED::generateCode();

    this->write("        EndFragmentProcessor();\n"
                "    }\n");
    return result;
}

void DSLCPPCodeGenerator::writeSetData(std::vector<const Variable*>& uniforms) {
    const char* fullName = fFullName.c_str();
    const Section* section = fSectionAndParameterHelper.getSection(kSetDataSection);
    const char* pdman = section ? section->argument().c_str() : "pdman";
    this->writef("    void onSetData(const GrGLSLProgramDataManager& %s, "
                                    "const GrFragmentProcessor& _proc) override {\n",
                 pdman);
    bool wroteProcessor = false;
    for (const Variable* u : uniforms) {
        if (is_uniform_in(*u)) {
            if (!wroteProcessor) {
                this->writef("        const %s& _outer = _proc.cast<%s>();\n", fullName, fullName);
                wroteProcessor = true;
                this->writef("        {\n");
            }

            const UniformCTypeMapper* mapper = UniformCTypeMapper::Get(fContext, *u);
            SkASSERT(mapper);

            String nameString(u->name());
            const char* name = nameString.c_str();

            // Switches for setData behavior in the generated code
            bool conditionalUniform = u->modifiers().fLayout.fWhen != "";
            bool isTracked = u->modifiers().fLayout.fFlags & Layout::kTracked_Flag;
            bool needsValueDeclaration = isTracked || !mapper->canInlineUniformValue();

            String uniformName = HCodeGenerator::FieldName(name) + "Var";

            String indent = "        "; // 8 by default, 12 when nested for conditional uniforms
            if (conditionalUniform) {
                // Add a pre-check to make sure the uniform was emitted
                // before trying to send any data to the GPU
                this->writef("        if (%s.isValid()) {\n", uniformName.c_str());
                indent += "    ";
            }

            String valueVar = "";
            if (needsValueDeclaration) {
                valueVar.appendf("%sValue", name);
                // Use AccessType since that will match the return type of _outer's public API.
                String valueType = HCodeGenerator::AccessType(fContext, u->type(),
                                                              u->modifiers().fLayout);
                this->writef("%s%s %s = _outer.%s;\n",
                             indent.c_str(), valueType.c_str(), valueVar.c_str(), name);
            } else {
                // Not tracked and the mapper only needs to use the value once
                // so send it a safe expression instead of the variable name
                valueVar.appendf("(_outer.%s)", name);
            }

            if (isTracked) {
                String prevVar = HCodeGenerator::FieldName(name) + "Prev";
                this->writef("%sif (%s) {\n"
                             "%s    %s;\n"
                             "%s    %s;\n"
                             "%s}\n", indent.c_str(),
                        mapper->dirtyExpression(valueVar, prevVar).c_str(), indent.c_str(),
                        mapper->saveState(valueVar, prevVar).c_str(), indent.c_str(),
                        mapper->setUniform(pdman, uniformName, valueVar).c_str(), indent.c_str());
            } else {
                this->writef("%s%s;\n", indent.c_str(),
                        mapper->setUniform(pdman, uniformName, valueVar).c_str());
            }

            if (conditionalUniform) {
                // Close the earlier precheck block
                this->writef("        }\n");
            }
        }
    }
    if (wroteProcessor) {
        this->writef("        }\n");
    }
    if (section) {
        for (const ProgramElement* p : fProgram.elements()) {
            if (p->is<GlobalVarDeclaration>()) {
                const GlobalVarDeclaration& global = p->as<GlobalVarDeclaration>();
                const VarDeclaration& decl = global.declaration()->as<VarDeclaration>();
                const Variable& variable = decl.var();

                if (needs_uniform_var(variable)) {
                    const char* varCppName = this->getVariableCppName(variable);

                    this->writef("        UniformHandle& %s = %.*sVar;\n"
                                 "        (void) %s;\n",
                                 varCppName, (int)variable.name().size(),
                                 variable.name().data(), varCppName);
                } else if (SectionAndParameterHelper::IsParameter(variable) &&
                           !variable.type().isFragmentProcessor()) {
                    if (!wroteProcessor) {
                        this->writef("        const %s& _outer = _proc.cast<%s>();\n",
                                     fullName, fullName);
                        wroteProcessor = true;
                    }

                    if (!variable.type().isFragmentProcessor()) {
                        const char* varCppName = this->getVariableCppName(variable);

                        this->writef("        auto %s = _outer.%.*s;\n"
                                     "        (void) %s;\n",
                                     varCppName, (int)variable.name().size(),
                                     variable.name().data(), varCppName);
                    }
                }
            }
        }
        this->writeSection(kSetDataSection);
    }
    this->write("    }\n");
}

void DSLCPPCodeGenerator::writeClone() {
    if (!this->writeSection(kCloneSection)) {
        if (fSectionAndParameterHelper.getSection(kFieldsSection)) {
            fErrors.error(/*offset=*/0, "fragment processors with custom @fields must also have a "
                                        "custom @clone");
        }
        this->writef("%s::%s(const %s& src)\n"
                     ": INHERITED(k%s_ClassID, src.optimizationFlags())", fFullName.c_str(),
                     fFullName.c_str(), fFullName.c_str(), fFullName.c_str());
        for (const Variable* param : fSectionAndParameterHelper.getParameters()) {
            String fieldName = HCodeGenerator::FieldName(String(param->name()).c_str());
            if (!param->type().isFragmentProcessor()) {
                this->writef("\n, %s(src.%s)",
                             fieldName.c_str(),
                             fieldName.c_str());
            }
        }
        this->writef(" {\n");
        this->writef("        this->cloneAndRegisterAllChildProcessors(src);\n");
        if (fAccessSampleCoordsDirectly) {
            this->writef("    this->setUsesSampleCoordsDirectly();\n");
        }
        this->write("}\n");
        this->writef("std::unique_ptr<GrFragmentProcessor> %s::clone() const {\n",
                     fFullName.c_str());
        this->writef("    return std::make_unique<%s>(*this);\n",
                     fFullName.c_str());
        this->write("}\n");
    }
}

void DSLCPPCodeGenerator::writeDumpInfo() {
    this->writef("#if GR_TEST_UTILS\n"
                 "SkString %s::onDumpInfo() const {\n", fFullName.c_str());

    if (!this->writeSection(kDumpInfoSection)) {
        if (fSectionAndParameterHelper.getSection(kFieldsSection)) {
            fErrors.error(/*offset=*/0, "fragment processors with custom @fields must also have a "
                                        "custom @dumpInfo");
        }

        String formatString;
        std::vector<String> argumentList;

        for (const Variable* param : fSectionAndParameterHelper.getParameters()) {
            // dumpInfo() doesn't need to log child FPs.
            if (param->type().isFragmentProcessor()) {
                continue;
            }

            // Add this field onto the format string and argument list.
            String fieldName = HCodeGenerator::FieldName(String(param->name()).c_str());
            String runtimeValue = this->formatRuntimeValue(param->type(),
                                                           param->modifiers().fLayout,
                                                           param->name(),
                                                           &argumentList);
            formatString.appendf("%s%s=%s",
                                 formatString.empty() ? "" : ", ",
                                 fieldName.c_str(),
                                 runtimeValue.c_str());
        }

        if (!formatString.empty()) {
            // Emit the finished format string and associated arguments.
            this->writef("    return SkStringPrintf(\"(%s)\"", formatString.c_str());

            for (const String& argument : argumentList) {
                this->writef(", %s", argument.c_str());
            }

            this->write(");");
        } else {
            // No fields to dump at all; just return an empty string.
            this->write("    return SkString();");
        }
    }

    this->write("\n"
                "}\n"
                "#endif\n");
}

void DSLCPPCodeGenerator::writeTest() {
    const Section* test = fSectionAndParameterHelper.getSection(kTestCodeSection);
    if (test) {
        this->writef(
                "GR_DEFINE_FRAGMENT_PROCESSOR_TEST(%s);\n"
                "#if GR_TEST_UTILS\n"
                "std::unique_ptr<GrFragmentProcessor> %s::TestCreate(GrProcessorTestData* %s) {\n",
                fFullName.c_str(),
                fFullName.c_str(),
                test->argument().c_str());
        this->writeSection(kTestCodeSection);
        this->write("}\n"
                    "#endif\n");
    }
}

static int bits_needed(uint32_t v) {
    int bits = 1;
    while (v >= (1u << bits)) {
        bits++;
    }
    return bits;
}

void DSLCPPCodeGenerator::writeGetKey() {
    auto bitsForEnum = [&](const Type& type) {
        for (const ProgramElement* e : fProgram.elements()) {
            if (!e->is<Enum>() || type.name() != e->as<Enum>().typeName()) {
                continue;
            }
            SKSL_INT minVal = 0, maxVal = 0;
            auto gatherEnumRange = [&](StringFragment, SKSL_INT value) {
                minVal = std::min(minVal, value);
                maxVal = std::max(maxVal, value);
            };
            e->as<Enum>().foreach(gatherEnumRange);
            if (minVal < 0) {
                // Found a negative value in the enum, just use 32 bits
                return 32;
            }
            SkASSERT(SkTFitsIn<uint32_t>(maxVal));
            return bits_needed(maxVal);
        }
        SK_ABORT("Didn't find declaring element for enum type!");
        return 32;
    };

    this->writef("void %s::onGetGLSLProcessorKey(const GrShaderCaps& caps, "
                                                "GrProcessorKeyBuilder* b) const {\n",
                 fFullName.c_str());
    for (const ProgramElement* p : fProgram.elements()) {
        if (p->is<GlobalVarDeclaration>()) {
            const GlobalVarDeclaration& global = p->as<GlobalVarDeclaration>();
            const VarDeclaration& decl = global.declaration()->as<VarDeclaration>();
            const Variable& var = decl.var();
            const Type& varType = var.type();
            String nameString(var.name());
            const char* name = nameString.c_str();
            if (var.modifiers().fLayout.fFlags & Layout::kKey_Flag) {
                if (var.modifiers().fFlags & Modifiers::kUniform_Flag) {
                    fErrors.error(var.fOffset, "layout(key) may not be specified on uniforms");
                }
                if (is_private(var)) {
                    this->writef("%s %s =",
                                 HCodeGenerator::FieldType(fContext, varType,
                                                           var.modifiers().fLayout).c_str(),
                                 this->getVariableCppName(var));
                    if (decl.value()) {
                        fCPPMode = true;
                        this->writeExpression(*decl.value(), Precedence::kAssignment);
                        fCPPMode = false;
                    } else {
                        this->writef("%s", default_value(var).c_str());
                    }
                    this->write(";\n");
                }
                if (var.modifiers().fLayout.fWhen.fLength) {
                    this->writef("if (%s) {", String(var.modifiers().fLayout.fWhen).c_str());
                }
                if (varType == *fContext.fTypes.fHalf4) {
                    this->writef("    uint16_t red = SkFloatToHalf(%s.fR);\n",
                                 HCodeGenerator::FieldName(name).c_str());
                    this->writef("    uint16_t green = SkFloatToHalf(%s.fG);\n",
                                 HCodeGenerator::FieldName(name).c_str());
                    this->writef("    uint16_t blue = SkFloatToHalf(%s.fB);\n",
                                 HCodeGenerator::FieldName(name).c_str());
                    this->writef("    uint16_t alpha = SkFloatToHalf(%s.fA);\n",
                                 HCodeGenerator::FieldName(name).c_str());
                    this->writef("    b->add32(((uint32_t)red << 16) | green, \"%s.rg\");\n", name);
                    this->writef("    b->add32(((uint32_t)blue << 16) | alpha, \"%s.ba\");\n",
                                 name);
                } else if (varType == *fContext.fTypes.fHalf ||
                           varType == *fContext.fTypes.fFloat) {
                    this->writef("    b->add32(sk_bit_cast<uint32_t>(%s), \"%s\");\n",
                                 HCodeGenerator::FieldName(name).c_str(), name);
                } else if (varType.isBoolean()) {
                    this->writef("    b->addBool(%s, \"%s\");\n",
                                 HCodeGenerator::FieldName(name).c_str(), name);
                } else if (varType.isEnum()) {
                    this->writef("    b->addBits(%d, (uint32_t) %s, \"%s\");\n",
                                 bitsForEnum(varType), HCodeGenerator::FieldName(name).c_str(),
                                 name);
                } else if (varType.isInteger()) {
                    this->writef("    b->add32((uint32_t) %s, \"%s\");\n",
                                 HCodeGenerator::FieldName(name).c_str(), name);
                } else {
                    SK_ABORT("NOT YET IMPLEMENTED: automatic key handling for %s\n",
                             varType.displayName().c_str());
                }
                if (var.modifiers().fLayout.fWhen.fLength) {
                    this->write("}");
                }
            }
        }
    }
    this->write("}\n");
}

bool DSLCPPCodeGenerator::generateCode() {
    std::vector<const Variable*> uniforms;
    for (const ProgramElement* p : fProgram.elements()) {
        if (p->is<GlobalVarDeclaration>()) {
            const GlobalVarDeclaration& global = p->as<GlobalVarDeclaration>();
            const VarDeclaration& decl = global.declaration()->as<VarDeclaration>();
            SkASSERT(decl.var().type().typeKind() != Type::TypeKind::kSampler);

            if (decl.var().modifiers().fFlags & Modifiers::kUniform_Flag) {
                uniforms.push_back(&decl.var());
            }

            if (is_uniform_in(decl.var())) {
                // Validate the "uniform in" declarations to make sure they are fully supported,
                // instead of generating surprising C++
                const UniformCTypeMapper* mapper = UniformCTypeMapper::Get(fContext, decl.var());
                if (mapper == nullptr) {
                    fErrors.error(decl.fOffset, String(decl.var().name())
                            + "'s type is not supported for use as a 'uniform in'");
                    return false;
                }
            } else {
                // If it's not a uniform_in, it's an error to be tracked
                if (decl.var().modifiers().fLayout.fFlags & Layout::kTracked_Flag) {
                    fErrors.error(decl.fOffset, "Non-'in uniforms' cannot be tracked");
                    return false;
                }
            }
        }
    }
    const char* baseName = fName.c_str();
    const char* fullName = fFullName.c_str();
    this->writef("%s\n", HCodeGenerator::GetHeader(fProgram, fErrors).c_str());
    this->writef(kFragmentProcessorHeader, fullName);
    this->write("/* TODO(skia:11854): DSLCPPCodeGenerator is currently a work in progress. */\n");
    this->writef("#include \"%s.h\"\n\n", fullName);
    this->writeSection(kCppSection);
    this->writef("#include \"src/core/SkUtils.h\"\n"
                 "#include \"src/gpu/GrTexture.h\"\n"
                 "#include \"src/gpu/glsl/GrGLSLFragmentProcessor.h\"\n"
                 "#include \"src/gpu/glsl/GrGLSLFragmentShaderBuilder.h\"\n"
                 "#include \"src/gpu/glsl/GrGLSLProgramBuilder.h\"\n"
                 "#include \"src/sksl/SkSLCPP.h\"\n"
                 "#include \"src/sksl/SkSLUtil.h\"\n"
                 "#include \"src/sksl/dsl/priv/DSLFPs.h\"\n"
                 "#include \"src/sksl/dsl/priv/DSLWriter.h\"\n"
                 "\n"
                 "#if defined(__clang__)\n"
                 "#pragma clang diagnostic ignored \"-Wcomma\"\n"
                 "#endif\n"
                 "\n"
                 "class GrGLSL%s : public GrGLSLFragmentProcessor {\n"
                 "public:\n"
                 "    GrGLSL%s() {}\n",
                 baseName, baseName);
    bool result = this->writeEmitCode(uniforms);
    this->write("private:\n");
    this->writeSetData(uniforms);
    this->writePrivateVars();
    for (const Variable* u : uniforms) {
        if (needs_uniform_var(*u) && !(u->modifiers().fFlags & Modifiers::kIn_Flag)) {
            this->writef("    UniformHandle %.*sVar;\n", (int)u->name().size(), u->name().data());
        }
    }
    for (const Variable* param : fSectionAndParameterHelper.getParameters()) {
        if (needs_uniform_var(*param)) {
            this->writef("    UniformHandle %.*sVar;\n",
                         (int)param->name().size(), param->name().data());
        }
    }
    this->writef("};\n"
                 "std::unique_ptr<GrGLSLFragmentProcessor> %s::onMakeProgramImpl() const {\n"
                 "    return std::make_unique<GrGLSL%s>();\n"
                 "}\n",
                 fullName, baseName);
    this->writeGetKey();
    this->writef("bool %s::onIsEqual(const GrFragmentProcessor& other) const {\n"
                 "    const %s& that = other.cast<%s>();\n"
                 "    (void) that;\n",
                 fullName, fullName, fullName);
    for (const auto& param : fSectionAndParameterHelper.getParameters()) {
        if (param->type().isFragmentProcessor()) {
            continue;
        }
        String nameString(param->name());
        const char* name = nameString.c_str();
        this->writef("    if (%s != that.%s) return false;\n",
                     HCodeGenerator::FieldName(name).c_str(),
                     HCodeGenerator::FieldName(name).c_str());
    }
    this->write("    return true;\n"
                "}\n");
    this->writeClone();
    this->writeDumpInfo();
    this->writeTest();
    this->writeSection(kCppEndSection);

    result &= 0 == fErrors.errorCount();
    return result;
}

}  // namespace SkSL

#endif // defined(SKSL_STANDALONE) || GR_TEST_UTILS
