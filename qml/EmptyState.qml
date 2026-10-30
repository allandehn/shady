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
    // Square pair at R == 0 (Omarchy's default), the rounded pair when R > 0;
    // follows theme.rounding live.
    readonly property bool roundShades: theme.rounding > 0
    Item {   // data/empty-shades-t8.svg / empty-shades-rounded-t8.svg, drawn here so they follow the theme
        anchors.horizontalCenter: parent.horizontalCenter
        width: 128; height: 69
        Shape {
            objectName: "shadesRounded"
            visible: empty.roundShades
            x: 15.6; y: 16.9
            transform: Rotation { origin.x: 64 - 15.6; origin.y: 34.5 - 16.9; angle: -8 }
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                fillColor: "transparent"; strokeColor: empty.shadeColor; strokeWidth: 5
                joinStyle: ShapePath.RoundJoin; capStyle: ShapePath.RoundCap
                PathSvg { path: "M 0.00 6.00 Q 0.00 0.00 6.00 0.00 L 33.60 0.00 Q 39.60 0.00 39.60 6.00 L 39.60 16.00 Q 39.60 22.00 35.36 26.24 L 30.64 30.96 Q 26.40 35.20 20.40 35.20 L 14.80 35.20 Q 8.80 35.20 5.47 30.21 L 3.33 26.99 Q 0.00 22.00 0.00 16.00 Z M 96.80 6.00 Q 96.80 0.00 90.80 0.00 L 63.20 0.00 Q 57.20 0.00 57.20 6.00 L 57.20 16.00 Q 57.20 22.00 61.44 26.24 L 66.16 30.96 Q 70.40 35.20 76.40 35.20 L 82.00 35.20 Q 88.00 35.20 91.33 30.21 L 93.47 26.99 Q 96.80 22.00 96.80 16.00 Z M39.6,2.5 Q48.4,-2.5 57.2,2.5 M0,0 L-6.6,0 M96.8,0 L103.4,0" }
            }
        }
        Shape {
            objectName: "shadesSquare"
            visible: !empty.roundShades
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
