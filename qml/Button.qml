import QtQuick

// Text-sized button. `primary` is the one accent-filled button per view.
// Focus is always a 1 px accent outline.
Rectangle {
    id: b
    property alias text: label.text
    property string keyHint: ""
    property bool primary: false
    property bool enabled: true
    signal clicked()
    implicitWidth: row.implicitWidth + 28
    implicitHeight: 30
    radius: theme.rounding
    color: primary ? ui.accent : "transparent"
    border.width: 1
    border.color: activeFocus ? ui.accent : primary ? ui.accent : Qt.rgba(ui.muted.r, ui.muted.g, ui.muted.b, 0.5)
    opacity: enabled ? 1 : 0.4
    activeFocusOnTab: false
    Keys.onReturnPressed: if (enabled) clicked()
    Keys.onEnterPressed: if (enabled) clicked()
    Keys.onSpacePressed: if (enabled) clicked()
    Rectangle {
        anchors.fill: parent; anchors.margins: -3
        visible: b.activeFocus && b.primary
        color: "transparent"; radius: theme.rounding
        border.width: 1; border.color: ui.accent
    }
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 10
        Label { id: label; font.pixelSize: 12; color: b.primary ? ui.bg : ui.fg; font.bold: b.primary }
        Label { text: b.keyHint; visible: text; font.pixelSize: 10; dim: true; anchors.verticalCenter: parent.verticalCenter }
    }
    MouseArea { anchors.fill: parent; cursorShape: b.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor; onClicked: if (b.enabled) b.clicked() }
}
