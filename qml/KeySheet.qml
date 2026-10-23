import QtQuick

// `?` key sheet: every key the app has, grouped. Esc or ? closes.
FocusScope {
    id: sheet
    visible: false
    anchors.fill: parent
    // open(from): from is the section id ("browse", "viewer", "editor",
    // "organize", "trash"); that group gets a brief accent highlight that
    // holds 0.6 s then eases out over 1.6 s. Layout never moves.
    property string from: ""
    property real glow: 0
    // Focus goes back to whatever had it (else `fallback`) however the sheet
    // closes. The sheet must also drop its own `focus`, or the parent
    // FocusScope (viewer, editor) keeps handing focus to this hidden item.
    property Item fallback: null
    property Item prev: null
    onVisibleChanged: if (!visible) {
        focus = false
        const p = prev && prev.visible ? prev : fallback
        prev = null
        if (p) p.forceActiveFocus()
    }
    function open(src) {
        if (!visible) prev = Window.activeFocusItem
        from = src || ""; visible = true; forceActiveFocus()
        glowAnim.stop(); glow = from && theme.animations ? 1 : 0
        if (glow > 0) glowAnim.start()
    }
    SequentialAnimation {
        id: glowAnim
        PauseAnimation { duration: 600 }
        NumberAnimation { target: sheet; property: "glow"; to: 0; duration: 1600; easing.type: Easing.OutCubic }
    }
    function close() { visible = false }
    // Two balanced columns, read top-left first (F-pattern), most-used keys first.
    readonly property var columns: [[
        { id: "browse", title: qsTr("Browse"), keys: [
            ["←→↑↓", qsTr("navigate")], ["enter", qsTr("run")], ["tab", qsTr("next view")], ["esc", qsTr("close")], ["?", qsTr("this page")]] },
        { id: "viewer", title: qsTr("Viewer"), keys: [
            ["↑↓", qsTr("quality")], ["←→", qsTr("fps")], ["a", qsTr("auto-adapt")], ["f", qsTr("fullscreen")], ["esc", qsTr("close")]] },
        { id: "editor", title: qsTr("Editor"), keys: [
            ["ctrl+↵", qsTr("compile")], ["ctrl+s", qsTr("save")], ["ctrl+h", qsTr("burn-in heatmap")], ["tab", qsTr("next field")], ["esc", qsTr("cancel")]] },
        { id: "mouse", title: qsTr("Mouse"), keys: [
            [qsTr("click"), qsTr("run")], [qsTr("right-click"), qsTr("menu")], [qsTr("back button"), qsTr("same as esc")]] }
    ], [
        { id: "organize", title: qsTr("Organize"), keys: [
            ["space", qsTr("select")], ["shift+←→↑↓", qsTr("extend selection")], ["ctrl+a", qsTr("select all")],
            ["s", qsTr("star / unstar")], ["h", qsTr("hide / unhide")], ["n", qsTr("new")], ["ctrl+e", qsTr("edit")],
            ["del", qsTr("move to Trash")], ["shift+del", qsTr("delete permanently")], ["ctrl+z", qsTr("undo")],
            ["shift+f10", qsTr("menu (or Menu key)")], [",", qsTr("settings")]] },
        { id: "trash", title: qsTr("Trash"), keys: [
            ["enter", qsTr("restore")], ["del", qsTr("delete forever")], ["tab", qsTr("empty trash")]] }
    ]]
    readonly property var groups: columns[0].concat(columns[1])

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
                text: "esc " + qsTr("close"); dim: true; font.pixelSize: 11 }
        Flickable {
            id: scroller
            x: 12; y: 40; width: parent.width - 24; height: parent.height - 48  // 8 px room for the highlight bleed
            contentHeight: flow.implicitHeight + 24; clip: true
            boundsBehavior: Flickable.StopAtBounds
        // Two columns side by side; stacked into one when the window is narrow.
        Grid {
            id: flow
            x: 8; y: 8
            width: scroller.width - 16
            readonly property bool two: width >= 460
            columns: two ? 2 : 1
            columnSpacing: 22; rowSpacing: 18
            Repeater {
                model: sheet.columns
                Column {
                    required property var modelData
                    width: flow.two ? (flow.width - 22) / 2 : flow.width
                    spacing: 18
                    Repeater {
                        model: modelData
                        Item {
                            id: grp
                            required property var modelData
                            readonly property real g: modelData.id === sheet.from ? sheet.glow : 0
                            width: parent.width
                            height: grpCol.implicitHeight
                            // Tint hugs the content: 8 px past the longest line on every side,
                            // plus a 2 px accent bar on the left. Both fade with the header.
                            Rectangle {
                                x: -8; y: -8
                                width: grpCol.childrenRect.width + 16; height: grpCol.implicitHeight + 16
                                color: Qt.rgba(ui.accent.r, ui.accent.g, ui.accent.b, 0.06 * grp.g)
                                visible: grp.g > 0
                                Rectangle { width: 2; height: parent.height; color: ui.accent; opacity: grp.g }
                            }
                        Column {
                            id: grpCol
                            width: parent.width
                            spacing: 5
                            Label { text: grp.modelData.title; smallCaps: true; font.bold: grp.g >= 1
                                    color: Qt.rgba(ui.muted.r + (ui.accent.r - ui.muted.r) * grp.g,
                                                   ui.muted.g + (ui.accent.g - ui.muted.g) * grp.g,
                                                   ui.muted.b + (ui.accent.b - ui.muted.b) * grp.g, 1) }
                            Repeater {
                                model: grp.modelData.keys
                                Row {
                                    id: keyRow
                                    required property var modelData
                                    readonly property real avail: grpCol.width
                                    spacing: 8
                                    Label { id: keyCap; width: 88; text: String(keyRow.modelData[0]).replace(/\+(?=.)/g, " "); font.pixelSize: 11; font.bold: true }
                                    Label { width: Math.min(implicitWidth, keyRow.avail - keyCap.width - keyRow.spacing); text: keyRow.modelData[1]; font.pixelSize: 11; dim: true; elide: Text.ElideRight }
                                }
                            }
                        }
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
