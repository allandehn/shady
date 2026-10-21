import QtQuick

// `?` key sheet: every key the app has, grouped. Esc or ? closes.
FocusScope {
    id: sheet
    visible: false
    anchors.fill: parent
    function open() { visible = true; forceActiveFocus() }
    function close() { visible = false }
    readonly property var groups: [
        { title: qsTr("List"), keys: [
            ["Enter", qsTr("open")], ["N", qsTr("new")], ["S", qsTr("star / unstar")], ["H", qsTr("hide / unhide")],
            ["Ctrl+E", qsTr("edit")], ["Shift+F10", qsTr("menu (or Menu key)")], ["Del", qsTr("move to Trash")],
            ["Shift+Del", qsTr("delete permanently")], ["Ctrl+Z", qsTr("undo")], ["Tab", qsTr("next view")],
            [",", qsTr("settings")], ["?", qsTr("all keys")], ["Esc", qsTr("close")]] },
        { title: qsTr("Trash"), keys: [
            ["Enter", qsTr("restore")], ["Del", qsTr("delete forever")], ["Tab", qsTr("footer")]] },
        { title: qsTr("Viewer"), keys: [
            ["↑↓", qsTr("quality")], ["←→", qsTr("fps")], ["F", qsTr("fullscreen")], ["Esc", qsTr("close")]] },
        { title: qsTr("Editor"), keys: [
            ["Ctrl+↵", qsTr("compile")], ["Ctrl+S", qsTr("save")], ["Tab", qsTr("next field")], ["Esc", qsTr("cancel")]] }
    ]

    Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.55) }
    MouseArea { anchors.fill: parent; onClicked: sheet.close() }
    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 560)
        height: Math.min(parent.height - 32, flow.implicitHeight + 64)
        color: ui.bg2; radius: theme.rounding
        border.width: 1; border.color: Qt.rgba(ui.muted.r, ui.muted.g, ui.muted.b, 0.3)
        clip: true
        Label { x: 20; y: 16; text: qsTr("All keys"); font.bold: true; font.pixelSize: 15 }
        Label { anchors { right: parent.right; rightMargin: 20; top: parent.top; topMargin: 18 }
                text: "Esc " + qsTr("close"); dim: true; font.pixelSize: 11 }
        Flickable {
            id: scroller
            x: 20; y: 48; width: parent.width - 40; height: parent.height - 56
            contentHeight: flow.implicitHeight + 8; clip: true
            boundsBehavior: Flickable.StopAtBounds
        Flow {
            id: flow
            width: scroller.width
            spacing: 22
            Repeater {
                model: sheet.groups
                Column {
                    required property var modelData
                    // Two columns when there's room, one when the window is narrow.
                    width: flow.width >= 460 ? (flow.width - 22) / 2 : flow.width
                    spacing: 5
                    Label { text: modelData.title; smallCaps: true; dim: true }
                    Repeater {
                        model: modelData.keys
                        Row {
                            id: keyRow
                            required property var modelData
                            width: parent.width
                            spacing: 8
                            Label { id: keyCap; width: 84; text: keyRow.modelData[0]; font.pixelSize: 11; font.bold: true }
                            Label { width: keyRow.width - keyCap.width - keyRow.spacing; text: keyRow.modelData[1]; font.pixelSize: 11; dim: true }
                        }
                    }
                }
            }
        }
        }
    }
    Keys.onPressed: (e) => {
        if (e.key === Qt.Key_Escape || e.key === Qt.Key_Question) close()
        else if (e.key === Qt.Key_Down || e.key === Qt.Key_Up) {
            const max = Math.max(0, scroller.contentHeight - scroller.height)
            scroller.contentY = Math.max(0, Math.min(max, scroller.contentY + (e.key === Qt.Key_Down ? 40 : -40)))
        }
        e.accepted = true
    }
}
