// @name Crouwel's New Alphabet
// @author Shady
// @description Glyphs on a grid, after Wim Crouwel
// @license MIT
// After Wim Crouwel's New Alphabet (1967): straight strokes on a coarse grid,
// 45-degree corner cuts, no curves. Glyphs draw themselves in, rest, and fade.

const float SPEED = 5.0; // playback speed: 1.0 = calm screensaver pace

// Polylines in grid units (x 0..4, y 0..8; x-height 6). (-1,-1) lifts the pen.
const vec2 P[] = vec2[](
    // 0 n
    vec2(0,0), vec2(0,6), vec2(3,6), vec2(4,5), vec2(4,0),
    // 5 e
    vec2(4,0), vec2(1,0), vec2(0,1), vec2(0,5), vec2(1,6), vec2(3,6), vec2(4,5), vec2(4,3), vec2(0,3),
    // 14 w (double width, x 0..8; bar under it is Crouwel's double-width marker)
    vec2(0,6), vec2(0,1), vec2(1,0), vec2(7,0), vec2(8,1), vec2(8,6), vec2(-1,-1), vec2(4,0), vec2(4,6), vec2(-1,-1), vec2(2,-1), vec2(6,-1),
    // 26 g (closed bowl, right stem drops into a descender with a short tail)
    vec2(4,0), vec2(1,0), vec2(0,1), vec2(0,5), vec2(1,6), vec2(4,6), vec2(4,-1), vec2(3,-2), vec2(1,-2),
    // 35 r
    vec2(0,0), vec2(0,6), vec2(3,6), vec2(4,5),
    // 39 i
    vec2(2,0), vec2(2,6), vec2(-1,-1), vec2(2,7), vec2(2,8),
    // 44 d
    vec2(4,8), vec2(4,0), vec2(1,0), vec2(0,1), vec2(0,5), vec2(1,6), vec2(4,6),
    // 51 s
    vec2(4,6), vec2(1,6), vec2(0,5), vec2(0,4), vec2(1,3), vec2(3,3), vec2(4,2), vec2(4,1), vec2(3,0), vec2(0,0),
    // 61 abstract: square spiral
    vec2(0,0), vec2(4,0), vec2(4,8), vec2(0,8), vec2(0,2), vec2(2,2), vec2(2,6),
    // 68 abstract: stepped stair
    vec2(0,0), vec2(0,2), vec2(1,3), vec2(2,3), vec2(2,5), vec2(3,6), vec2(4,6), vec2(4,8),
    // 76 abstract: framed bar
    vec2(1,0), vec2(0,1), vec2(0,7), vec2(1,8), vec2(3,8), vec2(4,7), vec2(4,1), vec2(3,0), vec2(1,0), vec2(-1,-1), vec2(2,2), vec2(2,6),
    // 88 end
    vec2(-1,-1));
const int G0[] = int[](0, 5, 14, 26, 35, 39, 44, 51, 61, 68, 76);
const int G1[] = int[](5, 14, 26, 35, 39, 44, 51, 61, 68, 76, 88);

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

// Square-capped stroke: distance to the box around segment a->b.
float segBox(vec2 p, vec2 a, vec2 b) {
    vec2 d = b - a;
    float L = length(d);
    vec2 t = d / max(L, 1e-4);
    vec2 q = p - a;
    float u = dot(q, t), v = dot(q, vec2(-t.y, t.x));
    return max(abs(u - 0.5 * L) - 0.5 * L, abs(v));
}

// Returns (distance to drawn part, distance to pen tip).
vec2 glyph(vec2 p, int g, float drawn) {
    float d = 1e3, tip = 1e3, acc = 0.0;
    int s = G0[g], e = G1[g];
    for (int i = 0; i < 14; i++) {
        int k = s + i;
        if (k + 1 >= e) break;
        vec2 a = P[k], b = P[k + 1];
        if (a.x < 0.0 || b.x < 0.0) continue; // (-1,-1) lifts the pen
        float L = length(b - a);
        float r = clamp(drawn - acc, 0.0, L);
        if (r > 0.0) {
            vec2 c = a + (b - a) * (r / L);
            d = min(d, segBox(p, a, c));
            if (r < L) tip = min(tip, length(p - c));
        }
        acc += L;
    }
    return vec2(d, tip);
}

vec3 hsv(float h, float s, float v)
{
    vec3 k = clamp(abs(fract(h + vec3(0.0, 2.0, 1.0) / 3.0) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
    return v * mix(vec3(1.0), k * k * (3.0 - 2.0 * k), s);   // smooth HSV, no harsh primaries
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec3 bg = vec3(0.102, 0.106, 0.149);          // #1a1b26
    vec3 pal[3] = vec3[](vec3(0.490, 0.812, 1.0),  // #7dcfff
                         vec3(0.478, 0.635, 0.969),// #7aa2f7
                         vec3(0.733, 0.604, 0.969));// #bb9af7

    float cell = iResolution.y / 38.0;            // grid unit in pixels
    vec2 slot = vec2(10.0, 12.0);                 // glyph 4x8 (w: 8x8) plus margin
    vec2 cols = floor(iResolution.xy / cell / slot);
    vec2 origin = 0.5 * (iResolution.xy - cols * slot * cell);
    vec2 g = (fragCoord - origin) / cell;          // grid units

    // Faint grid: tiny node dots.
    vec2 n = abs(fract(g + 0.5) - 0.5);
    float dots = (1.0 - smoothstep(0.03, 0.09, max(n.x, n.y))) * 0.11;
    vec3 col = bg * (1.0 - 0.25 * length(fragCoord / iResolution.xy - 0.5)) + dots * pal[1];

    vec2 id = floor(g / slot);
    if (all(greaterThanEqual(id, vec2(0))) && all(lessThan(id, cols))) {
                float period = 22.0;
        float ph = hash(id) * period;
        float tt = iTime * SPEED + ph;
        float cyc = floor(tt / period);
        float lt = mod(tt, period);
        float h = hash(id + cyc * 7.31);
        float on = step(0.45, hash(id * 1.7 + cyc * 3.1));   // about half the slots are busy
        int gi = int(h * 11.0) % 11;
        vec2 lp = g - id * slot - vec2(gi == 2 ? 1.0 : 3.0, 3.0);   // glyph space; w is double width
        float drawn = lt * 4.5;                    // grid units per second
        float fade = smoothstep(8.0, 10.0, lt) * (1.0 - smoothstep(14.0, 19.0, lt))
                   + (1.0 - step(8.0, lt)) * smoothstep(0.0, 1.0, lt);
        if (on > 0.5 && fade > 0.001) {
            vec2 r = glyph(lp, gi, drawn);
            // Per-glyph hue over the whole wheel: golden-ratio steps from a
            // hash keep neighbours apart; soft saturation and capped value so
            // the glow sits calmly on the navy instead of flashing.
            float hue = fract(hash(id + cyc * 1.13 + 5.0) + cyc * 0.618034);
            vec3 c = hsv(hue, 0.55, 0.95);
            float w = 0.16;
            float core = 1.0 - smoothstep(w - 0.04, w + 0.04, r.x);
            float glow = exp(-r.x * 3.5) * 0.30 + exp(-r.x * 1.6) * 0.05;
            float head = exp(-r.y * 4.0) * (1.0 - smoothstep(6.0, 8.0, lt));
            col += c * (core * 0.6 + glow) * fade + c * head * 0.35 * fade;
        }
    }
    fragColor = vec4(col, 1.0);
}
