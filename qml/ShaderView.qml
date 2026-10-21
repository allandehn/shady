import QtQuick

// Renders one Shadertoy-style shader at `quality` (0.1..1.0) of the item's
// pixel size and scales it up smoothly. `fps` is a real frame limit: the
// scene only changes when the timer ticks, so Qt Quick renders nothing in
// between. `running: false` stops rendering completely.
Item {
    id: root
    property url source: "qrc:/shaders/default.frag.qsb"
    property real quality: 0.5
    property int fps: 30
    property bool running: true
    property real time: 0
    property int frame: 0
    readonly property real dpr: Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1

    // iTime wraps every hour to keep float precision (see brief).
    readonly property real timeWrap: 3600

    ShaderEffect {
        id: fx
        width: Math.max(1, Math.round(root.width * root.quality))
        height: Math.max(1, Math.round(root.height * root.quality))
        transformOrigin: Item.TopLeft
        scale: root.width / Math.max(1, width)
        layer.enabled: root.quality < 1.0
        layer.smooth: true
        blending: false
        fragmentShader: root.source

        property real iTime: root.time
        property int iFrame: root.frame
        property vector3d iResolution: Qt.vector3d(width * root.dpr, height * root.dpr, 1)
        property vector4d iMouse: Qt.vector4d(0, 0, 0, 0)
    }

    Timer {
        id: tick
        interval: Math.round(1000 / Math.max(1, root.fps))
        repeat: true
        running: root.running && root.visible
        property double last: 0
        onRunningChanged: last = Date.now()
        onTriggered: {
            const now = Date.now()
            root.time = (root.time + (now - last) / 1000) % root.timeWrap
            last = now
            root.frame++
        }
    }
}
