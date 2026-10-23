import QtQuick

// One hint row: bold key, muted " action", hints joined by a muted " · ". Wraps instead of cutting off,
// or with `singleLine` keeps one line and drops the trailing keys that don't fit.
Flow {
    id: hint
    property var keys: []
    property bool singleLine: false
    width: parent ? parent.width : 0
    spacing: 0
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
            id: one
            // "key action"; " · " before every hint that doesn't start a line
            Label { text: " · "; font.pixelSize: 11; dim: true; visible: one.index > 0 && one.x > 0 }
            Label { text: modelData[0]; font.pixelSize: 11; font.bold: true }
            Label { text: " " + modelData[1]; font.pixelSize: 11; dim: true }
        }
    }
}
