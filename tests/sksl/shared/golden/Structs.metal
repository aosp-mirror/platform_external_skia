#include <metal_stdlib>
#include <simd/simd.h>
using namespace metal;
struct A {
    int x;
    int y;
};
struct B {
    float x;
    float y[2];
    A z;
};
struct Inputs {
};
struct Outputs {
    float4 sk_FragColor [[color(0)]];
};
struct Globals {
    A a1;
    B b1;
};


fragment Outputs fragmentMain(Inputs _in [[stage_in]], bool _frontFacing [[front_facing]], float4 _fragCoord [[position]]) {
    Globals _skGlobals{{}, {}};
    Outputs _outputStruct;
    thread Outputs* _out = &_outputStruct;
    _skGlobals.a1.x = 0;
    _skGlobals.b1.x = 0.0;
    _out->sk_FragColor.x = float(_skGlobals.a1.x) + _skGlobals.b1.x;
    return *_out;
}
