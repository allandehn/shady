#version 440
// Shady's optional CRT finishing pass (config `crt`, env SHADY_CRT). One cheap
// pass over the shader's own render texture; off by default.
//
// Inspired by Timothy Lottes's "PUBLIC DOMAIN CRT STYLED SCAN-LINE SHADER"
// ("Please take and use, change, or whatever."; see the libretro glsl-shaders
// port crt/shaders/crt-lottes.glsl). Borrowed ideas, rewritten small: the
// exp2 Gaussian weights for the horizontal kernel and the scanline, the
// distance-to-texel-centre trick, and the RGB aperture-grille mask on output
// pixels. Not used: curvature (warp), the multi-tap bloom, the shadow masks.
//
// Strength 0-100 from Settings (0 = no pass at all). Low end is calm; the top
// end is deliberately loud: deep scanline gaps, a bold grille, heavy glow.
// Always free of: curvature, flicker, rolling bars or
// noise, nothing animated, no tint beyond the grille's own (it averages to grey).
// Scanlines sit at a fixed 3 screen px per line (coarser only when the render
// texture itself is), so the look doesn't change with quality.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 srcSize;   // render texture size in pixels
    vec2 outSize;   // output size in screen pixels
    float maskAmount; // 1 = grille at this strength, 0 = off (diagnostics)
    float glow;     // 0 = cheap path (no glow taps), else glow strength
    // Strength-derived knobs, worked out in QML when the slider moves (not per pixel):
    float hardPix;  // horizontal softening (more negative = sharper)
    float scanFloor; // dark gap brightness between scanlines (1 = no lines)
    float scanNorm; // brightness compensation divisor for the scanlines
    float maskDepth; // grille stripe contrast (0 = none)
};
layout(binding = 1) uniform sampler2D src;

const float hardScan = -12.0; // scanline shape (Lottes default -8.0; ours has a floor, so a narrower peak)

float gaus(float x, float s) { return exp2(s * x * x); }
vec3 lin(vec3 c) { return c * c; }   // cheap gamma 2.0 approximation

void main()
{
    vec2 p = qt_TexCoord0 * srcSize;
    vec2 d = fract(p) - 0.5;                  // distance to the texel centre
    // Scanlines are tied to the screen: exactly 3 screen px per line, unless the
    // render texture's rows are coarser (very low quality), then one per row.
    // So auto-adapt changing quality (down to ~33%) doesn't change the look.
    float period = max(3.0, outSize.y / srcSize.y);
    float ly = qt_TexCoord0.y * outSize.y / period;
    float sy = fract(ly) - 0.5;
    float y = (floor(ly) + 0.5) * period / outSize.y; // sample this line's centre
    float x0 = (floor(p.x) + 0.5) / srcSize.x, dx = 1.0 / srcSize.x;

    // 3-tap horizontal Gaussian along the scanline.
    float wa = gaus(d.x + 1.0, hardPix), wb = gaus(d.x, hardPix), wc = gaus(d.x - 1.0, hardPix);
    vec3 c = (lin(texture(src, vec2(x0 - dx, y)).rgb) * wa
            + lin(texture(src, vec2(x0, y)).rgb) * wb
            + lin(texture(src, vec2(x0 + dx, y)).rgb) * wc) / (wa + wb + wc);

    // Soft scanline, brightness-compensated so the picture keeps its level.
    // Divided by the profile's mean over a row (~0.84) so the level stays put.
    float scan = mix(scanFloor, 1.0, gaus(sy, hardScan));
    c *= scan / scanNorm;

    // Slight glow on bright areas: four wide linear taps, skipped on the cheap path.
    if (glow > 0.0) {
        vec2 t = 1.0 / srcSize;
        vec3 g = lin(texture(src, qt_TexCoord0 + vec2(-2.5, -1.0) * t).rgb)
               + lin(texture(src, qt_TexCoord0 + vec2( 2.5, -1.0) * t).rgb)
               + lin(texture(src, qt_TexCoord0 + vec2(-2.5,  1.0) * t).rgb)
               + lin(texture(src, qt_TexCoord0 + vec2( 2.5,  1.0) * t).rgb);
        c += max(g * 0.25 - 0.35, 0.0) * glow;
    }

    // Faint aperture grille on output pixels (vertical RGB stripes).
    // Snapped to whole screen pixels (gl_FragCoord.x is k + 0.5), so the
    // stripe index is exact and never beats against the picture.
    float k = mod(floor(gl_FragCoord.x), 3.0);
    vec3 mask = vec3(1.0 - maskDepth);
    float lit = 1.0 + 2.0 * maskDepth; // keeps the stripe triplet's average at 1
    if (k < 0.5) mask.r = lit; else if (k < 1.5) mask.g = lit; else mask.b = lit;
    c *= mix(vec3(1.0), mask, maskAmount);

    fragColor = vec4(sqrt(max(c, 0.0)), 1.0) * qt_Opacity;
}
