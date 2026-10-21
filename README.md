# Shady

Shady runs Shadertoy-style GLSL shaders as a screensaver or in a window on
[Omarchy](https://omarchy.org). It's calm and keyboard-first: the theme
recolours the layout, the shaders supply all the colour.

Designed together by [Allan Kristensen](https://github.com/allandehn) and [Grok](https://x.ai).

![The shader list](screenshot-list.png)

![The viewer](screenshot-list-viewer.png)

> Early version: the shader list, the viewer and per-shader settings work.
> The editor, Trash, iChannels and screensaver mode come next.

## Build

Needs Qt 6.6+ (`qt6-base`, `qt6-declarative`, `qt6-shadertools`), CMake and Ninja.

    cmake -S . -B build -G Ninja
    cmake --build build
    ./build/shady

Options: `--self-test`, `--ui-test [--screenshot out.png]`, `--version`.

## Keys

List: `←→↑↓` move · `Enter` open · `S` star/unstar · `H` hide/unhide ·
`Tab` All / Starred / Hidden · `Esc` close

Viewer: `↑↓` quality (10% steps, 10–100%) · `←→` fps (10, 15, 24, 30, 60, 120,
then your display's max) · `F` fullscreen · `Esc`/`Q` back

## Your shaders

Each shader is a plain folder in `~/.local/share/shady/shaders/<slug>/`:
`shader.glsl` is your code exactly as written, `shader.toml` holds name, author,
license, description, quality, fps, starred, hidden and the channel setup.
Compiled shaders and preview stills live in `~/.cache/shady/` and can be
deleted any time. On first run Shady adds four small example shaders of its own.

## How it works

Shaders are written the Shadertoy way, `void mainImage(out vec4 fragColor, in vec2 fragCoord)`
with `iTime`, `iResolution`, `iFrame` and `iMouse`. A hidden wrapper turns that into
Vulkan-style GLSL 440 (one uniform block, y flipped so `fragCoord` starts bottom left),
which the `qsb` tool compiles at runtime (cached by source hash) for Vulkan and
OpenGL. Compile errors point at your own line numbers. Qt Quick draws it with a
`ShaderEffect`. Quality renders into a smaller layer and scales it up smoothly, and
fps is a timer, so nothing at all is drawn between frames. Vulkan is used when it
works, otherwise OpenGL.

## Tests

    tests/headless.sh ./build/shady

## License

MIT, see [LICENSE](LICENSE).
