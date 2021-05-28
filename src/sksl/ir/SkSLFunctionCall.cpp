/*
 * Copyright 2021 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/sksl/SkSLAnalysis.h"
#include "src/sksl/SkSLConstantFolder.h"
#include "src/sksl/SkSLContext.h"
#include "src/sksl/SkSLProgramSettings.h"
#include "src/sksl/ir/SkSLBoolLiteral.h"
#include "src/sksl/ir/SkSLConstructorCompound.h"
#include "src/sksl/ir/SkSLFloatLiteral.h"
#include "src/sksl/ir/SkSLFunctionCall.h"
#include "src/sksl/ir/SkSLIntLiteral.h"

#include "include/sksl/DSLCore.h"

namespace SkSL {

static bool has_compile_time_constant_arguments(const ExpressionArray& arguments) {
    for (const std::unique_ptr<Expression>& arg : arguments) {
        const Expression* expr = ConstantFolder::GetConstantValueForVariable(*arg);
        if (!expr->isCompileTimeConstant()) {
            return false;
        }
    }
    return true;
}

template <typename T>
static std::unique_ptr<Expression> coalesce_n_way_vector(const Expression* arg0,
                                                         const Expression* arg1,
                                                         T startingState,
                                                         const std::function<T(T, T, T)>& coalesce,
                                                         const std::function<T(T)>& finalize) {
    // Takes up to two vector or scalar arguments and coalesces them in sequence:
    //     scalar = startingState;
    //     scalar = coalesce(scalar, arg0.x, arg1.x);
    //     scalar = coalesce(scalar, arg0.y, arg1.y);
    //     scalar = coalesce(scalar, arg0.z, arg1.z);
    //     scalar = coalesce(scalar, arg0.w, arg1.w);
    //     scalar = finalize(scalar);
    //
    // If an argument is null, zero is passed to the coalesce function. If the arguments are a mix
    // of scalars and vectors, the scalars is interpreted as a vector containing the same value for
    // every component.

    arg0 = ConstantFolder::GetConstantValueForVariable(*arg0);
    SkASSERT(arg0);

    const Type& vecType =          arg0->type().isVector()  ? arg0->type() :
                          (arg1 && arg1->type().isVector()) ? arg1->type() :
                                                              arg0->type();
    SkASSERT(arg0->type().componentType() == vecType.componentType());

    if (arg1) {
        arg1 = ConstantFolder::GetConstantValueForVariable(*arg1);
        SkASSERT(arg1);
        SkASSERT(arg1->type().componentType() == vecType.componentType());
    }

    T value = startingState;
    int arg0Index = 0;
    int arg1Index = 0;
    for (int index = 0; index < vecType.columns(); ++index) {
        const Expression* arg0Subexpr = arg0->getConstantSubexpression(arg0Index);
        arg0Index += arg0->type().isVector() ? 1 : 0;
        SkASSERT(arg0Subexpr);

        const Expression* arg1Subexpr = nullptr;
        if (arg1) {
            arg1Subexpr = arg1->getConstantSubexpression(arg1Index);
            arg1Index += arg1->type().isVector() ? 1 : 0;
            SkASSERT(arg1Subexpr);
        }

        value = coalesce(value,
                         arg0Subexpr->as<Literal<T>>().value(),
                         arg1Subexpr ? arg1Subexpr->as<Literal<T>>().value() : T{});

        if constexpr (std::is_floating_point<T>::value) {
            // If coalescing the intrinsic yields a non-finite value, do not optimize.
            if (!std::isfinite(value)) {
                return nullptr;
            }
        }
    }

    if (finalize) {
        value = finalize(value);
    }

    return Literal<T>::Make(arg0->fOffset, value, &vecType.componentType());
}

template <typename T>
static std::unique_ptr<Expression> coalesce_vector(const ExpressionArray& arguments,
                                                   T startingState,
                                                   const std::function<T(T, T)>& coalesce,
                                                   const std::function<T(T)>& finalize) {
    SkASSERT(arguments.size() == 1);
    if constexpr (std::is_same<T, bool>::value) {
        SkASSERT(arguments.front()->type().componentType().isBoolean());
    }
    if constexpr (std::is_same<T, float>::value) {
        SkASSERT(arguments.front()->type().componentType().isFloat());
    }

    return coalesce_n_way_vector<T>(arguments.front().get(), /*arg1=*/nullptr, startingState,
                                    [&coalesce](T a, T b, T) { return coalesce(a, b); },
                                    finalize);
}

