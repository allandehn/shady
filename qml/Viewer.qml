import QtQuick

// 3. Viewer: the shader fills the window, with an info block top left and a
// hint bar at the bottom. Both fade out (with the pointer) after 3 s idle.
FocusScope {
    id: viewer
    property int row: -1
    property var item: ({})
    property int quality: 50
    property int fps: 30
    property real startTime: 0
    signal close()
    signal toggleFullscreen()

    function load(r, resumeAt) {
        row = r
        item = library.get(r)
        // No real GPU: start at minimum for this session (not saved).
        quality = theme.software ? 10 : item.quality
        fps = theme.software ? 10 : item.fps
        view.time = resumeAt || 0
        wake()
    }
    function wake() { overlay.shown = true; idle.restart() }
    function flash(text) { flashText.text = text; flashChip.shown = true; flashTimer.restart() }

    Rectangle { anchors.fill: parent; color: "black" }
    ShaderView {
        id: view
        anchors.fill: parent
        source: viewer.item.shaderUrl || ""
        quality: viewer.quality / 100
        fps: viewer.fps
        running: viewer.visible
    }

    Item {
        id: overlay
        anchors.fill: parent
        property bool shown: true
        opacity: shown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { enabled: theme.animations; NumberAnimation { duration: 840 } }

        Rectangle {
            x: 16; y: 16
            width: Math.min(info.implicitWidth, viewer.width - 64) + 28
            height: info.implicitHeight + 24
            color: Qt.rgba(ui.bg.r, ui.bg.g, ui.bg.b, 0.72)
            radius: theme.rounding
            Column {
                id: info
                x: 14; y: 12; spacing: 4
                Label { text: viewer.item.name || ""; font.bold: true; font.pixelSize: 16 }
                Label { text: viewer.item.author ? qsTr("by %1").arg(viewer.item.author) : ""; dim: true; visible: text }
                Label { text: viewer.item.description || ""; visible: text }
                Label { text: viewer.item.license || ""; dim: true; font.pixelSize: 10; visible: text }
            }
        }

        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: bar.height + 20
            color: Qt.rgba(ui.bg.r, ui.bg.g, ui.bg.b, 0.72)
            Item {
                id: bar
                x: 16; y: 10; width: parent.width - 32
                height: Math.max(barHints.height, saved.height)
                Hint {
                    id: barHints
                    width: parent.width - saved.width - 24
                    keys: [["↑↓", qsTr("quality %1%").arg(viewer.quality)], ["←→", qsTr("fps %1").arg(viewer.fps)],
                           ["F", qsTr("fullscreen")], ["Esc", qsTr("close")]]
                }
                Label {
                    id: saved
                    anchors { right: parent.right; bottom: parent.bottom }
                    text: viewer.item.shaderState === "ready" ? qsTr("Saved for this shader")
                        : viewer.item.shaderState === "error" ? qsTr("Line %1 · %2").arg(viewer.item.errorLine).arg(viewer.item.errorText)
                        : qsTr("Preparing…")
                    font.pixelSize: 11
                }
            }
        }
    }

    // Value flash: only the changed value, while the overlay is hidden.
    Rectangle {
        id: flashChip
        property bool shown: false
        anchors.centerIn: parent
        width: flashCol.implicitWidth + 40; height: flashCol.implicitHeight + 24
        color: Qt.rgba(ui.bg.r, ui.bg.g, ui.bg.b, 0.8)
        radius: theme.rounding
        opacity: shown && !overlay.shown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { enabled: theme.animations; NumberAnimation { duration: 300 } }
        Column {
            id: flashCol
            anchors.centerIn: parent; spacing: 4
            Label { id: flashText; anchors.horizontalCenter: parent.horizontalCenter; font.pixelSize: 15 }
            Label { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("saved for this shader"); dim: true; font.pixelSize: 10 }
        }
    }
    Timer { id: flashTimer; interval: 1500; onTriggered: flashChip.shown = false }
    Timer { id: idle; interval: 3000; onTriggered: overlay.shown = false }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: overlay.shown ? Qt.ArrowCursor : Qt.BlankCursor
        onPositionChanged: (m) => { view.mouse = Qt.point(m.x, m.y); viewer.wake() }
    }

    Keys.onPressed: (e) => {
        const wasHidden = !overlay.shown
        if (e.key === Qt.Key_Up || e.key === Qt.Key_Down) {
            const q = Math.max(10, Math.min(100, Math.round(viewer.quality / 10) * 10 + (e.key === Qt.Key_Up ? 10 : -10)))
            if (q !== viewer.quality) { viewer.quality = q; library.setQuality(viewer.row, q) }
            if (wasHidden) { viewer.flash(qsTr("quality %1%").arg(q)); e.accepted = true; return }
        } else if (e.key === Qt.Key_Left || e.key === Qt.Key_Right) {
            const f = library.stepFps(viewer.fps, e.key === Qt.Key_Right ? 1 : -1)
            if (f !== viewer.fps) { viewer.fps = f; library.setFps(viewer.row, f) }
            if (wasHidden) { viewer.flash(qsTr("fps %1").arg(f)); e.accepted = true; return }
        } else if (e.key === Qt.Key_F) {
            viewer.toggleFullscreen()
        } else if (e.key === Qt.Key_Escape || e.key === Qt.Key_Q) {
            library.flush()
            viewer.close()
            e.accepted = true
            return
        } else {
            return
        }
        e.accepted = true
        viewer.wake()
    }
}
