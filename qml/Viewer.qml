import QtQuick

// 3. Viewer: the shader fills the window, with one centred badge that fades
// out (with the pointer) after 3 s idle.
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
        overlay.bigText = ""; overlay.changed = "" // every open starts in start mode
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
    function flash(what, text) { overlay.changed = what; overlay.bigText = text; hl.restart(); bigTimer.restart(); wake() }

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

    // One badge, dead centre: name and credits, then the settings with their
    // keys. A setting you just changed shows big, with a brief accent flash.
    // Fades out (with the pointer) after 3 s idle; nothing else on screen.
    Item {
        id: overlay
        objectName: "badge"
        anchors.fill: parent
        property bool shown: true
        property string changed: ""     // "quality" | "fps" | "auto" while freshly changed
        property string bigText: ""
        // Start mode: name and credits. Change mode: the changed setting, big, with the settings rows.
        readonly property bool changeMode: bigText !== ""
        opacity: shown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { enabled: theme.animations; NumberAnimation { duration: 840 } }

        Rectangle {
            id: badge
            anchors.centerIn: parent
            width: Math.min(Math.max(info.implicitWidth, 300), viewer.width - 64) + 48
            height: info.implicitHeight + 40
            color: Qt.rgba(ui.bg.r, ui.bg.g, ui.bg.b, 0.95)
            radius: theme.rounding
            Column {
                id: info
                x: 24; y: 20; width: badge.width - 48; spacing: 6
                Label { width: parent.width; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                        text: viewer.item.name || ""; font.bold: true; font.pixelSize: 22; objectName: "badgeTitle"; visible: !overlay.changeMode }
                Label { width: parent.width; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                        text: viewer.item.author ? qsTr("by %1").arg(viewer.item.author) : ""; dim: true; visible: text && !overlay.changeMode }
                Label { width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                        text: viewer.item.description || ""; visible: text && !overlay.changeMode }
                Label { width: parent.width; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                        text: viewer.item.license || ""; dim: true; font.pixelSize: 10; visible: text && !overlay.changeMode }
                // Label and value read as one unit: accent, letter-spaced, tight on top.
                Column {
                    width: parent.width; spacing: 0
                    visible: overlay.changeMode
                    Label {
                        objectName: "badgeLabel"
                        width: parent.width; horizontalAlignment: Text.AlignHCenter
                        text: (overlay.changed === "quality" ? qsTr("Quality") : overlay.changed === "fps" ? qsTr("FPS")
                              : overlay.changed === "auto" ? qsTr("Auto-adapt") : "").toUpperCase()
                        color: ui.accent; font.pixelSize: 15; font.bold: true; font.letterSpacing: 3
                    }
                    Label {
                        id: big
                        objectName: "badgeValue"
                        width: parent.width; horizontalAlignment: Text.AlignHCenter
                        text: overlay.bigText
                        font.pixelSize: 40; font.bold: true
                        color: hl.running ? ui.accent : ui.fg
                        Behavior on color { enabled: theme.animations; ColorAnimation { duration: 400 } }
                    }
                }
                Item { width: 1; height: 4; visible: overlay.changeMode }
                Repeater {
                    model: [
                        { id: "quality", name: qsTr("Quality"), value: viewer.quality + "%", keys: "↑↓" },
                        { id: "fps", name: qsTr("FPS"), value: String(viewer.fps), keys: "←→" },
                        { id: "auto", name: qsTr("Auto-adapt"),
                          value: viewer.adapted > 0 ? qsTr("%1%").arg(viewer.adapted) : library.autoAdapt ? qsTr("on") : qsTr("off"), keys: "a" }
                    ]
                    Item {
                        required property var modelData
                        objectName: "badgeRow"
                        visible: overlay.changeMode
                        width: info.width; height: 18
                        readonly property bool hot: overlay.changed === modelData.id && hl.running
                        Label { text: modelData.name; dim: true; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                        Label { anchors.centerIn: parent; text: modelData.value; font.pixelSize: 12; font.bold: true
                                color: parent.hot ? ui.accent : ui.fg
                                Behavior on color { enabled: theme.animations; ColorAnimation { duration: 400 } } }
                        Label { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                text: modelData.keys; font.pixelSize: 12; font.bold: true }
                    }
                }
                Item { width: 1; height: 4 }
                // Start mode: the settings keys, no values (those show when you change one).
                Item {
                    objectName: "badgeKeys"
                    visible: !overlay.changeMode
                    width: info.width; height: 24 // ~8 px extra above, after the license line
                    Row { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom
                          height: 16; spacing: 10
                          Repeater { model: [["↑↓", qsTr("quality")], ["←→", qsTr("fps")], ["a", qsTr("auto-adapt")]]
                              Row { required property var modelData
                                    Label { text: modelData[0]; font.bold: true; font.pixelSize: 10 }
                                    Label { text: ":" + modelData[1]; dim: true; font.pixelSize: 10 } } } }
                }
                Item {
                    width: info.width; height: 16
                    Label { anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 10; dim: true
                            visible: overlay.changeMode // start mode: just the F / Esc hints
                            width: parent.width - escRow.width - 12; elide: Text.ElideRight
                            text: viewer.adapted > 0 ? qsTr("adapted to %1%").arg(viewer.adapted)
                                : viewer.openedAtMinimum ? qsTr("opened at minimum")
                                : viewer.item.shaderState === "ready" ? qsTr("Saved for this shader")
                                : viewer.item.shaderState === "error" ? qsTr("Line %1 · %2").arg(viewer.item.errorLine).arg(viewer.item.errorText)
                                : qsTr("Preparing…") }
                    Row { id: escRow; anchors.verticalCenter: parent.verticalCenter
                          x: overlay.changeMode ? parent.width - width : (parent.width - width) / 2; spacing: 10
                          Row { Label { text: "f"; font.bold: true; font.pixelSize: 10 } Label { text: ":" + qsTr("fullscreen"); dim: true; font.pixelSize: 10 } }
                          Row { Label { text: "esc"; font.bold: true; font.pixelSize: 10 } Label { text: ":" + qsTr("close"); dim: true; font.pixelSize: 10 } } }
                }
            }
        }
        Timer { id: hl; interval: 1200 }
        Timer { id: bigTimer; interval: 2500; onTriggered: { overlay.bigText = ""; overlay.changed = "" } }
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
                Row { anchors.verticalCenter: parent.verticalCenter
                    Label { text: "esc"; font.bold: true; font.pixelSize: 11 }
                    Label { text: ":" + qsTr("close"); dim: true; font.pixelSize: 11 } }
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

    Timer { id: idle; interval: 3000; onTriggered: overlay.shown = false }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: overlay.shown ? Qt.ArrowCursor : Qt.BlankCursor
        onPositionChanged: (m) => { view.mouse = Qt.point(m.x, m.y); viewer.wake() }
    }

    Keys.onPressed: (e) => {
        if (e.key === Qt.Key_Up || e.key === Qt.Key_Down) {
            const q = Math.max(10, Math.min(100, Math.round(viewer.quality / 10) * 10 + (e.key === Qt.Key_Up ? 10 : -10)))
            if (q !== viewer.quality) { viewer.quality = q; library.setQuality(viewer.row, q) }
            viewer.adapted = 0
            watch.restartGrace()
            viewer.flash("quality", qsTr("%1%").arg(q))
        } else if (e.key === Qt.Key_Left || e.key === Qt.Key_Right) {
            const f = library.stepFps(viewer.fps, e.key === Qt.Key_Right ? 1 : -1)
            if (f !== viewer.fps) { viewer.fps = f; library.setFps(viewer.row, f); watch.restartGrace() }
            viewer.flash("fps", qsTr("%1 fps").arg(f))
        } else if (e.key === Qt.Key_A) {
            library.autoAdapt = !library.autoAdapt
            if (!library.autoAdapt) viewer.adapted = 0
            viewer.flash("auto", library.autoAdapt ? qsTr("auto-adapt on") : qsTr("auto-adapt off"))
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
