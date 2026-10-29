// @name Kelp Drift
// @author Shady tests
// @description Grid-fill fixture for the headless recording (not shipped)
// @license MIT
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float r = length(uv), t = iTime * 0.3;
    float v = 0.5 + 0.5 * sin(4.70 * 12.0 * r - t * 4.0 + atan(uv.y, uv.x) * 1.0);
    vec3 c = 0.5 + 0.5 * cos(6.2832 * (vec3(0.0, 0.33, 0.67) + v * 0.73 + 0.83));
    fragColor = vec4(c * (0.6 + 0.4 * v), 1.0);
}
