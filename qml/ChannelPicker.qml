import QtQuick

// Popover for one iChannel slot: built-in textures, "Image file…", Empty,
// then Filter (linear/nearest) and Wrap (repeat/clamp) as two-option toggles.
// ↑↓ move · ←→ switch · Enter pick · Esc close
FocusScope {
    id: pk
    property int slot: -1
    property var value: ({ source: "", filter: "linear", wrap: "repeat" })
    property int current: 0
    property bool typing: false
    signal picked(int slot, var value)
    visible: false
    anchors.fill: parent

    readonly property var sources: library.builtinChannels().concat([{ source: "__file", label: qsTr("Image file…") },
                                                                      { source: "", label: qsTr("Empty") }])
    readonly property int rows: sources.length + 2 // + Filter, Wrap

    function open(slotIndex, v, x, y) {
        slot = slotIndex
        value = Object.assign({ source: "", filter: "linear", wrap: "repeat" }, v)
        current = value.source ? Math.max(0, sources.findIndex(s => s.source === value.source)) : 0
        typing = false
        box.x = Math.max(8, Math.min(x, width - box.width - 8))
        box.y = Math.max(8, Math.min(y, height - box.height - 8))
        visible = true
        forceActiveFocus()
    }
    function commit() { picked(slot, value); visible = false }
    function choose(i) {
        const s = sources[i]
        if (s.source === "__file") { typing = true; path.text = ""; path.input.forceActiveFocus(); return }
        value = Object.assign({}, value, { source: s.source })
        commit()
    }

    MouseArea { anchors.fill: parent; onClicked: pk.visible = false }
    Rectangle {
        id: box
        width: 230
        height: col.implicitHeight + 16
        color: ui.bg2; radius: theme.rounding
        border.width: 1; border.color: Qt.rgba(ui.muted.r, ui.muted.g, ui.muted.b, 0.35)
        MouseArea { anchors.fill: parent }
        Column {
            id: col
            x: 0; y: 8; width: parent.width
            Label { x: 14; text: "iChannel" + pk.slot; smallCaps: true; dim: true; bottomPadding: 6 }
            Repeater {
                model: pk.sources
                Item {
                    required property var modelData
                    required property int index
                    width: col.width; height: 26
                    Rectangle { anchors.fill: parent; anchors.leftMargin: 4; anchors.rightMargin: 4; radius: theme.rounding
                                visible: pk.current === index; color: Qt.rgba(ui.accent.r, ui.accent.g, ui.accent.b, 0.16) }
                    Label { x: 14; anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 12; text: modelData.label
                            font.bold: pk.value.source === modelData.source }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { pk.current = index; pk.choose(index) } }
                }
            }
            Field {
                id: path
                visible: pk.typing
                x: 10; width: col.width - 20
                label: qsTr("Path to an image")
                input.onAccepted: {
                    let p = text.trim()
                    if (p.startsWith("~/")) p = library.homePath() + p.slice(1)
                    if (p) { pk.value = Object.assign({}, pk.value, { source: p }); pk.commit() }
                }
                Keys.onEscapePressed: { pk.typing = false; pk.forceActiveFocus() }
            }
            Rectangle { x: 10; width: col.width - 20; height: 1; color: Qt.rgba(ui.muted.r, ui.muted.g, ui.muted.b, 0.3) }
            Repeater {
                model: [{ key: "filter", label: qsTr("Filter"), a: "linear", b: "nearest" },
                        { key: "wrap", label: qsTr("Wrap"), a: "repeat", b: "clamp" }]
                Item {
                    required property var modelData
                    required property int index
                    readonly property int row: pk.sources.length + index
                    width: col.width; height: 26
                    Rectangle { anchors.fill: parent; anchors.leftMargin: 4; anchors.rightMargin: 4; radius: theme.rounding
                                visible: pk.current === row; color: Qt.rgba(ui.accent.r, ui.accent.g, ui.accent.b, 0.16) }
                    Label { x: 14; anchors.verticalCenter: parent.verticalCenter; text: modelData.label; smallCaps: true; dim: true }
                    Row {
                        x: 90; anchors.verticalCenter: parent.verticalCenter; spacing: 14
                        Label { text: modelData.a; font.pixelSize: 12; font.bold: pk.value[modelData.key] === modelData.a; dim: pk.value[modelData.key] !== modelData.a
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: pk.setOpt(modelData.key, modelData.a) } }
                        Label { text: modelData.b; font.pixelSize: 12; font.bold: pk.value[modelData.key] === modelData.b; dim: pk.value[modelData.key] !== modelData.b
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: pk.setOpt(modelData.key, modelData.b) } }
                    }
                }
            }
            Hint { x: 14; width: col.width - 28; topPadding: 6; keys: [["←→", qsTr("switch")], ["enter", qsTr("pick")]] }
        }
    }
    function setOpt(k, v) { const n = Object.assign({}, value); n[k] = v; value = n; picked(slot, value) }
    function toggleRow(r) {
        const k = r === sources.length ? "filter" : "wrap"
        const opts = k === "filter" ? ["linear", "nearest"] : ["repeat", "clamp"]
        setOpt(k, value[k] === opts[0] ? opts[1] : opts[0])
    }
    Keys.onPressed: (e) => {
        if (e.key === Qt.Key_Up) current = Math.max(0, current - 1)
        else if (e.key === Qt.Key_Down) current = Math.min(rows - 1, current + 1)
        else if ((e.key === Qt.Key_Left || e.key === Qt.Key_Right) && current >= sources.length) toggleRow(current)
        else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Space) {
            if (current < sources.length) choose(current); else toggleRow(current)
        }
        else if (e.key === Qt.Key_Escape) visible = false
        e.accepted = true
    }
}
