import QtQuick

// Off-screen liveliness meter for burn-in protection: renders ~90 s of shader
// time at 1 fps (iTime set directly, so it runs faster than realtime) at 160x90
// and lets the library count how many pixels ever change. One shader at a
// time, only while idle (paused in the viewer); results are cached by source.
// The editor asks for its current compile first via request().
Item {
    id: maker
    x: -3000; width: 160; height: 90
    property bool paused: false
    readonly property int samples: 90
    property string code: ""        // source being measured ("" = idle)
    property int frame: 0
    property int tries: 0
    property int idleMs: 1500       // pause between shaders (the UI test shortens it)
    property var wanted: null       // {code, url, channels} from the editor
    readonly property bool busy: code !== ""
    ShaderView { id: view; anchors.fill: parent; running: false; quality: 1.0 }
    Timer { id: settle; interval: 250; onTriggered: maker.step() }
    Timer { id: idle; interval: maker.idleMs; onTriggered: maker.kick() }

    function request(c, url, channels) {
        if (!c || library.livelyOf(c) >= 0) return
        wanted = { code: c, url: url, channels: channels || [] }
        Qt.callLater(kick)
    }
    function kick() {
        if (busy || paused) return
        let job = null
        if (wanted && library.livelyOf(wanted.code) < 0) job = wanted
        wanted = null
        if (!job) {
            const r = library.nextNeedingLively()
            if (r < 0) return
            const m = library.get(r)
            job = { code: library.codeOf(m.slug), url: m.shaderUrl, channels: m.channels }
        }
        code = job.code
        frame = 0
        tries = 0
        library.livelyBegin()
        view.channels = job.channels
        view.time = 0
        view.source = job.url
        settle.restart()
    }
    function step() {
        // Off-window, a ShaderEffect may not report Compiled until it's
        // drawn; give it a moment, then measure anyway (like the stills).
        if (!view.ready && ++tries < 4) { settle.restart(); return }
        if (view.source == "") { library.livelyFailed(code); code = ""; idle.restart(); return }
        view.time = frame
        view.grabToImage((res) => {
            library.livelyAdd(res.image)
            if (++maker.frame < maker.samples && !maker.paused) Qt.callLater(maker.step)
            else maker.done()
        }, Qt.size(width, height))
    }
    function done() {
        if (frame >= samples) library.livelyFinish(code)
        else library.livelyBegin() // interrupted: start over later
        view.source = ""
        code = ""
        idle.restart()
    }
    onPausedChanged: if (!paused) idle.restart()
    Connections { target: library; function onCountChanged() { idle.restart() } }
    Component.onCompleted: idle.restart()
}
