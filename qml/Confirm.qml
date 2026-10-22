import QtQuick

// Shared confirmation: square box on a dark scrim. The action the user chose
// has focus first (decided by Allan, Oct 21). Enter does the focused button,
// Tab switches, Esc cancels. Neither button is accent-filled or red.
FocusScope {
    id: dlg
    property string title: ""
    property string line: ""
    property string detail: qsTr("This can't be undone.")
    property string actionText: qsTr("Delete forever")
    property string cancelText: qsTr("Cancel")
    property string hintAction: qsTr("delete")
    signal accepted()
    signal canceled()
    visible: false
    anchors.fill: parent

    function open() { visible = true; action.forceActiveFocus() }
    function done(ok) { visible = false; ok ? accepted() : canceled() }

    Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.55) }
    MouseArea { anchors.fill: parent }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, Math.max(320, col.implicitWidth + 40))
        height: col.implicitHeight + hint.height + 40
        color: ui.bg2
        radius: theme.rounding
        border.width: 1; border.color: Qt.rgba(ui.muted.r, ui.muted.g, ui.muted.b, 0.3)
        Column {
            id: col
            x: 20; y: 18; width: parent.width - 40
            spacing: 8
            Label { text: dlg.title; font.bold: true; font.pixelSize: 15 }
            Label { text: dlg.line; width: parent.width; wrapMode: Text.WordWrap }
            Label { text: dlg.detail; dim: true; font.pixelSize: 12; visible: text }
            Item { width: 1; height: 10 }
            Item {
                width: parent.width; height: 30
                Button { id: cancel; text: dlg.cancelText; onClicked: dlg.done(false)
                         KeyNavigation.tab: action; KeyNavigation.backtab: action }
                Button { id: action; anchors.right: parent.right; text: dlg.actionText; onClicked: dlg.done(true)
                         KeyNavigation.tab: cancel; KeyNavigation.backtab: cancel }
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: hint.height; color: Qt.darker(ui.bg2, 1.08)
            Hint { id: hint; x: 20; width: parent.width - 40; topPadding: 9; bottomPadding: 9
                   keys: [["enter", dlg.hintAction], ["tab", qsTr("switch")], ["esc", qsTr("cancel")]] } }
    }
    Keys.onEscapePressed: dlg.done(false)
    Keys.onPressed: (e) => { e.accepted = true } // modal: nothing leaks to the list
}
