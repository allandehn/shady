import QtQuick

// 3. Viewer: the shader fills the window. A bottom caption (name, credits, key
// hints) fades out with the pointer after 3 s idle; a compact centred badge
// shows a changed setting for 1.2 s.
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
        badge.shown = false; badge.changed = ""; badge.bigText = ""
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
    // A change shows only the centred badge; the caption stays out of the way.
    function flash(what, text) { badge.changed = what; badge.bigText = text; badge.shown = true; bigTimer.restart(); idle.stop(); overlay.shown = false }

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

    // Info caption: a soft scrim rising from the bottom edge, the meta line and
    // the name bottom-left, the key hints bottom-right on the name's baseline.
    // Shows on start and on any mouse move or key press; fades after 3 s idle.
    Item {
        id: overlay
        objectName: "caption"
        anchors.fill: parent
        property bool shown: true
        opacity: shown && !badge.visible ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { enabled: theme.animations; NumberAnimation { duration: 840 } }
        readonly property real u: viewer.height  // mockup sizes are fractions of the height
        readonly property real margin: Math.round(viewer.width * 0.03)
        readonly property real base: viewer.height - Math.round(u * 0.055)
        // Narrow window: the hints get their own row under the name when the
        // two don't fit side by side with at least 24 px between them.
        readonly property bool wrapped: nameLabel.implicitWidth + 24 + hintRow.implicitWidth > viewer.width - 2 * margin
        readonly property real nameBase: wrapped ? base - Math.round(Math.max(18, u * 0.035)) : base

        Rectangle {
            objectName: "scrim"
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: Math.round(parent.height * 0.30)
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(10/255, 10/255, 18/255, 0) }
                GradientStop { position: 0.5; color: Qt.rgba(10/255, 10/255, 18/255, 0.23) }
                GradientStop { position: 1.0; color: Qt.rgba(10/255, 10/255, 18/255, 0.62) }
            }
        }
        Label {
            id: metaLine
            objectName: "captionMeta"
            x: overlay.margin + 4
            width: viewer.width - x - overlay.margin
            y: overlay.nameBase - Math.round(overlay.u * 0.057) - baselineOffset
            text: {
                const p = []
                if (viewer.item.author) p.push(qsTr("by %1").arg(viewer.item.author))
                if (viewer.item.license) p.push(viewer.item.license)
                p.push(qsTr("quality") + ":" + viewer.renderQuality + "%")
                p.push(qsTr("%1 fps").arg(viewer.fps))
                return p.join(" · ").toLowerCase()
            }
            color: Qt.rgba(169/255, 177/255, 214/255, 0.88)
            font.pixelSize: Math.max(10, Math.round(overlay.u * 0.015)); font.weight: Font.Medium
            font.letterSpacing: Math.max(1, Math.round(overlay.u * 0.003))
            style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.45)
        }
        Label {
            id: nameLabel
            objectName: "captionName"
            x: overlay.margin
            width: overlay.wrapped ? viewer.width - x - overlay.margin : hintRow.x - x - 24
            y: overlay.nameBase - baselineOffset
            text: viewer.item.name || ""
            color: ui.fg
            font.pixelSize: Math.max(16, Math.round(overlay.u * 0.045)); font.weight: Font.Medium
            style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.45)
        }
        // Adaptive hints: the setting keys until you've used them once.
        Row {
            id: hintRow
            objectName: "captionHints"
            x: overlay.wrapped ? overlay.margin : viewer.width - overlay.margin - width
            y: overlay.base - hintMetric.baselineOffset
            baselineOffset: hintMetric.baselineOffset
            Label { id: hintMetric; visible: false; text: "x"; font.bold: true; font.pixelSize: hintRow.fs }
            spacing: Math.round(overlay.u * 0.016)
            readonly property int fs: Math.max(10, Math.round(overlay.u * 0.016))
            Repeater {
                model: library.learnedViewerKeys ? [["?", qsTr("keys")], ["esc", qsTr("back")]]
                     : [["↑↓", qsTr("quality")], ["←→", qsTr("fps")], ["?", qsTr("keys")], ["esc", qsTr("back")]]
                Row {
                    required property var modelData
                    objectName: "captionHint"
                    readonly property real baselineOffset: k.baselineOffset
                    Label { id: k; text: modelData[0]; font.bold: true; font.pixelSize: hintRow.fs
                            style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.45) }
                    Label { text: ":" + modelData[1]; color: "#8c95c0"; font.pixelSize: hintRow.fs
                            style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.45) }
                }
            }
        }
    }

    // Change badge, dead centre: only for quality, fps and auto-adapt.
    Item {
        id: badge
        objectName: "badge"
        anchors.fill: parent
        property string changed: ""     // "quality" | "fps" | "auto"
        property string bigText: ""
        property bool shown: false
        opacity: shown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { enabled: theme.animations; NumberAnimation { duration: 300 } }
        readonly property real u: viewer.height
        Rectangle {
            objectName: "badgeBox"
            anchors.centerIn: parent
            width: Math.max(Math.round(viewer.width * 0.14), bcol.implicitWidth + 40)
            height: bcol.implicitHeight + Math.round(badge.u * 0.05)
            color: Qt.rgba(26/255, 27/255, 38/255, 0.95)
            border.width: Math.max(1, Math.floor(viewer.height / 700)); border.color: "#292e42"
            radius: 0
            Column {
                id: bcol
                anchors.centerIn: parent
                spacing: Math.round(badge.u * 0.006)
                Label {
                    objectName: "badgeLabel"
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: (badge.changed === "quality" ? qsTr("Quality") : badge.changed === "fps" ? qsTr("FPS")
                          : badge.changed === "auto" ? qsTr("Auto-adapt") : "").toUpperCase()
                    color: ui.accent; font.pixelSize: Math.max(10, Math.round(badge.u * 0.016)); font.weight: Font.DemiBold
                    font.letterSpacing: Math.max(2, Math.round(badge.u * 0.006))
                }
                Label {
                    objectName: "badgeValue"
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: badge.bigText
                    color: ui.accent; font.pixelSize: Math.max(24, Math.round(badge.u * 0.06)); font.weight: Font.DemiBold
                }
            }
        }
        Timer { id: bigTimer; interval: 1200; onTriggered: badge.shown = false }
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
        cursorShape: overlay.shown || badge.shown ? Qt.ArrowCursor : Qt.BlankCursor
        onPositionChanged: (m) => { view.mouse = Qt.point(m.x, m.y); viewer.wake() }
    }

    Keys.onPressed: (e) => {
        if (e.key === Qt.Key_Up || e.key === Qt.Key_Down) {
            const q = Math.max(10, Math.min(100, Math.round(viewer.quality / 10) * 10 + (e.key === Qt.Key_Up ? 10 : -10)))
            if (q !== viewer.quality) { viewer.quality = q; library.setQuality(viewer.row, q) }
            viewer.adapted = 0
            watch.restartGrace()
            library.learnedViewerKeys = true
            viewer.flash("quality", qsTr("%1%").arg(q))
        } else if (e.key === Qt.Key_Left || e.key === Qt.Key_Right) {
            const f = library.stepFps(viewer.fps, e.key === Qt.Key_Right ? 1 : -1)
            if (f !== viewer.fps) { viewer.fps = f; library.setFps(viewer.row, f); watch.restartGrace() }
            library.learnedViewerKeys = true
            viewer.flash("fps", qsTr("%1 fps").arg(f))
        } else if (e.key === Qt.Key_A) {
            library.autoAdapt = !library.autoAdapt
            if (!library.autoAdapt) viewer.adapted = 0
            viewer.flash("auto", library.autoAdapt ? qsTr("auto-adapt on") : qsTr("auto-adapt off"))
        } else if (e.key === Qt.Key_Question) {
            viewerKeys.open()
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
        if (!badge.shown) viewer.wake()
    }

    KeySheet { id: viewerKeys; objectName: "viewerKeys"; z: 20; onVisibleChanged: if (!visible) viewer.forceActiveFocus() }
}
