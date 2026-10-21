import QtQuick
import QtQuick.Window

Window {
    id: win
    width: 620
    height: 760
    visible: true
    color: ui.bg
    title: qsTr("Shady")

    // Theme colors, crossfading over ~840 ms when the theme changes.
    QtObject {
        id: ui
        objectName: "ui"
        // --doc-shots only: the live card renders at full quality so README images are sharp.
        property bool crisp: false
        property color bg: theme.background
        property color bg2: theme.lighterBackground
        property color fg: theme.foreground
        property color muted: theme.muted
        property color accent: theme.accent
        Behavior on bg { enabled: theme.animations; ColorAnimation { duration: 840 } }
        Behavior on bg2 { enabled: theme.animations; ColorAnimation { duration: 840 } }
        Behavior on fg { enabled: theme.animations; ColorAnimation { duration: 840 } }
        Behavior on muted { enabled: theme.animations; ColorAnimation { duration: 840 } }
        Behavior on accent { enabled: theme.animations; ColorAnimation { duration: 840 } }
    }

    readonly property bool hiddenOrOff: win.visibility === Window.Hidden || win.visibility === Window.Minimized
    property string mode: "list"
    property QtObject meter: null   // FrameMeter, set from C++

    ShaderList {
        id: list
        objectName: "list"
        anchors.fill: parent
        visible: win.mode === "list" && !win.hiddenOrOff
        focus: win.mode === "list"
        swNote: theme.software ? qsTr("Software rendering · shaders start at minimum settings") : ""
        onOpen: (r) => {
            viewer.load(r, list.resume[library.get(r).slug] || 0)
            win.mode = "viewer"
            viewer.forceActiveFocus()
        }
        onClose: Qt.quit()
        onEdit: (slug) => {
            win.mode = "editor"
            editor.openShader(slug)
        }
    }

    Editor {
        id: editor
        objectName: "editor"
        anchors.fill: parent
        visible: win.mode === "editor" && !win.hiddenOrOff
        onClosed: (slug) => {
            win.mode = "list"
            if (slug) list.focusSlug(slug)
            list.forceActiveFocus()
        }
    }

    Viewer {
        id: viewer
        objectName: "viewer"
        anchors.fill: parent
        meter: win.meter
        visible: win.mode === "viewer" && !win.hiddenOrOff
        onClose: {
            if (win.visibility === Window.FullScreen) win.visibility = Window.Windowed
            win.mode = "list"
            list.focusRow(viewer.row)
            list.forceActiveFocus()
        }
        onToggleFullscreen: win.visibility = win.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
    }

    Benchmark { objectName: "benchmark" }

    Component.onCompleted: list.forceActiveFocus()
}
