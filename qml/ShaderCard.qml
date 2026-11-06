import QtQuick

// One card: preview on top, name and "by author" below. Neutral, no colored
// border; the highlighted card (last hovered or key-selected) gets a 1 px accent outline and swaps the author line
// for the shader's saved settings. Only the current card plays live.
// The last card in the All view is the quieter "+ New shader" card.
Item {
    id: card
    objectName: "card"
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
    required property int lively
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
    signal starRequested()
    signal editRequested()

    // One highlight, owned by the grid's current index: hovering moves it here
    // (see mouseMoved), keys move it away. So never two hot cards.
    readonly property bool hot: current
    signal mouseMoved(point scenePos)
    HoverHandler { id: hover; onPointChanged: if (hovered) card.mouseMoved(point.scenePosition) }
    opacity: (hidden || trashed) && !isNew ? 0.55 : 1
    layer.enabled: opacity < 1 // fade as one, so the corner masks still cover the preview

    Rectangle {
        id: frame
        anchors.fill: parent
        anchors.margins: 4
        color: card.isNew ? "transparent" : ui.bg2
        radius: theme.rounding
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
            Label { anchors.centerIn: parent; text: "n"; dim: true; font.pixelSize: 10 }
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
                cache: true // the URL carries the still's mtime, so a new still is a new key
                retainWhileLoading: true // keep the last thumbnail until the new one is decoded
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
                Icon { anchors.centerIn: parent; visible: card.selected; glyph: "\u{F012C}"; fallback: "✓"; color: ui.bg; size: 12; font.bold: !nerd }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: card.toggled() }
            }
            // Star and × sit on small background chips, in the foreground color, with
            // a hover tooltip naming what a click does.
            Row {
                anchors { top: parent.top; right: parent.right; margins: 6 }
                spacing: 4
                layoutDirection: Qt.RightToLeft
                Rectangle {
                    id: xChip
                    objectName: "trashChip"
                    readonly property string tipText: card.trashed ? qsTr("Remove") : qsTr("Trash")
                    // Keeps its slot when hidden, so the star doesn't jump on hover.
                    visible: !card.isNew
                    opacity: hover.hovered ? 1 : 0
                    enabled: hover.hovered
                    width: 18; height: 18; color: ui.bg; radius: Math.min(theme.rounding, 4)
                    Icon { anchors.centerIn: parent; glyph: "\u{F0156}"; fallback: "×"; size: 13 }
                    MouseArea { id: xMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: card.trashRequested() }
                    Tip { anchors { top: parent.bottom; right: parent.right; topMargin: 4 } text: xChip.tipText; hovered: xMouse.containsMouse }
                }
                Rectangle {
                    id: starChip
                    objectName: "starChip"
                    readonly property string tipText: card.starred ? qsTr("Unstar") : qsTr("Star")
                    // Starred: filled star, always. Unstarred: an outlined star on hover.
                    visible: !card.trashed && (card.starred || hover.hovered)
                    width: 18; height: 18; color: ui.bg; radius: Math.min(theme.rounding, 4)
                    Icon { anchors.centerIn: parent; glyph: card.starred ? "\u{F04CE}" : "\u{F04D2}"; fallback: card.starred ? "★" : "☆"; size: 12
                            color: starMouse.containsMouse ? ui.accent : ui.fg }
                    MouseArea { id: starMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: card.starRequested() }
                    Tip { anchors { top: parent.bottom; right: parent.right; topMargin: 4 } text: starChip.tipText; hovered: starMouse.containsMouse }
                }
            }
        }

        Column {
            id: info
            visible: !card.isNew
            anchors { left: parent.left; right: parent.right; top: preview.bottom; margins: 10; topMargin: 8 }
            spacing: 3
            Label { width: parent.width; text: card.name; font.bold: true }
            Label {
                id: byLine
                width: parent.width - (pencil.visible ? pencil.width + 6 : 0)
                elide: Text.ElideRight
                dim: true
                font.pixelSize: 11
                text: card.trashed ? card.removed
                    : card.shaderState === "unreadable" ? card.slug
                    : card.shaderState === "error" ? qsTr("Draft · line %1").arg(card.errorLine)
                    : card.hidden && !card.hot ? qsTr("Hidden")
                    : card.hot ? qsTr("%1% · %2 fps").arg(card.quality).arg(card.fps) + (card.lively >= 0 ? " · " + qsTr("lively") + ":" + card.lively + "%" : "")
                    : [card.author ? qsTr("by %1").arg(card.author) : "",
                       card.lively >= 0 ? qsTr("lively") + ":" + card.lively + "%" : ""].filter(Boolean).join(" · ")
            }
        }
    }

    // Edit pencil: bottom-right of the info strip, its box's bottom on the "by …"
    // baseline, nudged up 1 px, 12 px from the right. Highlighted or hovered card only.
    Item {
        id: pencil
        objectName: "editPencil"
        readonly property bool hot: pencilMouse.containsMouse
        visible: !card.isNew && !card.trashed && (card.hot || hover.hovered)
        width: 16; height: 16
        x: frame.x + frame.width - 12 - width
        y: frame.y + info.y + byLine.y + byLine.baselineOffset - height - 1
        Icon {
            id: pencilGlyph
            anchors.centerIn: parent
            visible: nerd
            glyph: "\u{F0CB6}" // mdi pencil-outline
            size: 14
            color: pencil.hot ? ui.accent : ui.muted
        }
        Canvas {   // fallback without a Nerd Font; joins follow rIcon (0 = miter)
            anchors.fill: parent
            visible: !pencilGlyph.nerd
            property color ink: pencil.hot ? ui.accent : ui.muted
            readonly property int rIcon: theme.rIcon
            onInkChanged: requestPaint()
            onRIconChanged: requestPaint()
            onPaint: {
                // Outlined pencil, ~14 px on the diagonal, 1.5 px stroke, tip bottom-left.
                const c = getContext("2d")
                c.reset()
                c.strokeStyle = ink; c.lineWidth = 1.5; c.lineJoin = rIcon > 0 ? "round" : "miter"; c.lineCap = rIcon > 0 ? "round" : "square"
                c.translate(8, 8); c.rotate(-Math.PI / 4)
                c.beginPath()
                c.moveTo(-8, 0); c.lineTo(-4.5, -2); c.lineTo(7, -2); c.lineTo(7, 2); c.lineTo(-4.5, 2); c.closePath()
                c.moveTo(4.5, -2); c.lineTo(4.5, 2)   // eraser band
                c.moveTo(-4.5, -2); c.lineTo(-4.5, 2) // where the wood starts
                c.stroke()
            }
        }
        MouseArea {
            id: pencilMouse
            anchors.fill: parent; anchors.margins: -3 // a slightly bigger target than the 16 px glyph
            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: { card.picked(); card.editRequested() }
        }
        Tip { anchors { top: parent.bottom; right: parent.right; topMargin: 4 } text: qsTr("Edit"); hovered: pencilMouse.containsMouse }
    }

    // Rounded corners: the preview (still or live shader) runs to the card's
    // top edge (1 px inset for the outline, which covers that pixel), so it is
    // clipped to R, the card's own radius, and the two share their corners.
    // An inset preview would use theme.rInner(padding). R = 0: no clipping.
    // Two tiny painted corner masks instead of a MultiEffect mask: no extra
    // offscreen layer per card, which keeps weak GPUs fast.
    Repeater {
        model: card.isNew ? 0 : 2
        Canvas {
            required property int index
            readonly property int r: theme.rounding
            visible: r > 0
            onRChanged: requestPaint()
            x: index === 0 ? frame.x : frame.x + frame.width - r; y: frame.y
            width: r; height: r
            transform: Scale { origin.x: width / 2; xScale: index === 0 ? 1 : -1 }
            property color fill: ui.bg
            onFillChanged: requestPaint()
            onPaint: {
                const c = getContext("2d")
                c.reset()
                c.fillStyle = fill
                c.beginPath(); c.moveTo(0, 0); c.lineTo(r, 0)
                c.arc(r, r, r, 1.5 * Math.PI, Math.PI, true)
                c.closePath(); c.fill()
            }
        }
    }
    Rectangle {
        // Focus keeps the accent outline; selection is a muted one, so the two never look the same.
        anchors.fill: frame
        color: "transparent"
        radius: theme.rounding
        border.width: card.hot || card.isNew || card.selected ? 1 : 0
        border.color: card.hot ? ui.accent : card.selected ? ui.muted : Qt.rgba(ui.muted.r, ui.muted.g, ui.muted.b, 0.35)
    }

    MouseArea {
        id: mouse
        z: -1
        anchors.fill: parent
        hoverEnabled: true; cursorShape: Qt.PointingHandCursor; 
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: (m) => {
            card.picked()
            if (m.button === Qt.RightButton && !card.isNew) card.menuRequested(m.x, m.y)
            else if (card.selecting && !card.isNew) card.toggled()
            else if (!card.trashed) card.activated() // one click runs it (Trash: just focus)
        }
    }
}
