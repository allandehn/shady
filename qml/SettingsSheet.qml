import QtQuick

// Settings (key `,`): where the screensaver runs, and the cache.
FocusScope {
    id: sheet
    visible: false
    anchors.fill: parent
    property int current: 0
    property real cacheMb: 0
    readonly property var options: [
        { value: "all", text: qsTr("All monitors · same shader"), later: false },
        { value: "primary", text: qsTr("Primary only"), later: false },
        { value: "each", text: qsTr("Each its own (later)"), later: true }
    ]
    function open() {
        cacheMb = library.cacheBytes() / 1048576
        current = library.screensaverOn === "primary" ? 1 : 0
        visible = true
        forceActiveFocus()
    }
    function close() { visible = false }
    function pick() {
        if (current < 3) { if (!options[current].later) library.screensaverOn = options[current].value }
        else if (current === 4) library.antiBurnIn = !library.antiBurnIn
        else { library.clearCache(); cacheMb = library.cacheBytes() / 1048576 }
    }

    Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.55) }
    MouseArea { anchors.fill: parent; onClicked: sheet.close() }
    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 380)
        height: col.implicitHeight + hint.height + 36
        color: ui.bg2; radius: theme.rounding
        border.width: 1; border.color: Qt.rgba(ui.muted.r, ui.muted.g, ui.muted.b, 0.3)
        MouseArea { anchors.fill: parent }
        Column {
            id: col
            x: 20; y: 16; width: parent.width - 40
            spacing: 10
            Label { text: qsTr("Settings"); font.bold: true; font.pixelSize: 15 }
            Rectangle { width: parent.width; height: 1; color: Qt.rgba(ui.muted.r, ui.muted.g, ui.muted.b, 0.25) }
            Label { text: qsTr("Screensaver on"); smallCaps: true; dim: true }
            Repeater {
                model: sheet.options
                Rectangle {
                    required property var modelData
                    required property int index
                    width: col.width; height: 26
                    color: "transparent"; radius: theme.rounding
                    border.width: sheet.current === index ? 1 : 0; border.color: ui.accent
                    Row {
                        x: 6; anchors.verticalCenter: parent.verticalCenter; spacing: 10
                        Rectangle {
                            width: 12; height: 12; radius: 6; anchors.verticalCenter: parent.verticalCenter
                            color: "transparent"; border.width: 1; border.color: modelData.later ? ui.muted : ui.fg
                            Rectangle { anchors.centerIn: parent; width: 6; height: 6; radius: 3; color: ui.accent
                                        visible: library.screensaverOn === modelData.value }
                        }
                        Label { text: modelData.text; font.pixelSize: 12; dim: modelData.later }
                    }
                    MouseArea { anchors.fill: parent; onClicked: { sheet.current = index; sheet.pick() } }
                }
            }
            Label {
                readonly property int n: library.monitorCount()
                text: qsTr("%n monitors", "", n) + " · " + qsTr("load ×%1").arg(library.screensaverOn === "primary" ? 1 : n)
                dim: true; font.pixelSize: 11
            }
            Rectangle { width: parent.width; height: 1; color: Qt.rgba(ui.muted.r, ui.muted.g, ui.muted.b, 0.25) }
            Label { text: qsTr("Cache"); smallCaps: true; dim: true }
            Label { text: qsTr("%1 MB · compiled + previews").arg(sheet.cacheMb.toFixed(sheet.cacheMb < 10 ? 1 : 0)); font.pixelSize: 12 }
            Button {
                text: qsTr("Clear cache")
                border.color: sheet.current === 3 ? ui.accent : Qt.rgba(ui.muted.r, ui.muted.g, ui.muted.b, 0.5)
                onClicked: { sheet.current = 3; sheet.pick() }
            }
            Label { text: qsTr("Rebuilt when needed."); dim: true; font.pixelSize: 11 }
            Rectangle { width: parent.width; height: 1; color: Qt.rgba(ui.muted.r, ui.muted.g, ui.muted.b, 0.25) }
            Label { text: qsTr("Burn-in protection"); smallCaps: true; dim: true }
            Rectangle {
                objectName: "antiBurnIn"
                width: col.width; height: 26
                color: "transparent"; radius: theme.rounding
                border.width: sheet.current === 4 ? 1 : 0; border.color: ui.accent
                Row {
                    x: 6; anchors.verticalCenter: parent.verticalCenter; spacing: 10
                    Rectangle {
                        width: 12; height: 12; radius: 2; anchors.verticalCenter: parent.verticalCenter
                        color: "transparent"; border.width: 1; border.color: ui.fg
                        Rectangle { anchors.centerIn: parent; width: 6; height: 6; radius: 1; color: ui.accent; visible: library.antiBurnIn }
                    }
                    Label { text: qsTr("Slow drift and dimming when fullscreen"); font.pixelSize: 12 }
                }
                MouseArea { anchors.fill: parent; onClicked: { sheet.current = 4; sheet.pick() } }
            }
            Item { width: 1; height: 4 }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: hint.height; color: Qt.darker(ui.bg2, 1.08)
            Hint { id: hint; x: 20; width: parent.width - 40; topPadding: 9; bottomPadding: 9
                   keys: [["↑↓", qsTr("move")], ["space", qsTr("pick")], ["esc", qsTr("close")]] } }
    }
    Keys.onPressed: (e) => {
        if (e.key === Qt.Key_Up) current = Math.max(0, current - 1)
        else if (e.key === Qt.Key_Down) current = Math.min(4, current + 1)
        else if (e.key === Qt.Key_Space || e.key === Qt.Key_Return || e.key === Qt.Key_Enter) pick()
        else if (e.key === Qt.Key_Escape || e.key === Qt.Key_Comma) close()
        e.accepted = true
    }
}
