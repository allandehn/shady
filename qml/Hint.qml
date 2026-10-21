import QtQuick

// One hint row: bold keys, muted actions. Wraps instead of cutting off.
Flow {
    property var keys: []
    width: parent ? parent.width : 0
    spacing: 14
    Repeater {
        model: keys
        Row {
            required property var modelData
            spacing: 5
            Label { text: modelData[0]; font.pixelSize: 11; font.bold: true }
            Label { text: modelData[1]; font.pixelSize: 11; dim: true }
        }
    }
}
