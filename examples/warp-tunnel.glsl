// @name Warp Tunnel
// @author Shady
// @description A checkered tunnel
// @license MIT
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 p = (2.0 * fragCoord - iResolution.xy) / iResolution.y;
    float r = max(length(p), 1e-3);
    float a = atan(p.y, p.x);
    vec2 uv = vec2(a / 3.14159 * 6.0, 1.0 / r + iTime * 0.8);
    float c = mod(floor(uv.x) + floor(uv.y * 2.0), 2.0);
    vec3 col = mix(vec3(0.1, 0.1, 0.95), vec3(0.92, 0.25, 0.5), c);
    col *= smoothstep(0.0, 0.6, r);
    fragColor = vec4(col, 1.0);
}
