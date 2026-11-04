// @name Pixel Arcade
// @author Shady tests
// @description Moving pixel-art fixture for the CRT recording (not shipped)
// @license MIT
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    float px = iResolution.y / 48.0;                      // 48 chunky rows
    vec2 g = floor(fragCoord / px);
    float cols = floor(iResolution.x / px);
    vec3 sky = mix(vec3(0.05, 0.04, 0.20), vec3(0.85, 0.30, 0.45), g.y / 48.0);
    vec3 c = floor(sky * 5.0) / 5.0;
    // scrolling stars
    vec2 st = vec2(mod(g.x + floor(iTime * 6.0), cols), g.y);
    if (g.y > 20.0 && fract(sin(dot(st, vec2(12.9898, 78.233))) * 43758.5453) > 0.985) c = vec3(1.0);
    // a big bright sun with a pulsing ring
    vec2 sun = vec2(cols * 0.72, 32.0);
    float d = length(g - sun);
    if (d < 7.0) c = vec3(1.0, 0.92, 0.45);
    else if (abs(d - 9.0 - sin(iTime * 3.0)) < 0.6) c = vec3(1.0, 0.6, 0.2);
    // scrolling checkered ground
    if (g.y < 10.0) c = mod(floor((g.x + floor(iTime * 12.0)) / 2.0) + g.y, 2.0) < 1.0 ? vec3(0.20, 0.75, 0.30) : vec3(0.10, 0.45, 0.18);
    // a bouncing 8x8 invader sprite at 2x
    int rows[8] = int[8](0x18, 0x3C, 0x7E, 0xDB, 0xFF, 0x24, 0x5A, 0xA5);
    vec2 pos = vec2(floor(mod(iTime * 10.0, cols + 16.0)) - 16.0, 11.0 + floor(abs(sin(iTime * 2.5)) * 14.0));
    vec2 s = floor((g - pos) / 2.0);
    if (s.x >= 0.0 && s.x < 8.0 && s.y >= 0.0 && s.y < 8.0) {
        int bits = rows[7 - int(s.y)];
        if (((bits >> (7 - int(s.x))) & 1) == 1) c = vec3(0.35, 1.0, 0.95);
    }
    fragColor = vec4(c, 1.0);
}
