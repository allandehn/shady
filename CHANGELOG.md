# Changelog

## Unreleased

- Live view: the caption now fades in quickly (180 ms) and fades out slowly (1.8 s, ease-out).

- Burn-in protection for screensaver use. New `--screensaver` launch flag (fullscreen viewer).
  In screensaver mode and whenever the viewer is fullscreen the whole output slowly orbits
  a few pixels (2–5 px, one turn per 5 min, overscanned so edges never show) with a gentle
  brightness (3–6 %) and hue wobble; stronger for shaders with a low liveliness score.
  Updated once a second, so it costs nothing on slow machines. Settings (`,`) → Burn-in protection turns it off.
- Liveliness score: each shader is rendered off-screen in the background (90 s of shader time at
  1 fps, 160×90, faster than realtime) and the share of pixels that ever change is shown as
  `lively:87%` on cards and in the viewer caption. Cached by source, so it runs once per version.
- Editor: `ctrl+h` shows a burn-in heatmap over the preview (red = pixels that never change).
- Editor: GLSL syntax colours from your theme (keywords, types, numbers, built-ins and Shadertoy uniforms, preprocessor lines, comments), following theme changes live. The line a compile error points to gets a faint red tint.
- Examples stay current: on startup, examples you haven't edited are updated to the version Shady ships (including libraries from 0.1.x and 0.2.0, so the new Crouwel colours arrive), and examples added in a release appear for existing users. Edited examples and fields you changed are left alone; examples you deleted or put in the Trash stay there.

## 0.2.0

- Shader metadata header: `// @name`, `@author`, `@description`, `@license` (also `Name:` etc.) fill in the editor's fields when you paste or edit.
- New example: Crouwel's New Alphabet, glyphs after Wim Crouwel drawn on a grid in colours from the whole spectrum.
- Click a card to run it; hover shows the star and trash buttons with tooltips.
- Viewer: a centred badge shows name, author, description and keys when a shader starts, and the setting you changed (quality, fps, auto-adapt). No bottom bar.
- Key hints on every screen, lower case and written as `key:action`; `?` lists every key and mouse action.
- The mouse back button works like `esc`.
- I-beam pointer over text fields.

## 0.1.1

- Packaging: the QML module is compiled into the binary; the package ships the .desktop file, icon and license. One-line installer.

## 0.1.0

First release.

- Shader list with star, hide, multi-select, Trash and undo; four example shaders on first run.
- Viewer with per-shader quality and fps, auto-adapt and a too-heavy pause.
- Editor with live preview, iChannels (built-in textures or your own images), and every compile error listed and linked to its line.
- First-run benchmark sets sensible defaults; software rendering starts at the minimum.
- Follows the Omarchy theme live. English and Danish.
