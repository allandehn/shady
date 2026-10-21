import QtQuick

// One card: preview on top, name and "by author" below. Neutral, no colored
// border; focus or hover adds a 1 px accent outline and swaps the author line
// for the shader's saved settings. Only the current card plays live.
Item {
    id: card
    required property int index
    required property string slug
    required property string name
    required property var author
    required property int quality
    required property int fps
    required property bool starred
    required property bool hidden
    required property string shaderState
    required property int errorLine
    required property url shaderUrl
    required property url stillUrl
    property bool current: false
    property bool live: false
    property var resume  // session map slug -> playback time
    signal activated()
    signal picked()

    readonly property bool hot: current || mouse.containsMouse
    opacity: hidden ? 0.55 : 1

    Rectangle {
        id: frame
        anchors.fill: parent
        anchors.margins: 4
        color: ui.bg2
        radius: theme.rounding
        border.width: card.hot ? 1 : 0
        border.color: ui.accent
        clip: true

        Item {
            id: preview
            x: 1; y: 1
            width: parent.width - 2
            height: Math.round(width * 0.46)

            Rectangle { anchors.fill: parent; color: ui.bg2 }
            Image {
                anchors.fill: parent
                source: card.stillUrl
                visible: !view.ready || !view.visible
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: false
                sourceSize: Qt.size(320, 148)
            }
            ShaderView {
                id: view
                anchors.fill: parent
                visible: card.live && card.shaderState === "ready"
                running: visible
                source: visible ? card.shaderUrl : ""
                quality: 0.35
                fps: Math.min(30, card.fps)
                // Resume where this card left off during this session.
                onVisibleChanged: {
                    if (visible) time = card.resume[card.slug] || 2.0
                    else card.resume[card.slug] = time
                }
            }
            Label {
                anchors.centerIn: parent
                visible: (card.shaderState === "compiling" || card.shaderState === "ready") && card.stillUrl == "" && !view.visible
                text: qsTr("Preparing…")
                dim: true
                font.pixelSize: 11
            }
            Hatch {
                anchors.fill: parent
                visible: card.shaderState === "error" || card.shaderState === "unreadable"
                label: card.shaderState === "error" ? qsTr("Not compiling") : qsTr("Can't read this shader")
            }
            // Star on a small background chip, in the foreground color.
            Rectangle {
                visible: card.starred
                anchors { top: parent.top; right: parent.right; margins: 6 }
                width: 18; height: 18
                color: ui.bg
                radius: Math.min(theme.rounding, 4)
                Label { anchors.centerIn: parent; text: "★"; font.pixelSize: 11 }
            }
        }

        Column {
            anchors { left: parent.left; right: parent.right; top: preview.bottom; margins: 10; topMargin: 9 }
            spacing: 3
            Label { width: parent.width; text: card.name; font.bold: true }
            Label {
                width: parent.width
                dim: true
                font.pixelSize: 11
                text: card.shaderState === "unreadable" ? card.slug
                    : card.shaderState === "error" ? qsTr("Draft · line %1").arg(card.errorLine)
                    : card.hidden && !card.hot ? qsTr("Hidden")
                    : card.hot ? qsTr("%1% · %2 fps").arg(card.quality).arg(card.fps)
                    : (card.author ? qsTr("by %1").arg(card.author) : "")
            }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: card.picked()
        onDoubleClicked: card.activated()
    }
}
