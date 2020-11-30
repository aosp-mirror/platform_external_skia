
out vec4 sk_FragColor;
in vec4 src;
in vec4 dst;
void main() {
    vec4 _0_blend_darken;
    {
        vec4 _1_1_blend_src_over;
        {
            _1_1_blend_src_over = src + (1.0 - src.w) * dst;
        }
        vec4 _2_result = _1_1_blend_src_over;

        _2_result.xyz = min(_2_result.xyz, (1.0 - dst.w) * src.xyz + dst.xyz);
        _0_blend_darken = _2_result;
    }
    sk_FragColor = _0_blend_darken;

}
