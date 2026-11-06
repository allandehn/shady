import QtQuick

// Text-sized button. `primary` is the one accent-filled button per view.
// `tone` colours a dialog action like Omarchy's ConfirmDialog: "urgent" for
// destructive actions, "accent" for the rest (tinted text, 22% fill when focused).
// Focus is always a 1 px outline. Corners: min(R, 4).
Rectangle {
    id: b
    property alias text: label.text
    property string keyHint: ""
    property bool primary: false
    property string tone: ""
    property bool enabled: true
    readonly property color toneColor: tone === "urgent" ? ui.urgent : ui.accent
    signal clicked()
    implicitWidth: row.implicitWidth + 28
    implicitHeight: 30
    radius: theme.rButton
    color: primary ? ui.accent
         : tone && activeFocus ? Qt.rgba(toneColor.r, toneColor.g, toneColor.b, 0.22) : "transparent"
    border.width: 1
    border.color: tone ? (activeFocus ? toneColor : Qt.rgba(toneColor.r, toneColor.g, toneColor.b, 0.5))
                : activeFocus ? ui.accent : primary ? ui.accent : Qt.rgba(ui.muted.r, ui.muted.g, ui.muted.b, 0.5)
    opacity: enabled ? 1 : 0.4
    activeFocusOnTab: false
    Keys.onReturnPressed: if (enabled) clicked()
    Keys.onEnterPressed: if (enabled) clicked()
    Keys.onSpacePressed: if (enabled) clicked()
    Rectangle {
        anchors.fill: parent; anchors.margins: -3
        visible: b.activeFocus && b.primary
        color: "transparent"; radius: theme.rButton + (theme.rButton > 0 ? 3 : 0)
        border.width: 1; border.color: ui.accent
    }
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 10
        Label { id: label; font.pixelSize: 12; color: b.primary ? ui.bg : b.tone ? b.toneColor : ui.fg; font.bold: b.primary || b.tone !== "" }
        Label { text: b.keyHint; visible: text; font.pixelSize: 10; dim: true; anchors.verticalCenter: parent.verticalCenter }
    }
    MouseArea { anchors.fill: parent; cursorShape: b.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor; onClicked: if (b.enabled) b.clicked() }
}
