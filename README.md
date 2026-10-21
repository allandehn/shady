# Shady

Shady runs Shadertoy-style GLSL shaders as a screensaver or in a window on
[Omarchy](https://omarchy.org). It's calm and keyboard-first: the theme
recolours the layout, the shaders supply all the colour.

Designed together by [Allan Kristensen](https://github.com/allandehn) and [Grok](https://x.ai).

![A shader rendered by Shady](screenshot.png)

> Early scaffold: it renders one built-in shader full-screen. The shader list,
> editor, per-shader settings and screensaver mode come next.

## Build

Needs Qt 6.6+ (`qt6-base`, `qt6-declarative`, `qt6-shadertools`), CMake and Ninja.

    cmake -S . -B build -G Ninja
    cmake --build build
    ./build/shady

Options: `--quality 10..100` (render scale, default 50), `--fps 10..240`
(real frame limit, default 30), `--self-test`, `--screenshot out.png`, `--version`.

## Keys

`↑↓` quality · `F` fullscreen · `Esc`/`Q` close

## How it works

Shaders are written the Shadertoy way, `void mainImage(out vec4 fragColor, in vec2 fragCoord)`
with `iTime`, `iResolution`, `iFrame` and `iMouse`. A hidden wrapper turns that into
Vulkan-style GLSL 440 (one uniform block, y flipped so `fragCoord` starts bottom left),
which `qsb` compiles for Vulkan, OpenGL and the rest. Qt Quick draws it with a
`ShaderEffect`. Quality renders into a smaller layer and scales it up smoothly, and
fps is a timer, so nothing at all is drawn between frames. Vulkan is used when it
works, otherwise OpenGL.

## Tests

    tests/headless.sh ./build/shady

## License

MIT, see [LICENSE](LICENSE).
