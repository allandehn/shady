import QtQuick

// Renders one Shadertoy-style shader (a .qsb built by the app's wrapper) at
// `quality` (0.1..1.0) of the item's pixel size and scales it up smoothly.
// `fps` is a real frame limit: the scene only changes when the timer ticks,
// so Qt Quick renders nothing in between. `running: false` stops rendering.
Item {
    id: root
    property url source
    property real quality: 0.5
    property int fps: 30
    property bool running: true
    property real time: 0
    property int frame: 0
    property real delta: 0
    property point mouse: Qt.point(0, 0)
    property var channels: []   // 4 x {url, filter, wrap, resolution} from library.channelsOf()
    readonly property bool ready: fxLoader.item !== null && fxLoader.item.status === ShaderEffect.Compiled
    readonly property real dpr: Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1
    readonly property real timeWrap: 3600 // keep float precision over long runs

    // Created only when there is a shader, so an empty view costs nothing.
    Loader {
        id: fxLoader
        active: root.source != ""
        sourceComponent: Component {
            ShaderEffect {
                id: fx
                width: Math.max(1, Math.round(root.width * root.quality))
                height: Math.max(1, Math.round(root.height * root.quality))
                transformOrigin: Item.TopLeft
                scale: root.width / Math.max(1, width)
                layer.enabled: root.quality < 1.0 || !root.running // when stopped, reuse the last frame instead of re-running the shader on every repaint; at 100% while running, no extra copy
                layer.smooth: true
                blending: false
                fragmentShader: root.source

                property real iTime: root.time
                property real iTimeDelta: root.delta
                property int iFrame: root.frame
                property vector3d iResolution: Qt.vector3d(width * root.dpr, height * root.dpr, 1)
                property vector4d iMouse: Qt.vector4d(root.mouse.x * root.quality * root.dpr,
                                                      (root.height - root.mouse.y) * root.quality * root.dpr, 0, 0)
                property vector4d iDate: Qt.vector4d(0, 0, 0, 0)
                property var iChannel0: ch0.texture
                property var iChannel1: ch1.texture
                property var iChannel2: ch2.texture
                property var iChannel3: ch3.texture
                property vector3d shady_chres0: ch0.resolution
                property vector3d shady_chres1: ch1.resolution
                property vector3d shady_chres2: ch2.resolution
                property vector3d shady_chres3: ch3.resolution
            }
        }
    }

    Item {
        visible: fxLoader.active
        Channel { id: ch0; info: root.channels[0] || ({}) }
        Channel { id: ch1; info: root.channels[1] || ({}) }
        Channel { id: ch2; info: root.channels[2] || ({}) }
        Channel { id: ch3; info: root.channels[3] || ({}) }
    }

    Timer {
        interval: Math.round(1000 / Math.max(1, root.fps))
        repeat: true
        running: root.running && root.visible && root.source != ""
        property double last: 0
        onRunningChanged: last = Date.now()
        onTriggered: {
            const now = Date.now()
            root.delta = Math.min(1, (now - last) / 1000)
            root.time = (root.time + root.delta) % root.timeWrap
            last = now
            root.frame++
        }
    }
}
