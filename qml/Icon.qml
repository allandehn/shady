import QtQuick

// Standard icon: a Nerd Font MDI glyph in the system Nerd Font (what Omarchy
// uses), coloured by the theme. Without a Nerd Font installed it falls back to
// the plain Unicode symbol, so nothing turns into tofu.
Text {
    property string glyph: ""     // MDI codepoint, e.g. "\u{F04CE}"
    property string fallback: ""  // Unicode stand-in
    property int size: 12
    readonly property bool nerd: theme.iconFont !== "" && glyph !== ""
    text: nerd ? glyph : fallback
    font.family: nerd ? theme.iconFont : theme.mono
    font.pixelSize: nerd ? size + 2 : size // MDI glyphs sit smaller in the em box
    color: ui.fg
    renderType: Text.NativeRendering
}