template <typename T>
static std::unique_ptr<Expression> coalesce_pairwise_vectors(
        const ExpressionArray& arguments,
        T startingState,
        const std::function<T(T, T, T)>& coalesce,
        const std::function<T(T)>& finalize) {
    SkASSERT(arguments.size() == 2);
    const Type& type = arguments.front()->type().componentType();

    if (type.isFloat()) {
        return coalesce_n_way_vector<float>(arguments[0].get(), arguments[1].get(), startingState,
                                            coalesce, finalize);
    }

    SkDEBUGFAILF("unsupported type %s", type.description().c_str());
    return nullptr;
}

template <typename LITERAL, typename FN>
static std::unique_ptr<Expression> optimize_comparison_of_type(const Context& context,
                                                               const Expression& left,
                                                               const Expression& right,
                                                               const FN& compare) {
    const Type& type = left.type();
    SkASSERT(type.isVector());
    SkASSERT(type.componentType().isNumber());
    SkASSERT(type == right.type());

    ExpressionArray array;
    array.reserve_back(type.columns());

    for (int index = 0; index < type.columns(); ++index) {
        const Expression* leftSubexpr = left.getConstantSubexpression(index);
        const Expression* rightSubexpr = right.getConstantSubexpression(index);
        SkASSERT(leftSubexpr);
        SkASSERT(rightSubexpr);
        bool value = compare(leftSubexpr->as<LITERAL>().value(),
                             rightSubexpr->as<LITERAL>().value());
        array.push_back(BoolLiteral::Make(context, leftSubexpr->fOffset, value));
    }

    const Type& bvecType = context.fTypes.fBool->toCompound(context, type.columns(), /*rows=*/1);
    return ConstructorCompound::Make(context, left.fOffset, bvecType, std::move(array));
}

template <typename FN>
static std::unique_ptr<Expression> optimize_comparison(const Context& context,
                                                       const ExpressionArray& arguments,
                                                       const FN& compare) {
    SkASSERT(arguments.size() == 2);
    const Expression* left = ConstantFolder::GetConstantValueForVariable(*arguments[0]);
    const Expression* right = ConstantFolder::GetConstantValueForVariable(*arguments[1]);
    const Type& type = left->type().componentType();

    if (type.isFloat()) {
        return optimize_comparison_of_type<FloatLiteral>(context, *left, *right, compare);
    }
    if (type.isInteger()) {
        return optimize_comparison_of_type<IntLiteral>(context, *left, *right, compare);
    }
    SkDEBUGFAILF("unsupported type %s", type.description().c_str());
    return nullptr;
}

