import QtQuick

// One hint row: bold keys, muted actions. Wraps instead of cutting off,
// or with `singleLine` keeps one line and drops the trailing keys that don't fit.
Flow {
    id: hint
    property var keys: []
    property bool singleLine: false
    width: parent ? parent.width : 0
    spacing: 14
    readonly property int fitCount: {
        if (!singleLine) return keys.length
        let x = 0, n = 0
        for (let i = 0; i < rep.count; ++i) {
            const it = rep.itemAt(i)
            if (!it) break
            x += (i ? spacing : 0) + it.implicitWidth
            if (x > width) break
            ++n
        }
        return n
    }
    Repeater {
        id: rep
        model: hint.keys
        Row {
            required property var modelData
            required property int index
            visible: index < hint.fitCount
            spacing: 5
            Label { text: modelData[0]; font.pixelSize: 11; font.bold: true }
            Label { text: modelData[1]; font.pixelSize: 11; dim: true }
        }
    }
}
