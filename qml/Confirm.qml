import QtQuick

// Shared confirmation, matching Omarchy's ConfirmDialog: a box (corners R) on
// a dark scrim, one right-aligned row with Cancel first and the action (a short
// verb) last. Focus starts on the action (Allan, Oct 21). Left/Right and
// Tab/Shift+Tab switch, Enter does the focused button, Esc or a click on the
// scrim cancels. The action is red (urgent) when destructive, accent otherwise
// (replaces the Oct 21 "no colour" rule; Allan, Oct 28).
FocusScope {
    id: dlg
    property string title: ""
    property string line: ""
    property string detail: qsTr("This can't be undone.")
    property string actionText: qsTr("Delete forever")
    property string cancelText: qsTr("Cancel")
    property string hintAction: qsTr("delete")
    property bool destructive: true
    signal accepted()
    signal canceled()
    visible: false
    anchors.fill: parent

    function open() { visible = true; action.forceActiveFocus() }
    function done(ok) { visible = false; ok ? accepted() : canceled() }

    Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.55) }
    MouseArea { anchors.fill: parent; onClicked: dlg.done(false) } // click outside cancels

    Rectangle {
        objectName: "confirmBox"
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, Math.max(320, col.implicitWidth + 40))
        height: col.implicitHeight + hint.height + 40
        color: ui.bg2
        radius: theme.rounding
        border.width: 1; border.color: Qt.rgba(ui.muted.r, ui.muted.g, ui.muted.b, 0.3)
        MouseArea { anchors.fill: parent } // clicks on the box itself don't cancel
        Column {
            id: col
            x: 20; y: 18; width: parent.width - 40
            spacing: 8
            Label { text: dlg.title; font.bold: true; font.pixelSize: 15 }
            Label { text: dlg.line; width: parent.width; wrapMode: Text.WordWrap }
            Label { text: dlg.detail; dim: true; font.pixelSize: 12; visible: text }
            Item { width: 1; height: 10 }
            Row {
                anchors.right: parent.right
                spacing: 8
                Button { id: cancel; objectName: "confirmCancel"; text: dlg.cancelText; onClicked: dlg.done(false)
                         KeyNavigation.tab: action; KeyNavigation.backtab: action
                         KeyNavigation.right: action; KeyNavigation.left: action }
                Button { id: action; objectName: "confirmAction"; text: dlg.actionText; onClicked: dlg.done(true)
                         tone: dlg.destructive ? "urgent" : "accent"
                         KeyNavigation.tab: cancel; KeyNavigation.backtab: cancel
                         KeyNavigation.right: cancel; KeyNavigation.left: cancel }
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: hint.height; color: Qt.darker(ui.bg2, 1.08)
            Hint { id: hint; x: 20; width: parent.width - 40; topPadding: 9; bottomPadding: 9
                   keys: [["enter", dlg.hintAction], ["←→", qsTr("switch")], ["esc", qsTr("cancel")]] } }
    }
    Keys.onEscapePressed: dlg.done(false)
    Keys.onPressed: (e) => { e.accepted = true } // modal: nothing leaks to the list
}
