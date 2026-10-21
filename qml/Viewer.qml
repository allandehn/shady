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
    property var meter: null
    // Potato support: session-only adaptation, the too-heavy pause, and
    // "opened at minimum" after a crash. Saved settings change only by keys.
    property int adapted: 0            // 0 = not adapted, else the session quality
    property bool paused: false
    property bool openedAtMinimum: false
    readonly property int renderQuality: adapted > 0 ? adapted : quality
    readonly property int tryQuality: Math.max(10, Math.floor(renderQuality / 20) * 10)
    signal close()
    signal toggleFullscreen()

    function load(r, resumeAt) {
        row = r
        item = library.get(r)
        // No real GPU: start at minimum for this session (not saved).
        openedAtMinimum = library.crashedLastTime(item.slug)
        quality = theme.software || openedAtMinimum ? 10 : item.quality
        fps = theme.software || openedAtMinimum ? 10 : item.fps
        adapted = 0
        paused = false
        view.time = resumeAt || 0
        library.viewerOpened(item.slug)
        watch.restartGrace()
        wake()
    }
    function leave() {
        library.flush()
        library.viewerClosed()
        paused = false
        viewer.close()
    }
    function tryLower() {
        quality = tryQuality
        library.setQuality(row, quality)
        library.flush()
        adapted = 0
        paused = false
        watch.restartGrace()
        wake()
    }
    function wake() { overlay.shown = true; idle.restart() }
    function flash(text) { flashText.text = text; flashChip.shown = true; flashTimer.restart() }

    Rectangle { anchors.fill: parent; color: "black" }
    ShaderView {
        id: view
        anchors.fill: parent
        source: viewer.item.shaderUrl || ""
        channels: viewer.item.channels || []
        quality: viewer.renderQuality / 100
        fps: viewer.fps
        running: viewer.visible && !viewer.paused
        opacity: viewer.paused ? 0.35 : 1
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
                    keys: viewer.adapted > 0
                        ? [["↑↓", qsTr("quality %1%").arg(viewer.quality)], ["←→", qsTr("fps %1").arg(viewer.fps)],
                           ["A", qsTr("auto off")], ["Esc", qsTr("close")]]
                        : [["↑↓", qsTr("quality %1%").arg(viewer.quality)], ["←→", qsTr("fps %1").arg(viewer.fps)],
                           ["F", qsTr("fullscreen")], ["Esc", qsTr("close")]]
                }
                Label {
                    id: saved
                    anchors { right: parent.right; bottom: parent.bottom }
                    font.bold: viewer.adapted > 0
                    text: viewer.adapted > 0 ? qsTr("adapted to %1%").arg(viewer.adapted)
                        : viewer.openedAtMinimum ? qsTr("opened at minimum")
                        : viewer.item.shaderState === "ready" ? qsTr("Saved for this shader")
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
    // Frame watch: pause when a frame takes far too long (> 500 ms), and
    // lower the session resolution when the fps setting can't be held.
    Timer {
        id: watch
        interval: 100
        repeat: true
        running: viewer.visible && !viewer.paused && viewer.meter !== null
        property double graceUntil: 0
        property double winStart: 0
        property int winFrames: 0
        property int slowWindows: 0
        function restartGrace() {
            graceUntil = viewer.meter ? viewer.meter.nowMs() + 1500 : 0
            winStart = 0; slowWindows = 0
        }
        onTriggered: {
            const m = viewer.meter, now = m.nowMs()
            if (now < graceUntil) return
            if (m.inFlightMs() > 500 || m.lastFrameMs() > 500) {
                viewer.paused = true
                library.slowMachine = true
                pauseBox.forceActiveFocus()
                return
            }
            if (winStart === 0) { winStart = now; winFrames = m.frames(); return }
            if (now - winStart < 1000) return
            const achieved = (m.frames() - winFrames) * 1000 / (now - winStart)
            winStart = now; winFrames = m.frames()
            if (!library.autoAdapt || viewer.renderQuality <= 10) { slowWindows = 0; return }
            if (achieved < viewer.fps * 0.8) {
                if (++slowWindows >= 2) {
                    // Pixels cost ~quality^2: scale to the rate we actually reached.
                    const q = viewer.renderQuality * Math.sqrt(Math.max(0.05, achieved / viewer.fps))
                    viewer.adapted = Math.max(10, Math.min(viewer.renderQuality - 10, Math.floor(q / 10) * 10))
                    library.slowMachine = true
                    viewer.wake()
                    restartGrace()
                }
            } else {
                slowWindows = 0
            }
        }
    }

    // Too-heavy pause: one primary action that names the value.
    FocusScope {
        id: pauseBox
        visible: viewer.paused
        anchors.centerIn: parent
        width: Math.min(viewer.width - 40, 320); height: pcol.implicitHeight + 32
        Rectangle { anchors.fill: parent; color: ui.bg2; radius: theme.rounding
                    border.width: 1; border.color: Qt.rgba(ui.muted.r, ui.muted.g, ui.muted.b, 0.3) }
        Column {
            id: pcol
            x: 16; y: 16; width: parent.width - 32; spacing: 8
            Label { text: qsTr("Paused"); font.bold: true }
            Label { width: parent.width; wrapMode: Text.WordWrap; text: qsTr("This shader is too heavy here. Try lower quality?") }
            Item { width: 1; height: 6 }
            Item {
                width: parent.width; height: 30
                Row { anchors.verticalCenter: parent.verticalCenter; spacing: 6
                    Label { text: "Esc"; font.bold: true; font.pixelSize: 11 }
                    Label { text: qsTr("close"); dim: true; font.pixelSize: 11 } }
                Button { id: tryBtn; anchors.right: parent.right; primary: true; focus: true
                         text: qsTr("Try %1%").arg(viewer.tryQuality); onClicked: viewer.tryLower() }
            }
        }
        Keys.onPressed: (e) => {
            if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) viewer.tryLower()
            else if (e.key === Qt.Key_Escape || e.key === Qt.Key_Q) viewer.leave()
            e.accepted = true
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
            viewer.adapted = 0
            watch.restartGrace()
            if (wasHidden) { viewer.flash(qsTr("quality %1%").arg(q)); e.accepted = true; return }
        } else if (e.key === Qt.Key_Left || e.key === Qt.Key_Right) {
            const f = library.stepFps(viewer.fps, e.key === Qt.Key_Right ? 1 : -1)
            if (f !== viewer.fps) { viewer.fps = f; library.setFps(viewer.row, f); watch.restartGrace() }
            if (wasHidden) { viewer.flash(qsTr("fps %1").arg(f)); e.accepted = true; return }
        } else if (e.key === Qt.Key_A) {
            library.autoAdapt = !library.autoAdapt
            if (!library.autoAdapt) viewer.adapted = 0
            viewer.flash(library.autoAdapt ? qsTr("auto-adapt on") : qsTr("auto-adapt off"))
        } else if (e.key === Qt.Key_F) {
            viewer.toggleFullscreen()
        } else if (e.key === Qt.Key_Escape || e.key === Qt.Key_Q) {
            viewer.leave()
            e.accepted = true
            return
        } else {
            return
        }
        e.accepted = true
        viewer.wake()
    }
}
