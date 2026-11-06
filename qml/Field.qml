import QtQuick

// Labeled single-line field (small-caps muted label above).
FocusScope {
    id: f
    property alias label: lab.text
    property alias text: input.text
    property alias input: input
    implicitHeight: lab.height + 6 + 30
    Label { id: lab; smallCaps: true; dim: true }
    Rectangle {
        y: lab.height + 6
        width: parent.width; height: 30
        color: ui.bg2; radius: theme.rButton
        border.width: 1; border.color: input.activeFocus ? ui.accent : "transparent"
        TextInput {
            id: input
            focus: true
            anchors { fill: parent; leftMargin: 9; rightMargin: 9 }
            verticalAlignment: TextInput.AlignVCenter
            color: ui.fg; selectionColor: Qt.rgba(ui.accent.r, ui.accent.g, ui.accent.b, 0.35)
            font.family: theme.mono; font.pixelSize: 12
            clip: true
            selectByMouse: true
            // I-beam over every text field (all single-line inputs use Field).
            HoverHandler { cursorShape: Qt.IBeamCursor }
        }
    }
}
