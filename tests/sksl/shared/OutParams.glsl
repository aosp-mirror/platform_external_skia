
out vec4 sk_FragColor;
uniform vec4 colorGreen;
uniform vec4 colorRed;
uniform vec4 colorWhite;
vec4 main() {
    vec4 result;
    float h;
    h = colorWhite.x;
    false;

    vec2 h2;
    h2 = vec2(colorWhite.y);
    false;

    vec3 h3;
    h3 = vec3(colorWhite.z);
    false;

    vec4 h4;
    h4 = vec4(colorWhite.w);
    false;

    h3.y = colorWhite.x;
    false;

    h3.xz = vec2(colorWhite.y);
    false;

    h4.zwxy = vec4(colorWhite.w);
    false;

    mat2 h2x2;
    h2x2 = mat2(colorWhite.x);
    false;

    mat3 h3x3;
    h3x3 = mat3(colorWhite.y);
    false;

    mat4 h4x4;
    h4x4 = mat4(colorWhite.z);
    false;

    h3x3[1] = vec3(colorWhite.z);
    false;

    h4x4[3].w = colorWhite.x;
    false;

    h2x2[0].x = colorWhite.x;
    false;

    int i;
    i = int(colorWhite.x);
    false;

    ivec2 i2;
    i2 = ivec2(int(colorWhite.y));
    false;

    ivec3 i3;
    i3 = ivec3(int(colorWhite.z));
    false;

    ivec4 i4;
    i4 = ivec4(int(colorWhite.w));
    false;

    i4.xyz = ivec3(int(colorWhite.z));
    false;

    i2.y = int(colorWhite.x);
    false;

    float f;
    f = colorWhite.x;
    false;

    vec2 f2;
    f2 = vec2(colorWhite.y);
    false;

    vec3 f3;
    f3 = vec3(colorWhite.z);
    false;

    vec4 f4;
    f4 = vec4(colorWhite.w);
    false;

    f3.xy = vec2(colorWhite.y);
    false;

    f2.x = colorWhite.x;
    false;

    mat2 f2x2;
    f2x2 = mat2(colorWhite.x);
    false;

    mat3 f3x3;
    f3x3 = mat3(colorWhite.y);
    false;

    mat4 f4x4;
    f4x4 = mat4(colorWhite.z);
    false;

    f2x2[0].x = colorWhite.x;
    false;

    bool b;
    b = bool(colorWhite.x);
    false;

    bvec2 b2;
    b2 = bvec2(bool(colorWhite.y));
    false;

    bvec3 b3;
    b3 = bvec3(bool(colorWhite.z));
    false;

    bvec4 b4;
    b4 = bvec4(bool(colorWhite.w));
    false;

    b4.xw = bvec2(bool(colorWhite.y));
    false;

    b3.z = bool(colorWhite.x);
    false;

    bool ok = true;
    ok = ok && 1.0 == (((((h * h2.x) * h3.x) * h4.x) * h2x2[0].x) * h3x3[0].x) * h4x4[0].x;
    ok = ok && 1.0 == (((((f * f2.x) * f3.x) * f4.x) * f2x2[0].x) * f3x3[0].x) * f4x4[0].x;
    ok = ok && 1 == ((i * i2.x) * i3.x) * i4.x;
    ok = ok && (((b && b2.x) && b3.x) && b4.x);
    return ok ? colorGreen : colorRed;
}
