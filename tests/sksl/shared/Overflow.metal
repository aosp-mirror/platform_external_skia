#include <metal_stdlib>
#include <simd/simd.h>
using namespace metal;
struct Inputs {
};
struct Outputs {
    float4 sk_FragColor [[color(0)]];
};
fragment Outputs fragmentMain(Inputs _in [[stage_in]], bool _frontFacing [[front_facing]], float4 _fragCoord [[position]]) {
    Outputs _out;
    (void)_out;
    float huge = (((((((((((((((((((((9.9999996169031625e+35 * 1000000000.0) * 1000000000.0) * 1000000000.0) * 1000000000.0) * 1000000000.0) * 1000000000.0) * 1000000000.0) * 1000000000.0) * 1000000000.0) * 1000000000.0) * 1000000000.0) * 1000000000.0) * 1000000000.0) * 1000000000.0) * 1000000000.0) * 1000000000.0) * 1000000000.0) * 1000000000.0) * 1000000000.0) * 1000000000.0) * 1000000000.0) * 1000000000.0;
    _out.sk_FragColor = saturate(float4(0.0, huge, 0.0, huge));
    return _out;
}
