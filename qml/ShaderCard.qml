import QtQuick

// One card: preview on top, name and "by author" below. Neutral, no colored
// border; focus or hover adds a 1 px accent outline and swaps the author line
// for the shader's saved settings. Only the current card plays live.
// The last card in the All view is the quieter "+ New shader" card.
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
    required property bool trashed
    required property bool isNew
    required property string removed
    required property string shaderState
    required property int errorLine
    required property url shaderUrl
    required property url stillUrl
    required property var channels
    property bool selected: false
    property bool selecting: false
    signal toggled()
    property bool current: false
    property bool live: false
    property var resume  // session map slug -> playback time
    signal activated()
    signal picked()
    signal menuRequested(real x, real y)
    signal trashRequested()

    readonly property bool hot: current || mouse.containsMouse
    opacity: (hidden || trashed) && !isNew ? 0.55 : 1

    Rectangle {
        id: frame
        anchors.fill: parent
        anchors.margins: 4
        color: card.isNew ? "transparent" : ui.bg2
        radius: theme.rounding
        border.width: card.hot || card.isNew || card.selected ? 1 : 0
        // Focus keeps the accent outline; selection is a muted one, so the two never look the same.
        border.color: card.hot ? ui.accent : card.selected ? ui.muted : Qt.rgba(ui.muted.r, ui.muted.g, ui.muted.b, 0.35)
        clip: true

        // "+ New shader" card
        Column {
            visible: card.isNew
            anchors.centerIn: parent
            spacing: 8
            Label { anchors.horizontalCenter: parent.horizontalCenter; text: "+"; font.pixelSize: 22; dim: true }
            Label { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("New shader"); dim: true; font.pixelSize: 12 }
        }
        Rectangle {
            visible: card.isNew
            anchors { top: parent.top; right: parent.right; margins: 8 }
            width: 18; height: 18; color: "transparent"; radius: Math.min(theme.rounding, 4)
            border.width: 1; border.color: Qt.rgba(ui.muted.r, ui.muted.g, ui.muted.b, 0.5)
            Label { anchors.centerIn: parent; text: "N"; dim: true; font.pixelSize: 10 }
        }

        Item {
            id: preview
            visible: !card.isNew
            x: 1; y: 1
            width: parent.width - 2
            height: width // square

            Rectangle { anchors.fill: parent; color: ui.bg2 }
            Image {
                anchors.fill: parent
                source: card.stillUrl
                visible: !view.ready || !view.visible
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: false
                sourceSize: Qt.size(256, 256)
            }
            ShaderView {
                id: view
                anchors.fill: parent
                visible: card.live && card.shaderState === "ready" && !card.trashed
                running: visible
                source: visible ? card.shaderUrl : ""
                channels: card.channels
                quality: ui.crisp ? 1.0 : 0.35
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
            // Checkbox: accent check when selected; an empty box on every card in
            // selection mode, otherwise only on the hovered or focused card.
            Rectangle {
                visible: !card.isNew && (card.selected || card.selecting || card.hot)
                anchors { top: parent.top; left: parent.left; margins: 6 }
                width: 18; height: 18
                radius: Math.min(theme.rounding, 4)
                color: card.selected ? ui.accent : ui.bg
                border.width: card.selected ? 0 : 1
                border.color: ui.muted
                Label { anchors.centerIn: parent; visible: card.selected; text: "✓"; color: ui.bg; font.pixelSize: 12; font.bold: true }
                MouseArea { anchors.fill: parent; onClicked: card.toggled() }
            }
            // Star and × sit on small background chips, in the foreground color.
            Row {
                anchors { top: parent.top; right: parent.right; margins: 6 }
                spacing: 4
                layoutDirection: Qt.RightToLeft
                Rectangle {
                    visible: mouse.containsMouse && !card.trashed
                    width: 18; height: 18; color: ui.bg; radius: Math.min(theme.rounding, 4)
                    Label { anchors.centerIn: parent; text: "×"; font.pixelSize: 13 }
                    MouseArea { anchors.fill: parent; onClicked: card.trashRequested() }
                }
                Rectangle {
                    visible: card.starred && !card.trashed
                    width: 18; height: 18; color: ui.bg; radius: Math.min(theme.rounding, 4)
                    Label { anchors.centerIn: parent; text: "★"; font.pixelSize: 11 }
                }
            }
        }

        Column {
            visible: !card.isNew
            anchors { left: parent.left; right: parent.right; top: preview.bottom; margins: 10; topMargin: 8 }
            spacing: 3
            Label { width: parent.width; text: card.name; font.bold: true }
            Label {
                width: parent.width
                dim: true
                font.pixelSize: 11
                text: card.trashed ? card.removed
                    : card.shaderState === "unreadable" ? card.slug
                    : card.shaderState === "error" ? qsTr("Draft · line %1").arg(card.errorLine)
                    : card.hidden && !card.hot ? qsTr("Hidden")
                    : card.hot ? qsTr("%1% · %2 fps").arg(card.quality).arg(card.fps)
                    : (card.author ? qsTr("by %1").arg(card.author) : "")
            }
        }
    }

    MouseArea {
        id: mouse
        z: -1
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: (m) => {
            card.picked()
            if (m.button === Qt.RightButton && !card.isNew) card.menuRequested(m.x, m.y)
            else if (card.isNew) card.activated()
        }
        onDoubleClicked: card.activated()
    }
}