template <typename T>
static std::unique_ptr<Expression> evaluate_n_way_intrinsic_of_type(
                                                            const Context& context,
                                                            const Expression* arg0,
                                                            const Expression* arg1,
                                                            const Expression* arg2,
                                                            const std::function<T(T, T, T)>& eval) {
    // Takes up to three arguments and evaluates them in tandem, equivalent to constructing a new
    // vector containing the results from:
    //     eval(arg0.x, arg1.x, arg2.x),
    //     eval(arg0.y, arg1.y, arg2.y),
    //     eval(arg0.z, arg1.z, arg2.z),
    //     eval(arg0.w, arg1.w, arg2.w)
    //
    // If an argument is null, zero is passed to the evaluation function. If the arguments are a mix
    // of scalars and vectors, scalars are interpreted as a vector containing the same value for
    // every component.
    arg0 = ConstantFolder::GetConstantValueForVariable(*arg0);
    SkASSERT(arg0);

    const Type& vecType =          arg0->type().isVector()  ? arg0->type() :
                          (arg1 && arg1->type().isVector()) ? arg1->type() :
                          (arg2 && arg2->type().isVector()) ? arg2->type() :
                                                              arg0->type();
    const Type& type = vecType.componentType();
    SkASSERT(arg0->type().componentType() == type);

    if (arg1) {
        arg1 = ConstantFolder::GetConstantValueForVariable(*arg1);
        SkASSERT(arg1);
        SkASSERT(arg1->type().componentType() == type);
    }

    if (arg2) {
        arg2 = ConstantFolder::GetConstantValueForVariable(*arg2);
        SkASSERT(arg2);
        SkASSERT(arg2->type().componentType() == type);
    }

    ExpressionArray array;
    array.reserve_back(vecType.columns());

    int arg0Index = 0;
    int arg1Index = 0;
    int arg2Index = 0;
    for (int index = 0; index < vecType.columns(); ++index) {
        const Expression* arg0Subexpr = arg0->getConstantSubexpression(arg0Index);
        arg0Index += arg0->type().isVector() ? 1 : 0;
        SkASSERT(arg0Subexpr);

        const Expression* arg1Subexpr = nullptr;
        if (arg1) {
            arg1Subexpr = arg1->getConstantSubexpression(arg1Index);
            arg1Index += arg1->type().isVector() ? 1 : 0;
            SkASSERT(arg1Subexpr);
        }

        const Expression* arg2Subexpr = nullptr;
        if (arg2) {
            arg2Subexpr = arg2->getConstantSubexpression(arg2Index);
            arg2Index += arg2->type().isVector() ? 1 : 0;
            SkASSERT(arg2Subexpr);
        }

        T value = eval(arg0Subexpr->as<Literal<T>>().value(),
                       arg1Subexpr ? arg1Subexpr->as<Literal<T>>().value() : T{},
                       arg2Subexpr ? arg2Subexpr->as<Literal<T>>().value() : T{});

        if constexpr (std::is_floating_point<T>::value) {
            // If evaluation of the intrinsic yields a non-finite value, do not optimize.
            if (!std::isfinite(value)) {
                return nullptr;
            }
        }

        array.push_back(Literal<T>::Make(arg0Subexpr->fOffset, value, &type));
    }

    return ConstructorCompound::Make(context, arg0->fOffset, vecType, std::move(array));
}

template <typename T>
static std::unique_ptr<Expression> evaluate_intrinsic(const Context& context,
                                                      const ExpressionArray& arguments,
                                                      const std::function<T(T)>& eval) {
    SkASSERT(arguments.size() == 1);

    if constexpr (std::is_same<T, bool>::value) {
        SkASSERT(arguments.front()->type().componentType().isBoolean());
    }
    if constexpr (std::is_same<T, float>::value) {
        SkASSERT(arguments.front()->type().componentType().isFloat());
    }
    if constexpr (std::is_same<T, SKSL_INT>::value) {
        SkASSERT(arguments.front()->type().componentType().isInteger());
    }

    return evaluate_n_way_intrinsic_of_type<T>(
            context, arguments.front().get(), /*arg1=*/nullptr, /*arg2=*/nullptr,
            [&eval](T a, T, T) { return eval(a); });
}

template <typename FN>
static std::unique_ptr<Expression> evaluate_intrinsic_numeric(const Context& context,
                                                              const ExpressionArray& arguments,
                                                              const FN& eval) {
    SkASSERT(arguments.size() == 1);
    const Type& type = arguments.front()->type().componentType();

    if (type.isFloat()) {
        return evaluate_intrinsic<float>(context, arguments, eval);
    }
    if (type.isInteger()) {
        return evaluate_intrinsic<SKSL_INT>(context, arguments, eval);
    }

    SkDEBUGFAILF("unsupported type %s", type.description().c_str());
    return nullptr;
}

