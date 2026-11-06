// @name Pixel Sprite
// @author Shady tests
// @description Pixel-art fixture for the CRT pass screenshots (not shipped)
// @license MIT
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 g = floor(fragCoord / (iResolution.y / 24.0));   // 24 chunky rows
    vec3 sky = mix(vec3(0.10, 0.12, 0.30), vec3(0.95, 0.45, 0.25), g.y / 24.0);
    vec3 c = floor(sky * 4.0) / 4.0;                       // 4-level banding
    vec2 s = g - vec2(floor(iResolution.x / (iResolution.y / 24.0)) * 0.5 - 4.0, 8.0);
    // an 8x8 invader-ish sprite
    int rows[8] = int[8](0x18, 0x3C, 0x7E, 0xDB, 0xFF, 0x24, 0x5A, 0xA5);
    if (s.x >= 0.0 && s.x < 8.0 && s.y >= 0.0 && s.y < 8.0) {
        int bits = rows[7 - int(s.y)];
        if (((bits >> (7 - int(s.x))) & 1) == 1) c = vec3(0.35, 1.0, 0.45);
    }
    if (g.y < 4.0) c = mod(g.x + g.y, 2.0) < 1.0 ? vec3(0.55, 0.32, 0.18) : vec3(0.42, 0.24, 0.12);
    if (abs(g.x - 6.0) < 0.5 && abs(g.y - 19.0) < 0.5) c = vec3(1.0);   // a white star
    fragColor = vec4(c, 1.0);
}
