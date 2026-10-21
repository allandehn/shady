# Notes for agents working on Shady

- App id and data folders: `shady` (`~/.local/share/shady/`, `~/.cache/shady/`,
  `~/.config/shady/settings.toml`).
- Theme: Omarchy 4 applies the palette at the fixed path
  `$HOME/.local/state/omarchy/current/theme/colors.toml` (no env var, not
  XDG_STATE_HOME) and the name in `.../current/theme.name`. Watch those and the
  folder; never read `$OMARCHY_PATH/themes` or `~/.config/omarchy/themes`.
- `/workspace/shader-saver-design/design-brief.md` is the source of truth for
  behaviour and design; the v2 mockups show it. v1 is historical.
- Commit, push, tag or release only when Allan asks for it.
- Never touch Allan's desktop or his computers: no input, no `DISPLAY`/`WAYLAND_DISPLAY`, no copying files to them. Test headlessly only (below).
- Stack: C++17, Qt 6 Quick, shaders through RHI (Vulkan, OpenGL fallback),
  compiled with `qsb`. Priorities: smooth frames, low CPU/GPU, small RSS, fast
  startup. It must run fairly on potato machines (quality down to 10%, fps
  down to 10). Only one shader renders at a time.

## Building

    cmake -S . -B build -G Ninja && cmake --build build

## Testing

- Headless only: `tests/headless.sh ./build/shady`. It uses
  `QT_QPA_PLATFORM=offscreen`, `QT_FORCE_STDERR_LOGGING=1`, a private D-Bus via
  `dbus-run-session`, and throwaway `XDG_DATA_HOME` / `XDG_CONFIG_HOME` /
  `XDG_CACHE_HOME`, plus a throwaway `HOME` (the Omarchy theme path is under it).
  If the offscreen plugin has no GL (Debian), the UI test
  reruns on a private `xvfb-run -a` display, never a real one.
- `./build/shady --self-test` for the core tests (storage, TOML, fps steps,
  qsb compiling, error line mapping); `./build/shady --ui-test [--screenshot
  out.png]` drives the app's own window (list, star, hide, filter, viewer keys,
  saved settings). Both need fresh throwaway XDG dirs; the script does that.
- Never send input to Allan's desktop, never use his `DISPLAY` /
  `WAYLAND_DISPLAY`, and never touch his running Shady or his real data.

## Releasing

- Always use `scripts/release.sh X.Y.Z [NOTES_FILE]`; never hand-roll a release.
  Run `scripts/release.sh --dry-run X.Y.Z` first.
- It runs preflight checks, bumps `CMakeLists.txt` and `packaging/PKGBUILD`,
  builds, runs the headless tests, commits "Release X.Y.Z", tags and pushes,
  signs the package with key AC6FF13C76D8916CCCF2DA05C3225162B5257DB4, updates
  the signed pacman repo on the `repo` GitHub release, publishes "Shady X.Y.Z"
  as Latest and checks it publicly. It stops at the first failure.
- Release notes come from NOTES_FILE or `$NOTES`; a real run with neither dies
  at once. It never opens an editor or prompt (GIT_EDITOR/EDITOR=false,
  GIT_TERMINAL_PROMPT=0, stdin from /dev/null).
- Every step prints `[HH:MM:SS] ==> Step` and runs under `timeout`; a hang fails
  loudly with the step name. Scale the limits with `SHADY_TIMEOUT_SCALE=2`.
- Signing uses gpg-agent's normal graphical pinentry: preflight makes a test
  signature (300s timeout), which may pop up a passphrase dialog on Allan's
  desktop. If it times out, tell Allan a passphrase dialog may be waiting.

## Tests (more)
`tests/headless.sh` runs these: self-test, UI test, `--potato-test` (with SHADY_SOFTWARE=0, it covers the benchmark, iChannels on screen, auto-adapt and the too-heavy pause) and `--danish-shot` (SHADY_LANG=da). Run `i18n/update.sh` after you change qsTr strings. Danish strings live in i18n/da.py.

## README screenshots
`./build/shady --doc-shots docs` (in the same throwaway, headless setup as the tests, with SHADY_LANG=en) refreshes `docs/list.png` and `docs/editor.png`.

## Text fit
Every screenshot step in the UI, potato and language tests runs `fits()`. It fails if any visible Text spills past the nearest background, clip or window, or overlaps another Text in the same box. The language test (`--danish-shot`, run with SHADY_LANG=da and en) also checks a narrow 440 px window. Give a Label a width and let it elide, or let the layout reflow. Install iA Writer Mono (github.com/iaolo/iA-Fonts) system-wide so headless screenshots match Omarchy.
