// Northern Veil · Shady example · MIT
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    float t = iTime * 0.25;
    vec3 col = vec3(0.01, 0.02, 0.06);
    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        float y = 0.55 + 0.12 * sin(uv.x * (3.0 + fi) + t * (1.0 + 0.3 * fi) + fi)
                       + 0.05 * sin(uv.x * 11.0 - t * 2.0 + fi);
        float d = abs(uv.y - y);
        float band = exp(-d * (18.0 + 6.0 * fi));
        col += band * mix(vec3(0.1, 0.9, 0.6), vec3(0.3, 0.4, 1.0), fi / 2.0) * 0.8;
    }
    fragColor = vec4(col, 1.0);
}
