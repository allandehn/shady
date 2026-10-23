import QtQuick

// Right-click menu: rows with their key on the right in muted text, a divider
// before Move to Trash. The highlighted row uses a soft accent tint.
FocusScope {
    id: menu
    property var items: []   // [{text, key, action}] or {divider: true}
    property int current: 0
    property Item origin: null
    signal picked(string action)
    visible: false
    anchors.fill: parent

    function open(x, y, list) {
        items = list
        current = 0
        box.x = Math.min(x, width - box.width - 8)
        box.y = Math.min(y, height - box.height - 8)
        visible = true
        forceActiveFocus()
    }
    function close() { visible = false }
    function move(d) {
        let i = current
        do { i = (i + d + items.length) % items.length } while (items[i].divider)
        current = i
    }

    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onPressed: menu.close() }
    Rectangle {
        id: box
        width: 230
        height: rows.implicitHeight + 8
        color: ui.bg2
        radius: theme.rounding
        border.width: 1; border.color: Qt.rgba(ui.muted.r, ui.muted.g, ui.muted.b, 0.35)
        Column {
            id: rows
            y: 4; width: parent.width
            Repeater {
                model: menu.items
                Item {
                    required property var modelData
                    required property int index
                    width: rows.width
                    height: modelData.divider ? 9 : 28
                    Rectangle { visible: !!modelData.divider; anchors.centerIn: parent; width: parent.width - 16; height: 1
                                color: Qt.rgba(ui.muted.r, ui.muted.g, ui.muted.b, 0.3) }
                    Rectangle { visible: !modelData.divider && index === menu.current; anchors.fill: parent; anchors.leftMargin: 4; anchors.rightMargin: 4
                                radius: theme.rounding; color: Qt.rgba(ui.accent.r, ui.accent.g, ui.accent.b, 0.16) }
                    Label { visible: !modelData.divider; x: 14; anchors.verticalCenter: parent.verticalCenter; text: modelData.text || ""; font.pixelSize: 12 }
                    Label { visible: !modelData.divider; anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                            text: modelData.key || ""; dim: true; font.pixelSize: 11 }
                    MouseArea { anchors.fill: parent; enabled: !modelData.divider; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; 
                                onEntered: menu.current = index
                                onClicked: { menu.close(); menu.picked(modelData.action) } }
                }
            }
        }
    }
    Keys.onPressed: (e) => {
        if (e.key === Qt.Key_Up) move(-1)
        else if (e.key === Qt.Key_Down) move(1)
        else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Space) { close(); picked(items[current].action) }
        else if (e.key === Qt.Key_Escape || e.key === Qt.Key_Menu) close()
        e.accepted = true
    }
}
