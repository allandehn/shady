# Shady

Shadertoy-style GLSL shaders as a calm, keyboard-first screensaver and viewer for [Omarchy](https://omarchy.org).

Designed together by [Allan](https://github.com/allandehn) and [Grok](https://x.ai).

![The start page with the four example shaders](docs/list.png)

![The editor open on Northern Veil](docs/editor.png)

## Features

- Paste Shadertoy code (`mainImage`, `iTime`, `iResolution`, `iMouse`, `iChannel0–3`) and it runs.
- Star, hide, multi-select, Trash with undo.
- An editor with a live preview, built-in noise textures or your own images as channels, and every compile error linked to its line.
- Quality and fps per shader, auto-adapt on slow machines, and a pause for shaders that are too heavy.
- Follows your Omarchy theme live. English and Danish.

Press `?` in the app for every key.

## Install

On Omarchy, run:

```
curl -fsSL https://raw.githubusercontent.com/allandehn/shady/main/install.sh | bash
```

It adds the Shady package repository, with packages signed by Allan Kristensen (key `AC6F F13C 76D8 916C CCF2  DA05 C322 5162 B525 7DB4`, the same key as OmaEar), and installs the app. Updates then arrive with `omarchy update`. Open **Shady** from the app launcher, or run `shady`.

Prefer to do it by hand? Read [install.sh](install.sh). It's a handful of lines.

## Build from source

An alternative to the package above.


Needs Qt 6.6+ (`qt6-base`, `qt6-declarative`, `qt6-shadertools`, plus `qt6-tools` and `vulkan-headers` to build), `glslang`, CMake and Ninja:

    sudo pacman -S --needed qt6-base qt6-declarative qt6-shadertools qt6-tools vulkan-headers glslang cmake ninja
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    sudo cmake --install build      # installs /usr/local/bin/shady

## Uninstall

If you installed the package:

    sudo pacman -Rns shady

If you built from source:

    sudo rm /usr/local/bin/shady

Your shaders and settings stay until you remove them too:

- `~/.local/share/shady`: your shaders (plain `shader.glsl` + `shader.toml` folders) and the Trash
- `~/.cache/shady`: compiled shaders and preview stills (safe to delete any time)
- `~/.config/shady`: settings

To also remove the Shady package source, delete the `[shady]` section from `/etc/pacman.conf`. Only if OmaEar isn't installed either (it uses the same key), run `sudo pacman-key --delete AC6FF13C76D8916CCCF2DA05C3225162B5257DB4`.

## License

MIT. See [LICENSE](LICENSE). The example shaders are MIT too.