template <typename FN>
static std::unique_ptr<Expression> evaluate_pairwise_intrinsic(const Context& context,
                                                               const ExpressionArray& arguments,
                                                               const FN& eval) {
    SkASSERT(arguments.size() == 2);
    const Type& type = arguments.front()->type().componentType();

    if (type.isFloat()) {
        return evaluate_n_way_intrinsic_of_type<float>(
                context, arguments[0].get(), arguments[1].get(), /*arg2=*/nullptr,
                [&eval](float a, float b, float) { return eval(a, b); });
    }
    if (type.isInteger()) {
        return evaluate_n_way_intrinsic_of_type<SKSL_INT>(
                context, arguments[0].get(), arguments[1].get(), /*arg2=*/nullptr,
                [&eval](SKSL_INT a, SKSL_INT b, SKSL_INT) { return eval(a, b); });
    }

    SkDEBUGFAILF("unsupported type %s", type.description().c_str());
    return nullptr;
}

template <typename FN>
static std::unique_ptr<Expression> evaluate_3_way_intrinsic(const Context& context,
                                                            const ExpressionArray& arguments,
                                                            const FN& eval) {
    SkASSERT(arguments.size() == 3);
    const Type& type = arguments.front()->type().componentType();

    if (type.isFloat()) {
        return evaluate_n_way_intrinsic_of_type<float>(
                context, arguments[0].get(), arguments[1].get(), arguments[2].get(), eval);
    }
    if (type.isInteger()) {
        return evaluate_n_way_intrinsic_of_type<SKSL_INT>(
                context, arguments[0].get(), arguments[1].get(), arguments[2].get(), eval);
    }

    SkDEBUGFAILF("unsupported type %s", type.description().c_str());
    return nullptr;
}

