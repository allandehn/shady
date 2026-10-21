import QtQuick
import QtQuick.Window

Window {
    id: win
    width: 960
    height: 540
    visible: true
    color: "black"
    title: qsTr("Shady")

    ShaderView {
        id: view
        anchors.fill: parent
        quality: shadyQuality
        fps: shadyFps
        focus: true
        running: win.visibility !== Window.Hidden && win.visibility !== Window.Minimized

        Keys.onPressed: (e) => {
            if (e.key === Qt.Key_Escape || e.key === Qt.Key_Q) Qt.quit()
            else if (e.key === Qt.Key_F)
                win.visibility = win.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
            else if (e.key === Qt.Key_Up) view.quality = Math.min(1.0, Math.round(view.quality * 10 + 1) / 10)
            else if (e.key === Qt.Key_Down) view.quality = Math.max(0.1, Math.round(view.quality * 10 - 1) / 10)
        }
    }
}
