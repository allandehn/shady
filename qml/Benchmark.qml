import QtQuick

// First-run benchmark: quietly renders a simple reference shader a few times
// off-screen and times it, so new shaders get sensible defaults (modest on
// fast machines, minimum on slow ones). Runs once; the result is saved.
Item {
    id: bench
    x: -2000; width: 384; height: 216
    property bool done: library.benchmarked
    property double t0: 0
    property int n: 0
    property var times: []
    readonly property string code: "void mainImage(out vec4 o, in vec2 p)\n{\n"
        + "    vec2 uv = (2.0 * p - iResolution.xy) / iResolution.y;\n"
        + "    vec3 ro = vec3(0.0, 0.0, -3.0), rd = normalize(vec3(uv, 1.5));\n"
        + "    float t = 0.0;\n"
        + "    for (int i = 0; i < 64; i++) {\n"
        + "        vec3 q = ro + rd * t;\n"
        + "        q.xy = mod(q.xy + 1.0, 2.0) - 1.0;\n"
        + "        float d = length(q) - 0.5 - 0.05 * sin(q.x * 10.0 + iTime);\n"
        + "        if (d < 0.001) break;\n"
        + "        t += d;\n"
        + "    }\n"
        + "    o = vec4(vec3(1.0 / (1.0 + t * t * 0.1)), 1.0);\n}\n"
    ShaderView { id: view; anchors.fill: parent; running: false; quality: 1.0; time: 1.0 }
    Timer { id: start; interval: 800; onTriggered: library.compileDraft(bench.code, []) }
    Connections {
        target: library
        enabled: !bench.done
        function onDraftCompiled(ok, url, line, text, src, types) {
            if (src !== bench.code) return
            if (!ok) { library.setBenchmark(100, theme.software); return }
            view.source = url
            Qt.callLater(bench.step)
        }
    }
    function step() {
        t0 = Date.now()
        view.time += 0.1
        view.grabToImage(function() {
            times.push(Date.now() - t0)
            if (++n < 12) { Qt.callLater(bench.step); return }
            const s = times.slice(2).sort((a, b) => a - b) // skip pipeline warm-up
            const ms = s[Math.floor(s.length / 2)]
            library.setBenchmark(ms, theme.software)
            view.source = ""
            done = true
        })
    }
    Component.onCompleted: if (!done && !theme.software) start.start()
    Connections {
        target: theme
        function onSoftwareChanged() { if (theme.software && !bench.done) { library.setBenchmark(100, true); bench.done = true } }
    }
}