static std::unique_ptr<Expression> optimize_intrinsic_call(const Context& context,
                                                           IntrinsicKind intrinsic,
                                                           const ExpressionArray& arguments) {
    using namespace SkSL::dsl;
    switch (intrinsic) {
        case k_all_IntrinsicKind:
            return coalesce_vector<bool>(arguments, /*startingState=*/true,
                                         [](bool a, bool b) { return a && b; },
                                         /*finalize=*/nullptr);
        case k_any_IntrinsicKind:
            return coalesce_vector<bool>(arguments, /*startingState=*/false,
                                         [](bool a, bool b) { return a || b; },
                                         /*finalize=*/nullptr);
        case k_not_IntrinsicKind:
            return evaluate_intrinsic<bool>(context, arguments, [](bool a) { return !a; });

        case k_greaterThan_IntrinsicKind:
            return optimize_comparison(context, arguments, [](auto a, auto b) { return a > b; });

        case k_greaterThanEqual_IntrinsicKind:
            return optimize_comparison(context, arguments, [](auto a, auto b) { return a >= b; });

        case k_lessThan_IntrinsicKind:
            return optimize_comparison(context, arguments, [](auto a, auto b) { return a < b; });

        case k_lessThanEqual_IntrinsicKind:
            return optimize_comparison(context, arguments, [](auto a, auto b) { return a <= b; });

        case k_equal_IntrinsicKind:
            return optimize_comparison(context, arguments, [](auto a, auto b) { return a == b; });

        case k_notEqual_IntrinsicKind:
            return optimize_comparison(context, arguments, [](auto a, auto b) { return a != b; });

        case k_abs_IntrinsicKind:
            return evaluate_intrinsic_numeric(context, arguments,
                                              [](auto a) { return std::abs(a); });
        case k_sign_IntrinsicKind:
            return evaluate_intrinsic_numeric(context, arguments,
                                              [](auto a) { return (a > 0) - (a < 0); });
        case k_sin_IntrinsicKind:
            return evaluate_intrinsic<float>(context, arguments,
                                             [](float a) { return std::sin(a); });
        case k_cos_IntrinsicKind:
            return evaluate_intrinsic<float>(context, arguments,
                                             [](float a) { return std::cos(a); });
        case k_tan_IntrinsicKind:
            return evaluate_intrinsic<float>(context, arguments,
                                             [](float a) { return std::tan(a); });
        case k_asin_IntrinsicKind:
            return evaluate_intrinsic<float>(context, arguments,
                                             [](float a) { return std::asin(a); });
        case k_acos_IntrinsicKind:
            return evaluate_intrinsic<float>(context, arguments,
                                             [](float a) { return std::acos(a); });
        case k_sinh_IntrinsicKind:
            return evaluate_intrinsic<float>(context, arguments,
                                             [](float a) { return std::sinh(a); });
        case k_cosh_IntrinsicKind:
            return evaluate_intrinsic<float>(context, arguments,
                                             [](float a) { return std::cosh(a); });
        case k_tanh_IntrinsicKind:
            return evaluate_intrinsic<float>(context, arguments,
                                             [](float a) { return std::tanh(a); });
        case k_ceil_IntrinsicKind:
            return evaluate_intrinsic<float>(context, arguments,
                                             [](float a) { return std::ceil(a); });
        case k_floor_IntrinsicKind:
            return evaluate_intrinsic<float>(context, arguments,
                                             [](float a) { return std::floor(a); });
        case k_fract_IntrinsicKind:
            return evaluate_intrinsic<float>(context, arguments,
                                             [](float a) { return a - std::floor(a); });
        case k_trunc_IntrinsicKind:
            return evaluate_intrinsic<float>(context, arguments,
                                             [](float a) { return std::trunc(a); });
        case k_mod_IntrinsicKind:
            return evaluate_pairwise_intrinsic(
                    context, arguments, [](auto x, auto y) { return x - y * std::floor(x / y); });
        case k_pow_IntrinsicKind:
            return evaluate_pairwise_intrinsic(context, arguments,
                                               [](auto x, auto y) { return std::pow(x, y); });
        case k_exp_IntrinsicKind:
            return evaluate_intrinsic<float>(context, arguments,
                                             [](float a) { return std::exp(a); });
        case k_log_IntrinsicKind:
            return evaluate_intrinsic<float>(context, arguments,
                                             [](float a) { return std::log(a); });
        case k_exp2_IntrinsicKind:
            return evaluate_intrinsic<float>(context, arguments,
                                             [](float a) { return std::exp2(a); });
        case k_log2_IntrinsicKind:
            return evaluate_intrinsic<float>(context, arguments,
                                             [](float a) { return std::log2(a); });
        case k_sqrt_IntrinsicKind:
            return evaluate_intrinsic<float>(context, arguments,
                                             [](float a) { return std::sqrt(a); });
        case k_saturate_IntrinsicKind:
            return evaluate_intrinsic<float>(context, arguments,
                                             [](float a) { return (a < 0) ? 0 : (a > 1) ? 1 : a; });
        case k_round_IntrinsicKind:      // GLSL `round` documents its rounding mode as unspecified
        case k_roundEven_IntrinsicKind:  // and is allowed to behave identically to `roundEven`.
            return evaluate_intrinsic<float>(context, arguments,
                                             [](float a) { return std::round(a / 2) * 2; });
        case k_inversesqrt_IntrinsicKind:
            return evaluate_intrinsic<float>(context, arguments,
                                             [](float a) { return 1 / std::sqrt(a); });
        case k_radians_IntrinsicKind:
            return evaluate_intrinsic<float>(context, arguments,
                                             [](float a) { return a * 0.0174532925; });
        case k_degrees_IntrinsicKind:
            return evaluate_intrinsic<float>(context, arguments,
                                             [](float a) { return a * 57.2957795; });
        case k_min_IntrinsicKind:
            return evaluate_pairwise_intrinsic(context, arguments,
                                               [](auto a, auto b) { return (a < b) ? a : b; });
        case k_max_IntrinsicKind:
            return evaluate_pairwise_intrinsic(context, arguments,
                                               [](auto a, auto b) { return (a > b) ? a : b; });
        case k_clamp_IntrinsicKind:
            return evaluate_3_way_intrinsic(context, arguments,
                    [](auto x, auto l, auto h) { return (x < l) ? l : (x > h) ? h : x; });
        case k_step_IntrinsicKind:
            return evaluate_pairwise_intrinsic(context, arguments,
                                               [](auto e, auto x) { return (x < e) ? 0 : 1; });
        case k_smoothstep_IntrinsicKind:
            return evaluate_3_way_intrinsic(context, arguments, [](auto edge0, auto edge1, auto x) {
                auto t = (x - edge0) / (edge1 - edge0);
                t = (t < 0) ? 0 : (t > 1) ? 1 : t;
                return t * t * (3.0 - 2.0 * t);
            });
        case k_length_IntrinsicKind:
            return coalesce_vector<float>(arguments, /*startingState=*/0,
                                         [](float a, float b) { return a + (b * b); },
                                         [](float a) { return std::sqrt(a); });
        case k_distance_IntrinsicKind:
            return coalesce_pairwise_vectors<float>(
                    arguments, /*startingState=*/0,
                    [](float a, float b, float c) { b -= c; return a + (b * b); },
                    [](float a) { return std::sqrt(a); });
        case k_dot_IntrinsicKind:
            return coalesce_pairwise_vectors<float>(
                    arguments, /*startingState=*/0,
                    [](float a, float b, float c) { return a + (b * c); },
                    /*finalize=*/nullptr);
        case k_normalize_IntrinsicKind: {
            auto Vec = [&] { return DSLExpression{arguments[0]->clone()}; };
            return (Vec() / Length(Vec())).release();
        }
        case k_faceforward_IntrinsicKind: {
            auto N    = [&] { return DSLExpression{arguments[0]->clone()}; };
            auto I    = [&] { return DSLExpression{arguments[1]->clone()}; };
            auto NRef = [&] { return DSLExpression{arguments[2]->clone()}; };
            return (N() * Select(Dot(NRef(), I()) < 0, 1, -1)).release();
        }
        case k_reflect_IntrinsicKind: {
            auto I    = [&] { return DSLExpression{arguments[0]->clone()}; };
            auto N    = [&] { return DSLExpression{arguments[1]->clone()}; };
            return (I() - 2.0 * Dot(N(), I()) * N()).release();
        }
        case k_refract_IntrinsicKind: {
            auto I    = [&] { return DSLExpression{arguments[0]->clone()}; };
            auto N    = [&] { return DSLExpression{arguments[1]->clone()}; };
            auto Eta  = [&] { return DSLExpression{arguments[2]->clone()}; };

            std::unique_ptr<Expression> k =
                    (1 - Pow(Eta(), 2) * (1 - Pow(Dot(N(), I()), 2))).release();
            if (!k->is<FloatLiteral>()) {
                return nullptr;
            }
            float kValue = k->as<FloatLiteral>().value();
            return ((kValue < 0) ?
                       (0 * I()) :
                       (Eta() * I() - (Eta() * Dot(N(), I()) + std::sqrt(kValue)) * N())).release();
        }
        case k_inverse_IntrinsicKind: {
            auto M = [&](int c, int r) -> float {
                int index = (arguments[0]->type().rows() * c) + r;
                return arguments[0]->getConstantSubexpression(index)->as<FloatLiteral>().value();
            };
            // Our matrix inverse is adapted from the logic in GLSLCodeGenerator::writeInverseHack.
            switch (arguments[0]->type().slotCount()) {
                case 4: {
                    float a00 = M(0, 0), a01 = M(0, 1);
                    float a10 = M(1, 0), a11 = M(1, 1);
                    float ind = 1 / (a00 * a11 - a01 * a10);  // inverse determinant
                    if (!std::isfinite(ind)) {
                        return nullptr;
                    }
                    return DSLType::Construct(&arguments[0]->type(),
                                               a11 * ind, -a01 * ind,
                                              -a10 * ind,  a00 * ind).release();
                }
                case 9: {
                    float a00 = M(0, 0), a01 = M(0, 1), a02 = M(0, 2);
                    float a10 = M(1, 0), a11 = M(1, 1), a12 = M(1, 2);
                    float a20 = M(2, 0), a21 = M(2, 1), a22 = M(2, 2);
                    float b01 =  a22 * a11 - a12 * a21;
                    float b11 = -a22 * a10 + a12 * a20;
                    float b21 =  a21 * a10 - a11 * a20;
                    float ind = 1 / (a00 * b01 + a01 * b11 + a02 * b21);  // inverse determinant
                    if (!std::isfinite(ind)) {
                        return nullptr;
                    }
                    return DSLType::Construct(&arguments[0]->type(),
                        b01 * ind, (-a22 * a01 + a02 * a21) * ind, ( a12 * a01 - a02 * a11) * ind,
                        b11 * ind, ( a22 * a00 - a02 * a20) * ind, (-a12 * a00 + a02 * a10) * ind,
                        b21 * ind, (-a21 * a00 + a01 * a20) * ind, ( a11 * a00 - a01 * a10) * ind)
                    .release();
                }
                case 16: {
                    float a00 = M(0, 0), a01 = M(0, 1), a02 = M(0, 2), a03 = M(0, 3);
                    float a10 = M(1, 0), a11 = M(1, 1), a12 = M(1, 2), a13 = M(1, 3);
                    float a20 = M(2, 0), a21 = M(2, 1), a22 = M(2, 2), a23 = M(2, 3);
                    float a30 = M(3, 0), a31 = M(3, 1), a32 = M(3, 2), a33 = M(3, 3);
                    float b00 = a00 * a11 - a01 * a10;
                    float b01 = a00 * a12 - a02 * a10;
                    float b02 = a00 * a13 - a03 * a10;
                    float b03 = a01 * a12 - a02 * a11;
                    float b04 = a01 * a13 - a03 * a11;
                    float b05 = a02 * a13 - a03 * a12;
                    float b06 = a20 * a31 - a21 * a30;
                    float b07 = a20 * a32 - a22 * a30;
                    float b08 = a20 * a33 - a23 * a30;
                    float b09 = a21 * a32 - a22 * a31;
                    float b10 = a21 * a33 - a23 * a31;
                    float b11 = a22 * a33 - a23 * a32;
                    float ind = 1 / (b00 * b11 - b01 * b10 + b02 * b09 +
                                     b03 * b08 - b04 * b07 + b05 * b06);  // inverse determinant
                    if (!std::isfinite(ind)) {
                        return nullptr;
                    }
                    return DSLType::Construct(&arguments[0]->type(),
                                              (a11 * b11 - a12 * b10 + a13 * b09) * ind,
                                              (a02 * b10 - a01 * b11 - a03 * b09) * ind,
                                              (a31 * b05 - a32 * b04 + a33 * b03) * ind,
                                              (a22 * b04 - a21 * b05 - a23 * b03) * ind,
                                              (a12 * b08 - a10 * b11 - a13 * b07) * ind,
                                              (a00 * b11 - a02 * b08 + a03 * b07) * ind,
                                              (a32 * b02 - a30 * b05 - a33 * b01) * ind,
                                              (a20 * b05 - a22 * b02 + a23 * b01) * ind,
                                              (a10 * b10 - a11 * b08 + a13 * b06) * ind,
                                              (a01 * b08 - a00 * b10 - a03 * b06) * ind,
                                              (a30 * b04 - a31 * b02 + a33 * b00) * ind,
                                              (a21 * b02 - a20 * b04 - a23 * b00) * ind,
                                              (a11 * b07 - a10 * b09 - a12 * b06) * ind,
                                              (a00 * b09 - a01 * b07 + a02 * b06) * ind,
                                              (a31 * b01 - a30 * b03 - a32 * b00) * ind,
                                              (a20 * b03 - a21 * b01 + a22 * b00) * ind).release();
                    break;
                }
            }
            SkDEBUGFAILF("unsupported type %s", arguments[0]->type().description().c_str());
            return nullptr;
            break;
        }
        default:
            return nullptr;
    }
}

