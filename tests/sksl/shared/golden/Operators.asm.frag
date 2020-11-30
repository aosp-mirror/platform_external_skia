### Compilation failed:

error: 1: SPIR-V validation error: Expected Constituents to be scalars or vectors of the same type as Result Type components
  %84 = OpCompositeConstruct %v2float %int_6 %int_6

OpCapability Shader
%1 = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %sk_Clockwise
OpExecutionMode %main OriginUpperLeft
OpName %sk_Clockwise "sk_Clockwise"
OpName %main "main"
OpName %x "x"
OpName %y "y"
OpName %z "z"
OpName %b "b"
OpName %c "c"
OpName %d "d"
OpName %e "e"
OpName %f "f"
OpDecorate %sk_Clockwise RelaxedPrecision
OpDecorate %sk_Clockwise BuiltIn FrontFacing
OpDecorate %36 RelaxedPrecision
OpDecorate %37 RelaxedPrecision
OpDecorate %40 RelaxedPrecision
OpDecorate %43 RelaxedPrecision
OpDecorate %46 RelaxedPrecision
OpDecorate %49 RelaxedPrecision
OpDecorate %85 RelaxedPrecision
OpDecorate %88 RelaxedPrecision
OpDecorate %91 RelaxedPrecision
OpDecorate %94 RelaxedPrecision
OpDecorate %97 RelaxedPrecision
%bool = OpTypeBool
%_ptr_Input_bool = OpTypePointer Input %bool
%sk_Clockwise = OpVariable %_ptr_Input_bool Input
%void = OpTypeVoid
%7 = OpTypeFunction %void
%float = OpTypeFloat 32
%_ptr_Function_float = OpTypePointer Function %float
%float_1 = OpConstant %float 1
%float_2 = OpConstant %float 2
%int = OpTypeInt 32 1
%_ptr_Function_int = OpTypePointer Function %int
%int_3 = OpConstant %int 3
%float_n6 = OpConstant %float -6
%float_n1 = OpConstant %float -1
%int_8 = OpConstant %int 8
%_ptr_Function_bool = OpTypePointer Function %bool
%true = OpConstantTrue %bool
%false = OpConstantFalse %bool
%float_12 = OpConstant %float 12
%int_10 = OpConstant %int 10
%int_0 = OpConstant %int 0
%int_n1 = OpConstant %int -1
%int_2 = OpConstant %int 2
%int_4 = OpConstant %int 4
%int_5 = OpConstant %int 5
%v2float = OpTypeVector %float 2
%int_6 = OpConstant %int 6
%float_0 = OpConstant %float 0
%float_6 = OpConstant %float 6
%main = OpFunction %void None %7
%8 = OpLabel
%x = OpVariable %_ptr_Function_float Function
%y = OpVariable %_ptr_Function_float Function
%z = OpVariable %_ptr_Function_int Function
%b = OpVariable %_ptr_Function_bool Function
%c = OpVariable %_ptr_Function_bool Function
%d = OpVariable %_ptr_Function_bool Function
%e = OpVariable %_ptr_Function_bool Function
%f = OpVariable %_ptr_Function_bool Function
OpStore %x %float_1
OpStore %y %float_2
OpStore %z %int_3
OpStore %x %float_n6
OpStore %y %float_n1
OpStore %z %int_8
%26 = OpLogicalEqual %bool %false %true
OpSelectionMerge %28 None
OpBranchConditional %26 %28 %27
%27 = OpLabel
%29 = OpExtInst %float %1 Sqrt %float_2
%30 = OpFOrdGreaterThanEqual %bool %float_2 %29
OpBranch %28
%28 = OpLabel
%31 = OpPhi %bool %true %8 %30 %27
OpStore %b %31
%33 = OpExtInst %float %1 Sqrt %float_2
%34 = OpFOrdGreaterThan %bool %33 %float_2
OpStore %c %34
%36 = OpLoad %bool %b
%37 = OpLoad %bool %c
%38 = OpLogicalNotEqual %bool %36 %37
OpStore %d %38
%40 = OpLoad %bool %b
OpSelectionMerge %42 None
OpBranchConditional %40 %41 %42
%41 = OpLabel
%43 = OpLoad %bool %c
OpBranch %42
%42 = OpLabel
%44 = OpPhi %bool %false %28 %43 %41
OpStore %e %44
%46 = OpLoad %bool %b
OpSelectionMerge %48 None
OpBranchConditional %46 %48 %47
%47 = OpLabel
%49 = OpLoad %bool %c
OpBranch %48
%48 = OpLabel
%50 = OpPhi %bool %true %42 %49 %47
OpStore %f %50
%51 = OpLoad %float %x
%53 = OpFAdd %float %51 %float_12
OpStore %x %53
%54 = OpLoad %float %x
%55 = OpFSub %float %54 %float_12
OpStore %x %55
%56 = OpLoad %float %x
%57 = OpLoad %float %y
OpStore %z %int_10
%58 = OpConvertSToF %float %int_10
%60 = OpFDiv %float %57 %58
OpStore %y %60
%61 = OpFMul %float %56 %60
OpStore %x %61
%62 = OpLoad %int %z
%64 = OpBitwiseOr %int %62 %int_0
OpStore %z %64
%65 = OpLoad %int %z
%67 = OpBitwiseAnd %int %65 %int_n1
OpStore %z %67
%68 = OpLoad %int %z
%69 = OpBitwiseXor %int %68 %int_0
OpStore %z %69
%70 = OpLoad %int %z
%72 = OpShiftRightArithmetic %int %70 %int_2
OpStore %z %72
%73 = OpLoad %int %z
%75 = OpShiftLeftLogical %int %73 %int_4
OpStore %z %75
%76 = OpLoad %int %z
%78 = OpSMod %int %76 %int_5
OpStore %z %78
%80 = OpExtInst %float %1 Sqrt %float_1
%81 = OpCompositeConstruct %v2float %80 %80
%84 = OpCompositeConstruct %v2float %int_6 %int_6
%79 = OpConvertSToF %float %84
OpStore %x %79
%85 = OpLoad %bool %b
%86 = OpSelect %float %85 %float_1 %float_0
%88 = OpLoad %bool %c
%89 = OpSelect %float %88 %float_1 %float_0
%90 = OpFMul %float %86 %89
%91 = OpLoad %bool %d
%92 = OpSelect %float %91 %float_1 %float_0
%93 = OpFMul %float %90 %92
%94 = OpLoad %bool %e
%95 = OpSelect %float %94 %float_1 %float_0
%96 = OpFMul %float %93 %95
%97 = OpLoad %bool %f
%98 = OpSelect %float %97 %float_1 %float_0
%99 = OpFMul %float %96 %98
OpStore %y %float_6
%101 = OpExtInst %float %1 Sqrt %float_1
%102 = OpCompositeConstruct %v2float %101 %101
%103 = OpCompositeConstruct %v2float %int_6 %int_6
OpStore %z %103
OpReturn
OpFunctionEnd

1 error
