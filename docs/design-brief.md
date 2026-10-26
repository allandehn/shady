# Shader screensaver: design brief

This is an app for Omarchy that runs GLSL shaders as a screensaver, either in a window or fullscreen. The current mockups are `shader-saver-mockup-v2-a@2x.png`, `shader-saver-mockup-v2-b@2x.png` and `shader-saver-mockup-v2-c@2x.png` in this folder. The v1 mockup (`shader-saver-mockup@2x.png`) is historical and superseded; don't build from it. Follow Omarchy's design language: it's calm and keyboard-first, the theme recolors the layout but never changes it, and the shaders supply all the color.

## Technical goals and stack
- **Priorities, in order:** smooth frames, then low CPU and GPU use, then a small memory footprint, then fast startup. The language doesn't matter; whatever serves these best on Omarchy wins.
- **Suggested stack:** a C++ app with Qt 6 Quick, rather than Quickshell. Shaders run through Qt's own GPU layer (RHI on Vulkan or OpenGL), compiled with `qsb`. That's a proven, widely available stack on Omarchy. It keeps the UI and theming simple and avoids running a whole shell runtime just for one app. Quickshell is a reasonable alternative if it turns out to be lighter in practice. Measure before deciding.
- **Keeping it lean:**
  - Only one shader renders at a time: the viewer, or the single card being previewed.
  - Card previews use still frames from the cache, and rendering stops completely when the window is hidden or the screen is off.
  - Quality lowers the render resolution, and fps is a real frame limit, not just skipped presents.
  - The screensaver mode could later be a separate, minimal entry point that loads only the renderer, without the list or editor.
- **Measuring:** for a reference shader at fixed settings, track memory use (RSS) at idle and while running, frame-time smoothness, and CPU and GPU load, so changes can be compared over time. Record the results per hardware class (AMD, Intel and NVIDIA GPUs, plus software rendering).

## Runs well on a potato
This is a core goal, not a nice-to-have. Omarchy runs on everything from new desktops with fast GPUs to old laptops with integrated graphics, and every one of them should get a fair, smooth experience at low quality and low fps.
- **Low floors.** Quality goes down to 10% and fps down to 10, and the lowest settings should still look intentional. Scaling up a low-resolution render should look soft, not blocky or broken.
- **Smooth over sharp.** If a shader can't keep up with its fps setting, the app automatically lowers its render resolution for that session to hold a steady frame rate, and the hint bar quietly says "adapted to 30%". The user's saved setting isn't changed unless they change it. The automatic adjustment can be turned off.
- **Graphics backends.** Vulkan is the default where it works well. The app falls back to OpenGL automatically on older GPUs, with no setup.
- **Software rendering.** When there's no real GPU (Mesa's software renderer, llvmpipe), the app says so once, in one calm line, and starts every shader at its minimum settings.
- **First-run defaults scale with the machine.** A quick, silent benchmark of a simple reference shader on first launch sets sensible defaults for new shaders: modest on fast machines and minimum on slow ones.
- **Never freeze the desktop.**
  - If a frame takes far too long, for example over 500 ms, rendering pauses and the viewer offers "This shader is too heavy here. Try lower quality?".
  - A shader that froze or crashed the app last time opens at minimum settings the next time.
- **List previews on slow machines.** Previews fall back to still frames only, and hover playback stops, once the app's automatic adjustment detects a slow machine.
- **Testing.** Test across hardware classes: a recent AMD or NVIDIA GPU, an older Intel integrated GPU, and the software renderer at minimum. The benchmark from "Technical goals" runs on each.

## Shared rules
- Take colors from the current theme's `colors.toml` and follow theme changes live. Use `background` for the base, `lighter_background` for cards and toolbars, `foreground` for text, a muted mix for secondary text, and `accent` only for selection, focus and the single primary button.
- Corners follow Hyprland's `decoration:rounding`, read through `hyprctl`, so they're square on a default install.
- Use the system monospace font (iA Writer Mono where available). Text size and weight carry the hierarchy, not color.
- Wrap every string in `qsTr()` and ship English and Danish from the start. Leave room for longer words.
- Colors crossfade over about 840 ms when the theme changes.

