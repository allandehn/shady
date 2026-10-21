import QtQuick

Text {
    property bool smallCaps: false
    property bool dim: false
    color: dim ? ui.muted : ui.fg
    font.family: theme.mono
    font.pixelSize: smallCaps ? 10 : 13
    font.capitalization: smallCaps ? Font.AllUppercase : Font.MixedCase
    font.letterSpacing: smallCaps ? 1.2 : 0
    elide: Text.ElideRight
    textFormat: Text.PlainText
}
