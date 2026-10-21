import QtQuick

// 1. Shader list panel: header with count, All · Starred · Hidden filter,
// a 2-column grid of cards, a status line and at most two hint rows.
FocusScope {
    id: panel
    signal open(int row)
    signal close()
    property var resume: ({})
    property string note: ""
    readonly property int currentRow: grid.currentIndex

    function focusRow(r) { grid.currentIndex = Math.max(0, Math.min(r, grid.count - 1)) }

    Rectangle { anchors.fill: parent; color: ui.bg }

    Column {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
        spacing: 12
        Item {
            width: parent.width; height: 24
            Grid {
                id: icon
                anchors.verticalCenter: parent.verticalCenter
                columns: 2; spacing: 2
                Repeater { model: 4; Rectangle { width: 6; height: 6; color: ui.accent } }
            }
            Label {
                anchors { left: icon.right; leftMargin: 10; verticalCenter: parent.verticalCenter }
                text: qsTr("Shaders"); font.pixelSize: 17; font.bold: true
            }
            Label {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                smallCaps: true; dim: true
                text: {
                    const n = library.count
                    let s = library.filter === 1 ? qsTr("%n starred", "", n)
                          : library.filter === 2 ? qsTr("%n hidden", "", n)
                          : qsTr("%n shaders", "", n)
                    if (library.preparing > 0) s += " · " + qsTr("preparing %1").arg(library.preparing)
                    return s
                }
            }
        }
        Item {
            width: parent.width; height: 20
            Row {
                spacing: 16
                Repeater {
                    model: [qsTr("All"), qsTr("Starred"), qsTr("Hidden")]
                    Column {
                        spacing: 3
                        required property int index
                        required property string modelData
                        Label {
                            text: modelData; font.pixelSize: 11
                            dim: library.filter !== index
                        }
                        Rectangle {
                            width: parent.width; height: 1; color: ui.accent
                            visible: library.filter === index
                        }
                    }
                }
            }
            Label { anchors.right: parent.right; text: "Tab"; dim: true; font.pixelSize: 10 }
        }
    }

    GridView {
        id: grid
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: footer.top
                  leftMargin: 12; rightMargin: 12; topMargin: 8 }
        clip: true
        focus: true
        model: library
        cellWidth: Math.floor(width / 2)
        cellHeight: Math.round((cellWidth - 10) * 0.46) + 60
        keyNavigationEnabled: true
        highlightFollowsCurrentItem: false
        boundsBehavior: Flickable.StopAtBounds
        delegate: ShaderCard {
            width: grid.cellWidth; height: grid.cellHeight
            current: GridView.isCurrentItem && panel.activeFocus
            live: current && !theme.software
            resume: panel.resume
            onPicked: { grid.currentIndex = index; panel.forceActiveFocus() }
            onActivated: panel.open(index)
        }

        Keys.onPressed: (e) => {
            const r = grid.currentIndex
            if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) { if (r >= 0) panel.open(r) }
            else if (e.key === Qt.Key_S && !(e.modifiers & Qt.ControlModifier)) { if (r >= 0) grid.currentIndex = library.toggleStar(r) }
            else if (e.key === Qt.Key_H) { if (r >= 0) grid.currentIndex = library.toggleHidden(r) }
            else if (e.key === Qt.Key_Tab) { library.filter = (library.filter + 1) % 3; grid.currentIndex = 0 }
            else if (e.key === Qt.Key_Backtab) { library.filter = (library.filter + 2) % 3; grid.currentIndex = 0 }
            else if (e.key === Qt.Key_Escape) panel.close()
            else return
            e.accepted = true
        }
    }

    Label {
        anchors.centerIn: grid
        visible: grid.count === 0
        dim: true
        text: library.filter === 1 ? qsTr("No starred shaders")
            : library.filter === 2 ? qsTr("No hidden shaders") : qsTr("No shaders yet")
    }

    // Off-screen still-frame maker: renders one shader at a time to a small
    // PNG in the cache, so cards show still frames without running shaders.
    Item {
        id: maker
        x: panel.width + 50; width: 320; height: 148
        property int row: -1
        property string slug: ""
        ShaderView {
            id: makerView
            anchors.fill: parent
            running: false
            time: 2.0
            quality: 1.0
        }
        Timer { id: makerWait; interval: 250; onTriggered: maker.grab() }
        function kick() {
            if (slug !== "") return
            const r = library.nextNeedingStill()
            if (r < 0) return
            row = r
            const m = library.get(r)
            slug = m.slug
            makerView.source = m.shaderUrl
            makerWait.restart()
        }
        function grab() {
            const path = library.stillTarget(library.rowOf(slug))
            makerView.grabToImage((res) => {
                const r = library.rowOf(maker.slug)
                if (res.saveToFile(path) && r >= 0) library.stillSaved(r)
                maker.slug = ""
                makerView.source = ""
                Qt.callLater(maker.kick)
            }, Qt.size(320, 148))
        }
        Connections { target: library; function onCountChanged() { Qt.callLater(maker.kick) } }
        Component.onCompleted: Qt.callLater(kick)
    }

    Column {
        id: footer
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        Label {
            x: 16; width: parent.width - 32; height: text ? 26 : 0
            verticalAlignment: Text.AlignVCenter
            dim: true; font.pixelSize: 11
            text: library.status || panel.note
        }
        Rectangle { width: parent.width; height: 1; color: ui.bg2 }
        Item {
            width: parent.width; height: hints.height + 24
            Column {
                id: hints
                x: 16; y: 12; width: parent.width - 32; spacing: 6
                Hint { keys: library.filter === 2
                        ? [["Enter", qsTr("open")], ["H", qsTr("unhide")], ["Tab", qsTr("view")]]
                        : [["Enter", qsTr("open")], ["S", qsTr("star")], ["H", qsTr("hide")], ["Tab", qsTr("view")]] }
                Hint { keys: [["←→↑↓", qsTr("move")], ["Esc", qsTr("close")]] }
            }
        }
    }
}
