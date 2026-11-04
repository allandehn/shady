// @name Smooth Gradient
// @author Shady tests
// @description Smooth gradient fixture for the CRT pass screenshots (not shipped)
// @license MIT
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    vec3 c = 0.5 + 0.5 * cos(6.2832 * (uv.x * 0.8 + vec3(0.0, 0.33, 0.67)) + uv.y * 2.0);
    c = mix(c, vec3(1.0), smoothstep(0.25, 0.0, length(uv - vec2(0.7, 0.65))));  // a bright spot for the glow
    fragColor = vec4(c, 1.0);
}
