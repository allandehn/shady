#version 440
// Hidden Shadertoy wrapper (the app will generate this around user code).
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 qt_FragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float iTime;
    int iFrame;
    vec3 iResolution;
    vec4 iMouse;
};

// ---- user code (Shadertoy shape) ----
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (2.0 * fragCoord - iResolution.xy) / iResolution.y;
    float t = iTime * 0.3;
    vec3 col = vec3(0.0);
    for (int i = 0; i < 3; ++i) {
        uv = abs(uv) / dot(uv, uv) - 0.9 + 0.1 * sin(t + float(i));
        col += 0.5 + 0.5 * cos(t + uv.xyx * 1.5 + vec3(i, i + 2, i + 4));
    }
    fragColor = vec4(col / 3.0, 1.0);
}
// ---- end user code ----

void main()
{
    // Shadertoy's fragCoord starts bottom-left; Qt/Vulkan top-left: flip y.
    vec2 fragCoord = vec2(qt_TexCoord0.x, 1.0 - qt_TexCoord0.y) * iResolution.xy;
    vec4 c;
    mainImage(c, fragCoord);
    qt_FragColor = vec4(c.rgb, 1.0) * qt_Opacity;
}
