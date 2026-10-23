# Shady

Shadertoy-style GLSL shaders as a calm, keyboard-first screensaver and viewer for [Omarchy](https://omarchy.org).

Designed together by [Allan](https://github.com/allandehn) and [Grok](https://x.ai).

![The start page with the five example shaders](docs/list.png)

![The editor open on Northern Veil](docs/editor.png)

## Features

- Paste Shadertoy code (`mainImage`, `iTime`, `iResolution`, `iMouse`, `iChannel0–3`) and it runs. A `// @name` / `@author` / `@description` / `@license` header fills in the details.
- Five examples, including Crouwel's New Alphabet: glyphs after Wim Crouwel drawing themselves on a grid.
- Click a shader to run it. Hover a card to star it or trash it, with tooltips.
- The viewer shows a centred badge when a shader starts (name, author, keys) and again when you change quality, fps or auto-adapt.
- Key hints along the bottom of every screen, written as `key:action`, and `?` for the full list. The mouse back button works like `esc`.
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

## Build it yourself (dev-install)

Download the script from a release tag, check its checksum, then run it:

```
curl -fsSLo /tmp/shady-dev-install.sh https://raw.githubusercontent.com/allandehn/shady/vNEXT/scripts/dev-install.sh
echo "0000000000000000000000000000000000000000000000000000000000000000  /tmp/shady-dev-install.sh" | sha256sum -c
bash /tmp/shady-dev-install.sh
```

By default it builds the latest release tag. Add `--main` to build the latest work on `main` before it's released, and set `SHADY_DEV_SHA=<commit>` to refuse anything but that exact commit. It builds a pacman package (version like `0.2.0.r3.g1a2b3c4`) in `~/.cache/shady-dev`, runs the headless tests, then runs `sudo pacman -U`, which shows what it installs and asks you to confirm. The next release replaces it on `omarchy update`. `--no-install` builds and tests only, and `--clean` forces a full rebuild.

## Build from source

An alternative to the package above.


Needs Qt 6.6+ (`qt6-base`, `qt6-declarative`, `qt6-shadertools`, plus `qt6-tools` and `vulkan-headers` to build), `glslang`, CMake and Ninja:

    sudo pacman -S --needed qt6-base qt6-declarative qt6-shadertools qt6-tools vulkan-headers glslang cmake ninja
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    sudo cmake --install build      # installs /usr/local/bin/shady

## Shader metadata header

Start a shader with a comment and Shady fills in the editor's fields when you paste or edit it:

```glsl
// @name Crouwel's New Alphabet
// @author Shady
// @description Glyphs on a grid, after Wim Crouwel
// @license MIT
```

`Name:` / `Author:` / `Description:` / `License:` (also `Title:`, `By:`) work too, case-insensitive, in `//` lines or one leading `/* */` block. A field follows the header while it's empty or still matches it; once you type your own value it stays yours. Editing the fields doesn't rewrite the source.

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
