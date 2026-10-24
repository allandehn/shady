import QtQuick
import QtQuick.Shapes

// Empty tab: big dim outline shades (tilted 8°), a dim title, then either a
// key hint in the Hint style ("s star a shader") or a plain dim line.
Column {
    id: empty
    property string title: ""
    property string line: ""
    property var keyHint: []      // ["s", "star a shader"]
    spacing: 0
    // #414868 on the default Tokyo Night palette; other themes get the same
    // weight between their background and text.
    readonly property color shadeColor: Qt.colorEqual(ui.bg, "#1a1b26") ? "#414868"
        : Qt.rgba(ui.bg.r + (ui.fg.r - ui.bg.r) * 0.27, ui.bg.g + (ui.fg.g - ui.bg.g) * 0.27, ui.bg.b + (ui.fg.b - ui.bg.b) * 0.27, 1)
    Item {   // data/empty-shades-t8.svg, drawn here so it follows the theme
        anchors.horizontalCenter: parent.horizontalCenter
        width: 128; height: 69
        Shape {
            x: 15.6; y: 16.9
            transform: Rotation { origin.x: 64 - 15.6; origin.y: 34.5 - 16.9; angle: -8 }
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                fillColor: "transparent"; strokeColor: empty.shadeColor; strokeWidth: 5
                joinStyle: ShapePath.MiterJoin; capStyle: ShapePath.FlatCap
                PathSvg { path: "M0,0 L39.6,0 L39.6,22 L26.4,35.2 L8.8,35.2 L0,22 Z M96.8,0 L57.2,0 L57.2,22 L70.4,35.2 L88,35.2 L96.8,22 Z M39.6,2.5 L57.2,2.5 M0,0 L-6.6,0 M96.8,0 L103.4,0" }
            }
        }
    }
    Item { width: 1; height: 14 }
    Label { anchors.horizontalCenter: parent.horizontalCenter; text: empty.title; dim: true; font.pixelSize: 13 }
    Item { width: 1; height: 6 }
    Label { anchors.horizontalCenter: parent.horizontalCenter; visible: empty.line !== ""; text: empty.line; dim: true; font.pixelSize: 11 }
    Row {   // same look as Hint.qml: bold key, dim " action"
        anchors.horizontalCenter: parent.horizontalCenter
        visible: empty.keyHint.length === 2
        Label { text: empty.keyHint[0] || ""; font.pixelSize: 11; font.bold: true }
        Label { text: " " + (empty.keyHint[1] || ""); font.pixelSize: 11; dim: true }
    }
}