## Per-shader settings
Each shader keeps its own quality (10 to 100%) and fps (10 up to the display's maximum refresh rate, read from `hyprctl monitors`), since shaders perform very differently. A setting changed in the viewer is saved straight away for that shader only.

## 1. Shader list (panel)
- The header has an icon, "Shaders", and a small-caps count on the right such as "6 SHADERS".
- Cards sit in a 2-column grid, each with a preview on top and the name and "by author" below. Cards are neutral, with no colored borders.
- Previews are live but cheap: a still frame by default that plays at low quality while the card is hovered or focused. Only one preview plays at a time.
- Previews resume rather than restart. Each card remembers its shader's playback time when hover or focus leaves, and continues from there when you come back. This memory lasts for the session, meaning from when the window opens until it closes. A fresh session starts every card from its still frame.
- On hover or keyboard focus the card gets a 1 px accent outline, and the author line is replaced by that shader's saved settings, for example "60% · 120 fps".
- Removing a shader uses a hover-only "×" in the card's corner or the Delete key. It moves the shader to the Trash straight away, with no confirmation, since it can be restored, and focus moves to the previous card. The status line says "Moved to Trash · Ctrl+Z undo". Restoring from the Trash shows the matching note "Restored … · Ctrl+Z undo" (with the shader name, or the count for a selection).
- The last card is a quieter "+ New shader" card with an `N` key hint.
- A slim hint area at the bottom, at most two rows (see Design, resolved Oct 21, Hint line).
- Arrow keys move between cards, Enter opens one, and Escape closes the panel.

### Starred, hidden shaders and Trash
- Any shader can be starred (key `S`, which toggles Star/Unstar) or hidden (key `H`). Both also appear in the card's right-click menu, and both are saved in the shader's `shader.toml`.
- Starred shaders sort first in the list and show a small, quiet star on the card. The star uses the foreground color, not the accent, so the cards stay neutral.
- Hidden shaders disappear from the normal list, from the screensaver rotation and from the count. Nothing is deleted.
- The panel header gets a small filter: All · Starred · Hidden · Trash, cycled with `Tab`. The count follows the filter, for example "2 HIDDEN".
- In the Hidden view the cards are shown dimmed, and `H` (or the menu) unhides one. An empty view says so in one calm line, for example "No hidden shaders".
- Hiding and unhiding are instant and need no confirmation, since nothing is lost.
- **Trash.** Trashed shaders show dimmed in the Trash view, sorted by when they were removed, newest first. There `Enter` or the menu restores one, and `Del` asks once before permanently deleting: the whole selection when there is one, otherwise the focused card (the same "Delete forever?" dialog, with Delete forever focused). Trashed shaders never play as the screensaver and don't count anywhere else.
- **Shift+Del deletes permanently** (decided by Allan, Oct 21). In the list it skips the Trash for the focused card or the whole selection, and shows the same "Delete forever?" confirmation with Delete forever focused. The Trash view's footer has one action (Clear cache lives in Settings, see Design decisions #3):
  - **Empty Trash.** It asks once with the number of shaders, for example "Permanently delete 4 shaders?", using the shared confirmation. It's disabled when the Trash is empty. It has no single key: reach it with Tab to the footer, then Enter.
- As the screensaver, the app uses starred shaders if there are any, and otherwise every shader that isn't hidden.
- Hints use at most two rows per view, and `?` opens the full key sheet (see Design, resolved Oct 21, Hint line).

## 2. Editor
- The header says "New shader" (or the shader's name) with an accent dot while there are unsaved changes.
- It has fields for Name, Author, License (optional) and Description (small-caps muted labels), then a monospaced code area with line numbers.
- A preview sits to the right of the code. It shows the last successful compile, so a broken edit never blanks it.
- Compiling is explicit, so nothing runs constantly while typing. A quiet Compile button (Ctrl+Enter) sits next to the status line, and Save also compiles first. A shader that fails to compile can still be saved as a draft, but it is marked as not compiling in the list.
- Under the code a calm status line reads "Compiled · 0 errors", or "Not compiled" after edits. Errors show there in plain text with the line number and never as a popup. Ideally the offending line number is highlighted too.
- The footer has "Esc cancel" as plain text on the left and one accent-filled Save button on the right. Escape asks before throwing away unsaved changes.

## Shader format and compiling
- Shaders are written Shadertoy-style: the user writes `void mainImage(out vec4 fragColor, in vec2 fragCoord)` and may use `iTime`, `iResolution`, `iFrame` and `iMouse`. The app wraps this in the boilerplate Qt needs (version line, uniform block, inputs and outputs), so most Shadertoy single-pass shaders can be pasted in.
- Shaders can use up to four input images, `iChannel0` to `iChannel3`, as on Shadertoy, with `iChannelResolution` available too. Many good shaders, such as iq's Clouds, read noise textures through them.
- The app ships a small set of built-in textures generated in code, so they carry no licensing questions. They cover what most Shadertoy shaders expect, and users can also pick their own image file for a channel.
  - **RGBA noise, 64 and 256 px.** In the 256 px version the green channel must be the red channel shifted by (37, 239) pixels, and the alpha channel must relate to blue the same way. This is Shadertoy's trick that lets shaders like iq's Clouds get 3D noise from a single 2D texture read, and without it they look smeared or wrong.
  - **Grey noise, 2D.**
  - **Grey noise, 3D**, a 32×32×32 volume texture, as used on `iChannel2` in Clouds.
  - **An organic-looking texture.**
- Reference case: iq's [Clouds](https://www.shadertoy.com/view/XslGRr) uses RGBA noise on `iChannel0` and `iChannel1` and 3D grey noise on `iChannel2`, and leaves `iChannel3` empty. With its default setting it only reads `iChannel0`. It should look right out of the box with the built-ins.
- Licensing, informational and without judgement. Many Shadertoy shaders, Clouds included, carry a license or copyright notice. The app itself never bundles other people's shaders, but it doesn't police what users paste. Every shader has an optional License field next to Author. When pasted code starts with a copyright or license comment, the field is pre-filled with a short summary such as "© Inigo Quilez 2013 · see source", and the user can edit or clear it. The license shows as a plain muted line under the author in the viewer's info block and in the editor, with no warnings, colors, icons or blocking. A later export feature carries the field and the original comment along unchanged.
- The editor gets a row of four small channel slots under the code. Each shows a thumbnail and opens a picker of the built-ins plus "Choose image…". Each slot also has its filtering (linear or nearest) and wrap (repeat or clamp) settings, since noise-based shaders usually need repeat with linear filtering. The channel setup is saved with the shader.
- Qt 6 can't run raw GLSL. The app compiles the wrapped source with Qt's `qsb` tool (package `qt6-shadertools`, a dependency) into a `.qsb` file, cached per shader and rebuilt only when the source changes.
- Error line numbers from `qsb` are mapped back to the user's own lines, so the hidden boilerplate never shows.
- Quality is applied by rendering at a reduced resolution and scaling up, and fps by throttling the frame timer.
- Fps steps are fixed: 10, 15, 24, 30, 60, 120, then the display's maximum refresh rate, keeping only those up to that maximum.
- New shaders start at a modest default (for example 50% and 30 fps), so a heavy shader can't make the machine crawl on first run.

## Shadertoy compatibility pitfalls (for the build)
Shadertoy runs WebGL code, which is GLSL ES, in a browser. Browsers quietly smooth over many problems, but desktop Vulkan and some drivers, notably Mesa's AMD driver, don't. Earlier experiments ran into these, especially with strict toolchains such as Rust's wgpu, which uses a very strict GLSL reader. `qsb` uses glslang, which is more forgiving, but the app should still handle the following.
- **Wrapping.** The hidden boilerplate turns the Shadertoy shape into Vulkan-style GLSL 440:
  - uniforms go in one uniform block, plus samplers with explicit bindings
  - `texture2D` and `textureCube` are mapped to `texture`
  - precision qualifiers are dropped
  - the right sampler type (2D, 3D or cube) is declared for each channel
- **Flipped y-axis.** Shadertoy's `fragCoord` starts at the bottom left, while Qt and Vulkan start at the top left. The wrapper flips y so shaders aren't upside down, and `iMouse` gets the same flip.
- **Uninitialized variables.** This is the most common driver surprise. Variables that are declared but never set often happen to be zero in browsers and on some drivers, but they're garbage on others, notably AMD, which shows up as flicker, black areas or noise. The app can't safely rewrite users' code. Instead, when a shader compiles but renders oddly, the status line can mention this as a likely cause, in plain words.
- **Math that browsers forgive.** `pow` with a negative base, `sqrt` of a negative, `normalize` of a zero vector, `atan(0, 0)` and dividing by zero can all give NaN on some drivers, which shows as black pixels. This gets a calm hint, just like uninitialized variables.
- **Reserved words.** GLSL 4.x reserves names that WebGL allows, such as `sample`, `input`, `output`, `filter`, `active` and `common`. If a shader uses them as variable or function names, the wrapper can rename them automatically, since that's a safe, mechanical change.
- **Integer and float mixing.** Implicit conversions differ between GLSL versions, for example `float x = 1;` or comparing an int with a float. The app should report these clearly, with the user's own line number.
- **Time precision.** A screensaver can run for hours, and a float `iTime` gets less precise over time, which makes motion stutter. The app wraps `iTime` after a generous period, for example every hour with a quick crossfade, or resets it whenever the saver starts.
- **Out of scope for now:** multipass shaders (Buffer A to D) and the Common tab. These are clearly reported as unsupported rather than failing in strange ways.
- **Test set:** a handful of well-known Shadertoy shaders, kept in the developer's test setup only and never shipped, covering iq's Clouds (2D and 3D noise channels), a raymarcher, a shader that uses `iMouse`, and one with an uninitialized variable, checked on AMD, Intel and NVIDIA drivers, and the software renderer.

## 3. Viewer
- The shader fills the window. Top left there's a block on a subtle translucent scrim with the name (bold), author and description.
- A bottom hint bar reads "↑↓ quality 60% · ←→ fps 120 · F fullscreen · Esc close", with "Saved for this shader" on the right.
- After 3 seconds without input, the overlay and the mouse pointer fade out together in about 840 ms, leaving only the shader. Any key press or mouse movement brings them back.
- When quality or fps changes, show the new value briefly (about 1.5 s) even while the rest of the overlay stays hidden.
- Keys: ↑↓ change quality in 10% steps, ←→ change fps in steps, F toggles fullscreen, and Esc or Q closes. Steps stop at the limits instead of wrapping around.

## Storage
- Everything is plain files that are easy to read, back up or put in git. `<app>` below stands for the app's final id.
- Each shader is one folder in `~/.local/share/<app>/shaders/<slug>/`:
  - `shader.glsl` holds the user's code exactly as written, without the hidden boilerplate.
  - `shader.toml` holds the name, author, license, description, quality, fps, the four channel setups (built-in name or image file, filter, wrap), and created and modified dates.
  - Any user-picked channel images are copied into the folder, so a shader never breaks when the original file moves.
- The slug comes from the name, for example `northern-veil`, and gets a `-2` suffix if that one is taken. Renaming a shader changes only the name field, not the folder, so nothing breaks.
- Compiled `.qsb` files and preview still frames live in `~/.cache/<app>/`, keyed by a hash of the source. They can be deleted at any time and are rebuilt when needed.
- App-wide settings, such as the last selected shader and window size, go in `~/.config/<app>.conf`.
- Writes are atomic: write a temporary file, then rename it over the old one. Quality and fps changes in the viewer are saved straight away, but batched about half a second apart, so holding an arrow key doesn't hammer the disk.
- Preview resume positions are only kept in memory for the session and never written to disk.
- Removing a shader moves its folder to `~/.local/share/<app>/trash/<slug>/` and records when it was removed in its `shader.toml`. Restoring moves it back, adding a suffix if that slug is taken in the meantime. Emptying the Trash, or deleting a single shader permanently, deletes the folders and their cache files. This is the app's own Trash, separate from the desktop trash.
- A shader folder is the natural unit for the later import, export and duplicate features: export can simply zip the folder.

## Multiple monitors
- In the viewer, F fullscreens the window on the monitor it's on, like a video player.
- As the real screensaver, it runs one instance per monitor, all showing the same shader by default. Showing a different shader on each screen is a later option.
- Each instance uses the shader's saved quality and fps, so several screens multiply the load. That's one more reason for modest defaults.

## Design Champ
Every item from the earlier "For Design Champ" list is designed in the v2 mockups and described below. That covers the License field, iChannels, the Compile button and statuses, broken shaders, Starred, the filter, Trash, Hidden, the right-click menu, the hint lines, the delete confirmation, the value flash, the low-end messages and Danish lengths. Multi-select and sharing are also designed up front, as asked.

Still open for Design Champ:
- ~~Frame 3 focus~~ Done Oct 21: frame 3 shows Delete forever focused, per Allan's decision.
- ~~Danish lengths~~ The strings are approved by Allan (Oct 21). Check them in the first build.

### Design, resolved Oct 21
Mockups: `shader-saver-mockup-v2-a@2x.png` (list states), `-v2-b@2x.png` (editor, Hidden view), `-v2-c@2x.png` (viewer states, sharing, monitors). v1 (`shader-saver-mockup@2x.png`) is historical and superseded.
- **License.** A third field on the Name/Author row, labeled "LICENSE (OPTIONAL)". In the viewer it's a muted fourth line in the info block, shown only when set.
- **iChannels.** A "CHANNELS" block of four slots in a 2×2 grid under the preview, each with a 40 px thumbnail, "iChannelN" in small caps and the source name. Empty slots show a muted "+". Enter or a click opens a small popover listing the built-in textures plus "Image file…", with Filter (linear/nearest) and Wrap (repeat/clamp) as two-option toggles. The open slot gets the accent outline.
- **Compile and status.** A quiet outlined "Compile  Ctrl+↵" button sits at the right end of the status line. The states are "Not compiled" (muted), "Compiled · 0 errors" (muted), and an error shown as "Line 12" plus the plain message. An error is the only red in the app: the line number text and a 2 px bar on the code line, which also gets a faint tint. The footer shows "Saves as draft" next to Save while the code is broken.
- **Broken shader in the list.** The preview is hatched with a muted "Not compiling" label, and the sub-line reads "Draft · line 14". No red, no badge.
- **Starred.** A small solid star in the card's top-right corner on a background-colored chip, in the foreground color.
- **Filter.** A text tab row under the title: All · Starred · Hidden · Trash, plus a muted "Tab" hint. The active tab is bold foreground with a 2 px accent underline. The count follows the filter ("2 HIDDEN", "3 IN TRASH", "3 OF 6 SELECTED").
- **Trash.** Dimmed cards whose sub-line reads "Removed today / 3 d ago". The footer has one outlined Empty Trash button. Clear cache moved to Settings (Design decisions #3). The hints are "Enter restore · Del delete forever · Tab footer · Esc close".
- **"Moved to Trash".** A status note just above the hint line: "Moved to Trash · Ctrl+Z undo". It fades after about 4 s.
- **Hidden view.** Dimmed cards with the sub-line "Hidden". `H` unhides. The empty state is one centered muted line, "No hidden shaders" (and the same pattern for the other views).
- **Right-click menu.** Open, Edit (Ctrl+E), Star, Hide, Select, Share… (only once sharing ships; the mockup's "Share (later)" row is illustrative only), a divider, then Move to Trash last, each with its key on the right in muted text. The highlighted row uses a soft accent tint, not solid accent.
- **Hint line.** The full line doesn't fit in one row of a 2-column panel (about 48 characters). Use two rows: "Enter open · S star · H hide · Del trash" and "Space select · N new · ? all keys" (`E share` is added when sharing ships). `?` opens a full key sheet. Each view swaps in its own hints.
- **Delete confirmation.** Design Champ found no shared Omarchy dialog a third-party app can call (still to verify), so the app draws its own: a square box on a dark scrim with a bold title "Delete forever?", one line giving the count, and a muted "This can't be undone.". Two buttons: a plain outlined Cancel on the left, and "Delete forever" on the right, which has focus by default and carries the accent focus outline. Neither is accent-filled and neither is red. Enter deletes, Tab switches to Cancel, and Esc cancels. Del is not a key inside this dialog. (Outside the Trash view, Del moves to the Trash with no confirmation, since that can be undone; in the Trash view, Del deletes permanently and always opens this dialog.) The hint line reads "Enter delete · Tab switch · Esc cancel". **Decided by Allan, Oct 21 16:51:** focus starts on Delete forever because the user chose it deliberately. This overrides the earlier "focus starts on Cancel, Enter cancels" safe-default rule.
- **Value flash.** When the overlay is hidden, a small centered scrim chip shows just the changed value, e.g. "quality 70%", with a muted "saved for this shader" under it. It stays about 1.5 s, then fades.
- **Potato messages.**
  - "adapted to 30%" sits right-aligned in the hint bar, in the place of "Saved for this shader". The hint `A` turns auto-adapt off.
  - The too-heavy pause dims the shader and shows a small box: "Paused / This shader is too heavy here. Try lower quality?", "Esc close", and an accent button that names the actual value ("Try 20%").
  - The software-rendering note is one line in the list status area, shown once.
- **Multiple monitors.** A "SCREENSAVER ON" radio group: All monitors · same shader (the default), Primary only, and Each its own (later, muted). Under it, a muted "2 monitors · load ×2".
- **Danish.** Every label is laid out with about 30% spare room. Tabs, buttons and menu items size to their text, never fixed widths. Buttons to check: "Tøm papirkurv", "Ryd cache", "Flyt til papirkurv".
- **Multi-select (design now, build in v1; see Design decisions #7, #10).** Space toggles a card. Shift plus any arrow extends the selection: Shift+←→ by one card, Shift+↑↓ by one row, always from the anchor card (the first one picked) in reading order (decided by Allan, Oct 21). A selected card gets a small accent check chip at its top left and a muted outline, while focus keeps the accent outline, so the two never look the same. While anything is selected, a selection bar appears above the hints: "3 selected", then Hide and Trash, in that order (Share… is added later). The hints switch to "H hide · Del trash · Ctrl+Z undo / Space toggle · Shift+←→↑↓ extend · Esc clear".
- **Where Share lives.** `E`, the right-click menu, and the selection bar. "Share all" and "Share starred" go in the `?` key sheet and the right-click menu of an empty spot in the panel, not the header.
- **Share dialog (later).** It's titled "Share 3 shaders" and has a pack-name field, an "INCLUDES" list (thumbnail, name, author · license), and checkboxes for "Copy post text" and "Still image" (no clip). The footer has "Esc cancel" and an accent "Save pack" button.
- **Import preview (later).** It's titled "Import <file>" with a muted line for author · count · date, then rows that each end in a state: "new", "you have it · skip", or "differs · add copy" (in foreground). Below that, "Authors and licenses are kept as they are." and an accent "Import N" button.

### States, focus and motion (Design Champ, final check Oct 21)
- **Loading.** While a preview frame is being built, the card shows a plain `lighter_background` block with a muted "Preparing…". No spinner. On a first launch with many shaders, the count line reads "6 SHADERS · PREPARING 2".
- **Empty library.** Only the "+ New shader" card is shown, with one muted line above it: "No shaders yet". Every other empty view shows one centered line ("No starred shaders", "No hidden shaders", "Trash is empty").
- **Unreadable shader.** If `shader.toml` or `shader.glsl` can't be read, the card shows the hatched preview with "Can't read this shader" and the folder name. It can still be trashed. No popup.
- **Save or IO errors.** Show one plain line in the status area ("Couldn't save · disk full"). Keep the editor open and the unsaved dot on.
- **Focus order.**
  - List: filter tabs, then the grid in reading order, then the footer buttons. `Tab` cycles the filter only while focus is in the header or grid.
  - Editor: Name → Author → License → Description → code → Compile → iChannel0–3 → Save.
  - Dialogs: the action the user chose comes first (Delete forever), then Cancel.
  - Focus is always visible as a 1 px accent outline.
- **Every action has a key.**
  - Ctrl+E opens the focused shader in the editor, Ctrl+S saves in the editor, Ctrl+Enter compiles, and Shift+Del deletes permanently. Empty Trash has no key and is reached with Tab.
  - `,` opens Settings (approved by Allan), `?` opens the key sheet, Ctrl+A selects all, and Shift plus any arrow extends the selection.
  - The Menu key or Shift+F10 opens the card's right-click menu. The `?` key sheet lists every key here, including Ctrl+E and Menu/Shift+F10.
  - Space on a card does the same as clicking its checkbox.
  - Nothing is mouse-only, and nothing is hover-only without a key.
- **Reduced motion.** If Hyprland animations are off (`animations:enabled = false`), every fade becomes an instant change: the overlay hide, the value flash, the theme crossfade and status notes. Timings stay the same (3 s idle, 1.5 s flash, 4 s note). Hover previews still play, since they're the content, not decoration.
- **Danish room.** Size labels to their text, and wrap the hint rows rather than cutting them off. The Danish strings are approved by Allan (Oct 21). The longest ones to test (including "Stjernemarkerede" for the Starred tab, "Fjern stjerne" for Unstar and "Ingen stjernemarkerede shaders" for its empty state): "Slet permanent", "Tøm papirkurv", "Flyt til papirkurv", "Gemmes som kladde", "Denne shader er for tung her. Prøv lavere kvalitet?", "Gemt for denne shader". The viewer's hint bar wraps to two rows in narrow windows, as in frame 7.
- **Naming.** Starred + S chosen for coherence with the star icon; F reserved for fullscreen (Oct 21). The right-click menu says "Star" or "Unstar" depending on state. In list context `S` is used only for this; Ctrl+S saves in the editor.
- **Wording.** Dialogs always name the action ("Delete forever", "Try 20%") and never say plain "OK" or "Yes".

## Design decisions (approved by Allan, Oct 21)
Allan approved items 1 to 9 ("I want all of them", Oct 21, 15:59) and proposed item 10 himself. All ten are approved, and the mockups show them. If a decision ever conflicts with the rest of this brief, the brief wins. On the Oct 21 check none did. #4 refines the editor rule "Ideally the offending line number is highlighted too": the line number and the bar are red, and the message stays plain.
1. ✅ **Undo.** Ctrl+Z undoes the last trash, restore, hide or unhide. The status note reads "Moved to Trash · Ctrl+Z undo".
2. ✅ **Keys.** Hints are two rows at most per view. `?` opens a full key sheet (frame 14), and "Share all" and "Share starred" go there later too.
3. ✅ **Clear cache.** It lives in a small Settings sheet (key `,`), next to the "Screensaver on" monitor choice, with the cache size and "Rebuilt when needed", and no confirmation. The Trash footer has only Empty Trash.
4. ✅ **Errors.** The only red in the app is the error's line number and a 2 px bar on that code line. The message stays in foreground text.
5. ✅ **Adapted.** "adapted to 30%" takes the place of "Saved for this shader" while it applies.
6. ✅ **Too heavy.** The button names the value, e.g. "Try 20%".
7. ✅ **Multi-select ships in v1 without Share.** The selection bar has Hide and Trash. Share… joins the bar, the right-click menu and the `E` key when sharing ships.
8. ✅ **Sharing uses a still image only.** One PNG per pack, or a grid of up to four. No video clip.
9. ✅ **Star and × sit on a small background-colored chip**, so they stay visible on bright previews.
10. ✅ **Checkboxes (proposed by Allan).** In selection mode, every unselected card shows an empty checkbox (muted outline on a background chip) in the top-left corner, where the accent check sits on selected cards. Outside selection mode, only the hovered or focused card shows the empty checkbox. Clicking it selects that card and enters selection mode. Leaving selection mode (Esc, or nothing selected) hides the boxes again.

## Open questions
- Does Omarchy have a shared confirmation dialog a separate app can call? Design Champ found none, so the mockups show the app drawing its own in the same style. Verify this before building.

## Later
Everything below is out of scope for the first version. It's written down so that the first version's choices, especially storage, don't block it.

### Sharing
- **One shader, many or a whole set.** You can share a single shader, a selection, All or Starred. Hidden and trashed shaders are never included unless picked one by one.
- **Pack file.** A share is one file, such as `northern-veil.shaderpack` or `my-starred.shaderpack`. It's a zip of the shader folders exactly as stored, plus a small manifest with the pack name, author, date and app version. Compiled caches aren't included, so packs stay small.
- **Importing.** You open the file or drop it on the panel, and the app shows a calm preview listing what's inside. Shaders you already have are skipped or added as copies, never silently overwritten. Imported shaders keep their author and license fields untouched.
- **Sharing on X.** Many Omarchy builders are there, but a post can't carry a file. So "Share…" would:
  - save the pack,
  - copy a short post text with the shader names and authors to the clipboard,
  - render a still image to attach: one PNG, or a grid of up to four (see Design decisions #8).

  The pack itself is shared through a link, such as a GitHub gist or release, or any file host. Opening the app with such a link (`<app> --import <url>`) imports it after the same preview.
- **Very small shaders.** Shaders that use only built-in textures could also be shared as a single copyable text block that you paste into the app, which works even inside a long X post. This is worth trying only if packs feel too heavy.
- **Licenses.** Shares carry the license field and the original code comments unchanged. The app shows them, as everywhere else, and never judges them.
- **Design:** the share dialog and import preview are already designed (see Design, resolved Oct 21).

### Other ideas
- Hook it into Omarchy's idle setting so it can act as the real screensaver.
- Duplicate a shader. Import and export are covered by Sharing above.

## Releasing and agent notes

From day one the repo has `scripts/release.sh X.Y.Z` (with `--dry-run`), modelled on OmaEar's: preflight checks, version bump, build, headless tests, signed package and repo, publish, public verification, and a stop at the first failure. An `AGENTS.md` at the repo root tells coding agents to always release with the script (dry run first), to commit, push or release only when Allan asks, and to test headlessly without touching his desktop or running app.
