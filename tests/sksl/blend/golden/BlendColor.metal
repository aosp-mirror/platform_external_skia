#include <metal_stdlib>
#include <simd/simd.h>
using namespace metal;
struct Inputs {
    float4 src;
    float4 dst;
};
struct Outputs {
    float4 sk_FragColor [[color(0)]];
};


fragment Outputs fragmentMain(Inputs _in [[stage_in]], bool _frontFacing [[front_facing]], float4 _fragCoord [[position]]) {
    Outputs _outputStruct;
    thread Outputs* _out = &_outputStruct;
    float4 _0_blend_color;
    {
        float _1_alpha = _in.dst.w * _in.src.w;
        float3 _2_sda = _in.src.xyz * _in.dst.w;
        float3 _3_dsa = _in.dst.xyz * _in.src.w;
        float3 _4_blend_set_color_luminance;
        {
            float _5_11_blend_color_luminance;
            {
                _5_11_blend_color_luminance = dot(float3(0.30000001192092896, 0.5899999737739563, 0.10999999940395355), _3_dsa);
            }
            float _6_lum = _5_11_blend_color_luminance;

            float _7_12_blend_color_luminance;
            {
                _7_12_blend_color_luminance = dot(float3(0.30000001192092896, 0.5899999737739563, 0.10999999940395355), _2_sda);
            }
            float3 _8_result = (_6_lum - _7_12_blend_color_luminance) + _2_sda;

            float _9_minComp = min(min(_8_result.x, _8_result.y), _8_result.z);
            float _10_maxComp = max(max(_8_result.x, _8_result.y), _8_result.z);
            if (_9_minComp < 0.0 && _6_lum != _9_minComp) {
                _8_result = _6_lum + ((_8_result - _6_lum) * _6_lum) / (_6_lum - _9_minComp);
            }
            _4_blend_set_color_luminance = _10_maxComp > _1_alpha && _10_maxComp != _6_lum ? _6_lum + ((_8_result - _6_lum) * (_1_alpha - _6_lum)) / (_10_maxComp - _6_lum) : _8_result;
        }
        _0_blend_color = float4((((_4_blend_set_color_luminance + _in.dst.xyz) - _3_dsa) + _in.src.xyz) - _2_sda, (_in.src.w + _in.dst.w) - _1_alpha);

    }
    _out->sk_FragColor = _0_blend_color;

    return *_out;
}
