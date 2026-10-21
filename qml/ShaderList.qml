import QtQuick

// 1. Shader list panel: header with count, All · Starred · Hidden · Trash,
// a 2-column grid of cards (+ New shader last in All), a status note, and at
// most two hint rows. Menu, confirmation, key sheet and Settings sit on top.
FocusScope {
    id: panel
    signal open(int row)
    signal edit(string slug)
    signal close()
    property var resume: ({})
    property string swNote: ""
    property string note: ""
    readonly property int currentRow: grid.currentIndex
    readonly property int columns: grid.columns
    readonly property bool inTrash: library.filter === 3
    readonly property bool overlayOpen: menu.visible || confirm.visible || keys.visible || settings.visible

    // Multi-select: slug -> true. The anchor is the first card picked.
    property var sel: ({})
    property int selCount: 0
    property string anchor: ""
    property var base: ({})
    readonly property bool selecting: selCount > 0
    function selected() { return Object.keys(sel).filter(k => sel[k]) }
    function setSel(m) { sel = m; selCount = Object.keys(m).filter(k => m[k]).length; if (!selCount) anchor = "" }
    function clearSel() { setSel({}); base = {} }
    function toggle(r) {
        if (!isShader(r)) return
        const slug = library.get(r).slug
        const m = Object.assign({}, sel)
        m[slug] = !m[slug]
        if (!m[slug]) delete m[slug]
        if (m[slug] && !anchor) anchor = slug
        base = Object.assign({}, m)
        setSel(m)
    }
    function extend(to) {
        // Shift+arrows: from the anchor to the new card, in reading order.
        if (!isShader(to)) return
        const cur = grid.currentIndex
        if (!anchor && isShader(cur)) { anchor = library.get(cur).slug; base = Object.assign({}, sel) }
        const a = library.rowOf(anchor)
        const m = Object.assign({}, base)
        for (let i = Math.min(a, to); i <= Math.max(a, to); ++i) m[library.get(i).slug] = true
        setSel(m)
        grid.currentIndex = to
    }
    function selectAll() {
        const m = {}
        for (const s of library.allSlugs()) m[s] = true
        anchor = library.count ? library.get(0).slug : ""
        base = Object.assign({}, m)
        setSel(m)
    }
    function trashSelected() { const n = library.trashMany(selected()); clearSel(); focusRow(grid.currentIndex) }
    function hideSelected() { library.hideMany(selected(), library.filter !== 2); clearSel(); focusRow(grid.currentIndex) }
    function restoreSelected() { library.restoreMany(selected()); clearSel(); focusRow(0) }
    function confirmDeleteSelected() {
        const list = selected()
        confirm.title = qsTr("Delete forever?")
        confirm.line = qsTr("Permanently delete %n shaders?", "", list.length)
        confirm.run = () => { library.deleteMany(list); clearSel(); focusRow(0) }
        confirm.open()
    }
    Connections { target: library; function onFilterChanged() { panel.clearSel() } }

    function focusRow(r) { grid.currentIndex = Math.max(0, Math.min(r, grid.count - 1)); grid.forceActiveFocus() }
    function focusSlug(s) { const r = library.rowOf(s); if (r >= 0) focusRow(r) }
    function isShader(r) { return r >= 0 && r < library.count }
    function cycle(d) { clearSel(); library.filter = (library.filter + d + 4) % 4; focusRow(0) }
    function confirmDelete(r) {
        const m = library.get(r)
        confirm.title = qsTr("Delete forever?")
        confirm.line = qsTr("Permanently delete %1?").arg(m.name)
        confirm.run = () => { library.deleteForever(library.rowOf(m.slug)); focusRow(r - 1) }
        confirm.open()
    }
    function confirmEmpty() {
        if (library.trashCount === 0) return
        confirm.title = qsTr("Delete forever?")
        confirm.line = qsTr("Permanently delete %n shaders?", "", library.trashCount)
        confirm.run = () => { library.emptyTrash(); focusRow(0) }
        confirm.open()
    }
    function openMenu(r, x, y) {
        if (!isShader(r)) return
        const m = library.get(r)
        grid.currentIndex = r
        menu.row = r
        if (m.trashed)
            menu.open(x, y, [{ text: qsTr("Restore"), key: "Enter", action: "restore" }, { divider: true },
                             { text: qsTr("Delete forever"), key: "Del", action: "forever" }])
        else
            menu.open(x, y, [{ text: qsTr("Open"), key: "Enter", action: "open" },
                             { text: qsTr("Edit"), key: "Ctrl+E", action: "edit" },
                             { text: m.starred ? qsTr("Unstar") : qsTr("Star"), key: "S", action: "star" },
                             { text: m.hidden ? qsTr("Unhide") : qsTr("Hide"), key: "H", action: "hide" },
                             { text: panel.sel[m.slug] ? qsTr("Deselect") : qsTr("Select"), key: "Space", action: "select" },
                             { divider: true },
                             { text: qsTr("Move to Trash"), key: "Del", action: "trash" }])
    }
    function act(action, r) {
        if (!isShader(r)) return
        const slug = library.get(r).slug
        if (action === "open") panel.open(r)
        else if (action === "edit") panel.edit(slug)
        else if (action === "star") focusRow(library.toggleStar(r))
        else if (action === "hide") focusRow(library.toggleHidden(r))
        else if (action === "trash") focusRow(library.moveToTrash(r))
        else if (action === "restore") focusRow(library.restore(r))
        else if (action === "forever") confirmDelete(r)
        else if (action === "select") toggle(r)
    }
    function showNote(t) { note = t; noteTimer.restart() }

    Connections { target: library; function onNoted(t) { panel.showNote(t) } }
    Timer { id: noteTimer; interval: 4000; onTriggered: panel.note = "" }

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
                    if (panel.selCount > 0) return qsTr("%1 of %2 selected").arg(panel.selCount).arg(n)
                    let s = library.filter === 1 ? qsTr("%n starred", "", n)
                          : library.filter === 2 ? qsTr("%n hidden", "", n)
                          : library.filter === 3 ? qsTr("%n in trash", "", n)
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
                    model: [qsTr("All"), qsTr("Starred"), qsTr("Hidden"), qsTr("Trash")]
                    Column {
                        spacing: 3
                        required property int index
                        required property string modelData
                        Label {
                            text: modelData; font.pixelSize: 11
                            dim: library.filter !== index
                            font.bold: library.filter === index
                        }
                        Rectangle { width: parent.width; height: 2; color: ui.accent; visible: library.filter === index }
                        MouseArea { width: parent.width; height: 20; onClicked: { library.filter = index; panel.focusRow(0) } }
                    }
                }
            }
            Label { anchors.right: parent.right; text: "Tab"; dim: true; font.pixelSize: 10 }
        }
    }

    Label {
        id: emptyAll
        anchors { left: parent.left; top: header.bottom; leftMargin: 16; topMargin: 10 }
        visible: library.filter === 0 && library.count === 0
        height: visible ? implicitHeight + 6 : 0
        dim: true
        text: qsTr("No shaders yet")
    }

    GridView {
        id: grid
        anchors { left: parent.left; right: parent.right; top: emptyAll.visible ? emptyAll.bottom : header.bottom
                  bottom: footer.top; leftMargin: 12; rightMargin: 12; topMargin: 8 }
        clip: true
        focus: true
        model: library
        // Square previews: as many ~200 px columns as fit, never fewer than two.
        readonly property int columns: Math.max(2, Math.floor((width + 10) / 200))
        cellWidth: Math.floor(width / columns)
        cellHeight: (cellWidth - 10) + 58
        keyNavigationEnabled: true
        highlightFollowsCurrentItem: false
        boundsBehavior: Flickable.StopAtBounds
        delegate: ShaderCard {
            width: grid.cellWidth; height: grid.cellHeight
            current: GridView.isCurrentItem && grid.activeFocus
            live: current && !theme.software && !library.slowMachine
            selected: !!panel.sel[slug]
            selecting: panel.selecting
            onToggled: panel.toggle(index)
            resume: panel.resume
            onPicked: { grid.currentIndex = index; grid.forceActiveFocus() }
            onActivated: isNew ? panel.edit("") : panel.inTrash ? panel.act("restore", index) : panel.open(index)
            onMenuRequested: (mx, my) => { const p = mapToItem(panel, mx, my); panel.openMenu(index, p.x, p.y) }
            onTrashRequested: panel.act("trash", index)
        }

        Keys.onPressed: (e) => {
            const r = grid.currentIndex
            const ctrl = e.modifiers & Qt.ControlModifier, shift = e.modifiers & Qt.ShiftModifier
            const isNewCard = r === library.count && library.filter === 0
            if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
                if (panel.selecting && panel.inTrash) panel.restoreSelected()
                else if (isNewCard) panel.edit("")
                else panel.act(panel.inTrash ? "restore" : "open", r)
            }
            else if (e.key === Qt.Key_Space) panel.toggle(r)
            else if (shift && (e.key === Qt.Key_Left || e.key === Qt.Key_Right || e.key === Qt.Key_Up || e.key === Qt.Key_Down)) {
                const step = e.key === Qt.Key_Left ? -1 : e.key === Qt.Key_Right ? 1 : e.key === Qt.Key_Up ? -grid.columns : grid.columns
                let to = Math.max(0, Math.min(library.count - 1, r + step))
                while (to > 0 && !panel.isShader(to)) --to // stop at the last shader, not the New card
                if (to !== r || !panel.selecting) panel.extend(to)
            }
            else if (e.key === Qt.Key_A && ctrl) panel.selectAll()
            else if (e.key === Qt.Key_Escape && panel.selecting) panel.clearSel()
            else if (panel.selecting && e.key === Qt.Key_H && !ctrl && !panel.inTrash) panel.hideSelected()
            else if (panel.selecting && e.key === Qt.Key_Delete) {
                if (panel.inTrash || shift) panel.confirmDeleteSelected()
                else panel.trashSelected()
            }
            else if (e.key === Qt.Key_Z && ctrl) { const s = library.undo(); if (s) panel.focusSlug(s) }
            else if (e.key === Qt.Key_E && ctrl) { if (!panel.inTrash) panel.act("edit", r) }
            else if (e.key === Qt.Key_N && !ctrl) panel.edit("")
            else if (e.key === Qt.Key_S && !ctrl) { if (!panel.inTrash) panel.act("star", r) }
            else if (e.key === Qt.Key_H && !ctrl) { if (!panel.inTrash) panel.act("hide", r) }
            else if (e.key === Qt.Key_Delete) {
                if (!panel.isShader(r)) return
                if (panel.inTrash || shift) panel.confirmDelete(r)
                else panel.act("trash", r)
            }
            else if (e.key === Qt.Key_Menu || (e.key === Qt.Key_F10 && shift)) {
                if (grid.currentItem) { const p = grid.currentItem.mapToItem(panel, 24, 40); panel.openMenu(r, p.x, p.y) }
            }
            else if (e.key === Qt.Key_Question) keys.open()
            else if (e.key === Qt.Key_Comma) settings.open()
            else if (e.key === Qt.Key_Tab) { if (panel.inTrash && library.trashCount > 0) emptyBtn.forceActiveFocus(); else panel.cycle(1) }
            else if (e.key === Qt.Key_Backtab) panel.cycle(-1)
            else if (e.key === Qt.Key_Escape) panel.close()
            else return
            e.accepted = true
        }
    }

    Label {
        anchors.centerIn: grid
        visible: library.count === 0 && library.filter !== 0
        dim: true
        text: library.filter === 1 ? qsTr("No starred shaders")
            : library.filter === 2 ? qsTr("No hidden shaders") : qsTr("Trash is empty")
    }

    // Off-screen still-frame maker: renders one shader at a time to a small
    // PNG in the cache, so cards show still frames without running shaders.
    Item {
        id: maker
        x: panel.width + 50; width: 256; height: 256
        property string slug: ""
        ShaderView { id: makerView; anchors.fill: parent; running: false; time: 2.0; quality: 1.0 }
        Timer { id: makerWait; interval: 250; onTriggered: maker.grab() }
        function kick() {
            if (slug !== "") return
            const r = library.nextNeedingStill()
            if (r < 0) return
            const m = library.get(r)
            slug = m.slug
            makerView.channels = m.channels
            makerView.source = m.shaderUrl
            makerWait.restart()
        }
        function grab() {
            const path = library.stillTarget(library.rowOf(slug))
            makerView.grabToImage((res) => {
                const r = library.rowOf(maker.slug)
                if (path && res.saveToFile(path) && r >= 0) library.stillSaved(r)
                maker.slug = ""
                makerView.source = ""
                Qt.callLater(maker.kick)
            }, Qt.size(256, 256))
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
            text: library.status || panel.note || panel.swNote
        }
        // Selection bar
        Rectangle {
            visible: panel.selecting
            width: parent.width; height: visible ? 50 : 0
            color: ui.bg2
            Label { x: 16; anchors.verticalCenter: parent.verticalCenter; font.bold: true; font.pixelSize: 12
                    text: qsTr("%1 selected").arg(panel.selCount) }
            Row {
                anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                spacing: 8
                Button { visible: !panel.inTrash; text: library.filter === 2 ? qsTr("Unhide") : qsTr("Hide"); onClicked: panel.hideSelected() }
                Button { visible: !panel.inTrash; text: qsTr("Trash"); onClicked: panel.trashSelected() }
                Button { visible: panel.inTrash; text: qsTr("Restore"); onClicked: panel.restoreSelected() }
                Button { visible: panel.inTrash; text: qsTr("Delete forever"); onClicked: panel.confirmDeleteSelected() }
            }
        }
        Rectangle { width: parent.width; height: 1; color: ui.bg2; visible: panel.inTrash }
        Item {
            visible: panel.inTrash
            width: parent.width; height: visible ? 50 : 0
            Button {
                id: emptyBtn
                x: 16; anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Empty Trash")
                enabled: library.trashCount > 0
                onClicked: panel.confirmEmpty()
                Keys.onTabPressed: panel.cycle(1)
                Keys.onBacktabPressed: grid.forceActiveFocus()
                Keys.onEscapePressed: panel.close()
            }
        }
        Rectangle { width: parent.width; height: 1; color: ui.bg2 }
        Item {
            width: parent.width; height: hints.height + 24
            Column {
                id: hints
                x: 16; y: 12; width: parent.width - 32; spacing: 6
                Hint { keys: panel.selecting ? (panel.inTrash ? [["Enter", qsTr("restore")], ["Del", qsTr("delete forever")]]
                                                                : [["H", library.filter === 2 ? qsTr("unhide") : qsTr("hide")], ["Del", qsTr("trash")], ["Ctrl+Z", qsTr("undo")]])
                           : panel.inTrash ? [["Enter", qsTr("restore")], ["Del", qsTr("delete forever")]]
                           : library.filter === 2 ? [["H", qsTr("unhide")], ["Enter", qsTr("open")], ["Esc", qsTr("close")]]
                           : [["Enter", qsTr("open")], ["S", qsTr("star")], ["H", qsTr("hide")], ["Del", qsTr("trash")]] }
                Hint { keys: panel.selecting ? [["Space", qsTr("toggle")], ["Shift+←→↑↓", qsTr("extend")], ["Esc", qsTr("clear")]]
                           : panel.inTrash ? [["Tab", qsTr("empty trash")], ["Esc", qsTr("close")]]
                           : library.filter === 2 ? [["Tab", qsTr("next view")], ["?", qsTr("all keys")]]
                           : [["Space", qsTr("select")], ["N", qsTr("new")], ["?", qsTr("all keys")]] }
            }
        }
    }

    Menu {
        id: menu
        property int row: -1
        onPicked: (a) => panel.act(a, row)
        onVisibleChanged: if (!visible && !confirm.visible) grid.forceActiveFocus()
    }
    Confirm {
        id: confirm
        objectName: "confirm"
        property var run: null
        onAccepted: { if (run) run(); grid.forceActiveFocus() }
        onCanceled: grid.forceActiveFocus()
    }
    KeySheet { id: keys; objectName: "keys"; onVisibleChanged: if (!visible) grid.forceActiveFocus() }
    SettingsSheet { id: settings; objectName: "settings"; onVisibleChanged: if (!visible) grid.forceActiveFocus() }
}
