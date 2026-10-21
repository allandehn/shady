import QtQuick

// Hatched placeholder for previews that can't render (broken or unreadable).
Rectangle {
    property alias label: text.text
    color: ui.bg2
    clip: true
    Canvas {
        anchors.fill: parent
        onPaint: {
            const c = getContext("2d")
            c.reset()
            c.strokeStyle = Qt.rgba(ui.muted.r, ui.muted.g, ui.muted.b, 0.25)
            c.lineWidth = 1
            for (let x = -height; x < width; x += 8) {
                c.beginPath(); c.moveTo(x, height); c.lineTo(x + height, 0); c.stroke()
            }
        }
    }
    Label { id: text; anchors.centerIn: parent; dim: true; font.pixelSize: 11 }
}
