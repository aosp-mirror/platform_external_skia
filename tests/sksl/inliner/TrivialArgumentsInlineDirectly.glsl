
out vec4 sk_FragColor;
in float val;
uniform int ui;
uniform vec4 uh4;
uniform bool b;
struct S {
    vec4 ah4[1];
    float ah[1];
    vec4 h4;
    float h;
};
void main() {
    S s;
    s.ah4[0] = vec4(val);
    s.ah[0] = val;
    s.h4 = vec4(val);
    s.h = val;
    S as[1];
    as[0].ah4[0] = vec4(val);
    sk_FragColor = sk_FragColor.xxxx;
    false;

    sk_FragColor = vec4(s.h);
    false;

    sk_FragColor = b ? sk_FragColor.xxxx : sk_FragColor.yyyy;
    false;

    sk_FragColor = s.ah4[0].yw.xyxy;
    false;

    sk_FragColor = as[0].ah4[0].xy.xyxy;
    false;

    sk_FragColor = s.h4.zzz.xyzx;
    false;

    sk_FragColor = uh4.xyz.xyzx;
    false;

    sk_FragColor = vec3(s.h).xyzx;
    false;

    sk_FragColor = vec4(s.h);
    false;

    sk_FragColor = s.ah4[0].xxxy;
    false;

    sk_FragColor = uh4;
    false;

    sk_FragColor = sk_FragColor.yyyy;
    false;

    float _0_h = -s.h;
    sk_FragColor = vec4(_0_h);
    false;

    bool _1_b = !b;
    sk_FragColor = _1_b ? sk_FragColor.xxxx : sk_FragColor.yyyy;
    false;

    vec2 _2_h2 = s.ah4[ui].yw;
    sk_FragColor = _2_h2.xyxy;
    false;

    vec3 _3_h3 = s.h4.yyy + s.h4.zzz;
    sk_FragColor = _3_h3.xyzx;
    false;

    vec4 _4_h4 = vec3(s.h4.y, 0.0, 1.0).xyyz;
    sk_FragColor = _4_h4;
    false;

}