bool FunctionCall::hasProperty(Property property) const {
    if (property == Property::kSideEffects &&
        (this->function().modifiers().fFlags & Modifiers::kHasSideEffects_Flag)) {
        return true;
    }
    for (const auto& arg : this->arguments()) {
        if (arg->hasProperty(property)) {
            return true;
        }
    }
    return false;
}

std::unique_ptr<Expression> FunctionCall::clone() const {
    ExpressionArray cloned;
    cloned.reserve_back(this->arguments().size());
    for (const std::unique_ptr<Expression>& arg : this->arguments()) {
        cloned.push_back(arg->clone());
    }
    return std::make_unique<FunctionCall>(
            fOffset, &this->type(), &this->function(), std::move(cloned));
}

String FunctionCall::description() const {
    String result = String(this->function().name()) + "(";
    String separator;
    for (const std::unique_ptr<Expression>& arg : this->arguments()) {
        result += separator;
        result += arg->description();
        separator = ", ";
    }
    result += ")";
    return result;
}

std::unique_ptr<Expression> FunctionCall::Convert(const Context& context,
                                                  int offset,
                                                  const FunctionDeclaration& function,
                                                  ExpressionArray arguments) {
    // Reject function calls with the wrong number of arguments.
    if (function.parameters().size() != arguments.size()) {
        String msg = "call to '" + function.name() + "' expected " +
                     to_string((int)function.parameters().size()) + " argument";
        if (function.parameters().size() != 1) {
            msg += "s";
        }
        msg += ", but found " + to_string(arguments.count());
        context.fErrors.error(offset, msg);
        return nullptr;
    }

    // GLSL ES 1.0 requires static recursion be rejected by the compiler. Also, our CPU back-end
    // cannot handle recursion (and is tied to strictES2Mode front-ends). The safest way to reject
    // all (potentially) recursive code is to disallow calls to functions before they're defined.
    if (context.fConfig->strictES2Mode() && !function.definition() && !function.isBuiltin()) {
        context.fErrors.error(offset, "call to undefined function '" + function.name() + "'");
        return nullptr;
    }

    // Resolve generic types.
    FunctionDeclaration::ParamTypes types;
    const Type* returnType;
    if (!function.determineFinalTypes(arguments, &types, &returnType)) {
        String msg = "no match for " + function.name() + "(";
        String separator;
        for (const std::unique_ptr<Expression>& arg : arguments) {
            msg += separator;
            msg += arg->type().displayName();
            separator = ", ";
        }
        msg += ")";
        context.fErrors.error(offset, msg);
        return nullptr;
    }

    for (size_t i = 0; i < arguments.size(); i++) {
        // Coerce each argument to the proper type.
        arguments[i] = types[i]->coerceExpression(std::move(arguments[i]), context);
        if (!arguments[i]) {
            return nullptr;
        }
        // Update the refKind on out-parameters, and ensure that they are actually assignable.
        const Modifiers& paramModifiers = function.parameters()[i]->modifiers();
        if (paramModifiers.fFlags & Modifiers::kOut_Flag) {
            const VariableRefKind refKind = paramModifiers.fFlags & Modifiers::kIn_Flag
                                                    ? VariableReference::RefKind::kReadWrite
                                                    : VariableReference::RefKind::kPointer;
            if (!Analysis::MakeAssignmentExpr(arguments[i].get(), refKind, &context.fErrors)) {
                return nullptr;
            }
        }
    }

    return Make(context, offset, returnType, function, std::move(arguments));
}

std::unique_ptr<Expression> FunctionCall::Make(const Context& context,
                                               int offset,
                                               const Type* returnType,
                                               const FunctionDeclaration& function,
                                               ExpressionArray arguments) {
    SkASSERT(function.parameters().size() == arguments.size());
    SkASSERT(function.definition() || function.isBuiltin() || !context.fConfig->strictES2Mode());

    if (context.fConfig->fSettings.fOptimize) {
        // We might be able to optimize built-in intrinsics.
        if (function.isIntrinsic() && has_compile_time_constant_arguments(arguments)) {
            // The function is an intrinsic and all inputs are compile-time constants. Optimize it.
            if (std::unique_ptr<Expression> expr =
                        optimize_intrinsic_call(context, function.intrinsicKind(), arguments)) {
                return expr;
            }
        }
    }

    return std::make_unique<FunctionCall>(offset, returnType, &function, std::move(arguments));
}

}  // namespace SkSL
