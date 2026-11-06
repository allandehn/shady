import QtQuick

// Hover tooltip: shows after ~400 ms, theme colours, chip corners (min(R, 4)).
// Put it inside the hovered item and set `hovered`.
Rectangle {
    id: tip
    objectName: "tip"
    property alias text: label.text
    property bool hovered: false
    property bool shown: false
    onHoveredChanged: { if (hovered) delay.restart(); else { delay.stop(); shown = false } }
    Timer { id: delay; interval: 400; onTriggered: tip.shown = true }
    visible: shown
    z: 100
    radius: theme.rButton
    width: label.implicitWidth + 14; height: label.implicitHeight + 8
    color: ui.bg
    border.width: 1; border.color: Qt.rgba(ui.accent.r, ui.accent.g, ui.accent.b, 0.6)
    Label { id: label; anchors.centerIn: parent; font.pixelSize: 11 }
}
