import QtQuick
import QtQuick.Controls as QQC

// 2. Editor: Name, Author, License (optional), Description, the code with
// line numbers, and a preview of the last successful compile. Compiling is
// explicit (Ctrl+Enter); Save compiles first and saves a broken shader as a
// draft. Esc asks before throwing away unsaved changes.
FocusScope {
    id: ed
    property string slug: ""
    property var saved: ({})
    property url previewUrl: ""
    property string compiledCode: ""
    property bool compiling: false
    property bool lastOk: false
    property int errorLine: 0
    property string errorText: ""
    property var errors: []          // every error from the last compile: {line, text}
    property bool errorsOpen: true   // the errors pane; starts collapsed when there's one error
    readonly property string errorSummary: {
        const n = errors.length, l = n ? errors[0].line : 0
        if (n === 1) return l > 0 ? qsTr("1 error · line %1").arg(l) : qsTr("1 error")
        return l > 0 ? qsTr("%1 errors · first at line %2").arg(n).arg(l) : qsTr("%1 errors").arg(n)
    }
    function sourceLine(n) {
        const l = code.text.split("\n")[n - 1]
        return l === undefined ? "" : l.trim()
    }
    readonly property int focusedLine: errList.activeFocus && errors[errList.currentIndex] && upToDate
                                       ? errors[errList.currentIndex].line : 0
    // Scroll the code to the focused error's line; the cursor only moves on ↵.
    onFocusedLineChanged: if (focusedLine > 0) {
        const y = code.y + lineY(focusedLine)
        flick.ensureVisible(Qt.rect(0, y, 1, code.lineH))
    }
    readonly property string focusedError: errList.activeFocus && errors[errList.currentIndex] ? errors[errList.currentIndex].text : ""
    readonly property var errorLines: errors.map(x => x.line)
    function gotoLine(n) {
        if (n <= 0) return
        let pos = 0
        for (let i = 1; i < n; ++i) { const k = code.text.indexOf("\n", pos); if (k < 0) break; pos = k + 1 }
        code.forceActiveFocus()
        code.cursorPosition = pos
        flick.ensureVisible(code.cursorRectangle)
    }
    property bool pendingSave: false
    property bool licenseTouched: false
    property var channels: [{}, {}, {}, {}]   // {source, filter, wrap}
    property string savedChannels: ""
    function chJson(c) { return JSON.stringify(c.map(x => [x.source || "", x.filter || "linear", x.wrap || "repeat"])) }
    function chInfo(i) {
        const c = channels[i] || {}
        const m = library.channelInfo(c.source || "", slug)
        m.filter = c.filter || "linear"; m.wrap = c.wrap || "repeat"
        return m
    }
    readonly property var previewChannels: [chInfo(0), chInfo(1), chInfo(2), chInfo(3)]
    signal closed(string slug)

    readonly property bool dirty: name.text !== saved.name || author.text !== saved.author
        || license.text !== saved.license || description.text !== saved.description || code.text !== saved.code
        || chJson(channels) !== savedChannels
    property string compiledKey: ""
    readonly property bool upToDate: compiledKey === code.text + typesOf(channels) && compiledCode !== ""
    function typesOf(c) { return c.map(x => x.source === "grey3d" ? "3" : "2").join("") }
    readonly property bool broken: upToDate && !lastOk

    readonly property string template: "void mainImage(out vec4 fragColor, in vec2 fragCoord)\n{\n"
        + "    vec2 uv = fragCoord / iResolution.xy;\n"
        + "    vec3 col = 0.5 + 0.5 * cos(iTime + uv.xyx + vec3(0, 2, 4));\n"
        + "    fragColor = vec4(col, 1.0);\n}\n"

    function openShader(s) {
        const m = s ? library.get(library.rowOf(s)) : ({})
        slug = s || ""
        saved = { name: m.name || "", author: m.author || "", license: m.license || "",
                  description: m.description || "", code: s ? library.codeOf(s) : template }
        licenseTouched = true // no pre-fill while loading
        const ch = s ? library.channelsOf(s) : []
        channels = [0, 1, 2, 3].map(i => ch[i] ? { source: ch[i].source, filter: ch[i].filter, wrap: ch[i].wrap }
                                                : { source: "", filter: "linear", wrap: "repeat" })
        savedChannels = chJson(channels)
        name.text = saved.name; author.text = saved.author; license.text = saved.license
        description.text = saved.description; code.text = saved.code
        licenseTouched = saved.license !== ""
        previewUrl = ""; compiledCode = ""; errorLine = 0; errorText = ""; errors = []; lastOk = false; pendingSave = false
        compile()
        name.forceActiveFocus()
    }
    function compile() { compiling = true; pendingCode = code.text; pendingKey = code.text + typesOf(channels); library.compileDraft(code.text, channels) }
    property string pendingKey: ""
    property string pendingCode: ""
    function save() { pendingSave = true; compile() }
    function cancel() { if (dirty) discard.open(); else closed(slug) }

    Connections {
        target: library
        function onDraftCompiled(ok, url, line, text, src, types, errs) {
            if (!ed.visible || src + types !== ed.pendingKey) return // a stale result
            ed.compiling = false
            ed.compiledCode = src
            ed.compiledKey = ed.pendingKey
            ed.lastOk = ok
            ed.errorLine = ok ? 0 : line
            ed.errorText = ok ? "" : text
            ed.errors = ok ? [] : (errs && errs.length ? errs : [{ line: line, text: text }])
            ed.errorsOpen = ed.errors.length > 1
            if (ok) ed.previewUrl = url // a broken edit never blanks the preview
            if (ed.pendingSave) {
                ed.pendingSave = false
                const s = library.saveShader(ed.slug, { name: name.text, author: author.text, license: license.text,
                                                        description: description.text, channels: ed.channels }, code.text)
                if (s !== "") { ed.slug = s; ed.saved = { name: name.text, author: author.text, license: license.text,
                                                          description: description.text, code: code.text }
                                ed.savedChannels = ed.chJson(ed.channels); ed.closed(s) }
            }
        }
    }

    Rectangle { anchors.fill: parent; color: ui.bg }

    Row {
        id: head
        x: 20; y: 16; spacing: 10
        Label { text: name.text || (ed.slug ? (ed.saved.name || "") : qsTr("New shader")); font.bold: true; font.pixelSize: 17
                width: Math.min(implicitWidth, ed.width - 80) }
        Rectangle { visible: ed.dirty; width: 7; height: 7; radius: 3.5; color: ui.accent; anchors.verticalCenter: parent.verticalCenter }
    }
    Rectangle { x: 20; y: 50; width: parent.width - 40; height: 1; color: Qt.rgba(ui.muted.r, ui.muted.g, ui.muted.b, 0.25) }

    Column {
        id: fields
        x: 20; y: 62; width: parent.width - 40
        spacing: 10
        Row {
            width: parent.width; spacing: 10
            readonly property real w: (width - 20) / 3
            Field { id: name; objectName: "nameField"; width: parent.w; label: qsTr("Name"); KeyNavigation.tab: author }
            Field { id: author; width: parent.w; label: qsTr("Author"); KeyNavigation.tab: license; KeyNavigation.backtab: name }
            Field { id: license; width: parent.w; label: qsTr("License (optional)"); KeyNavigation.tab: description; KeyNavigation.backtab: author
                    input.onTextEdited: ed.licenseTouched = true }
        }
        Field { id: description; width: parent.width; label: qsTr("Description"); KeyNavigation.tab: code; KeyNavigation.backtab: license }
    }

    Item {
        id: body
        anchors { left: parent.left; right: parent.right; top: fields.bottom; bottom: footer.top; margins: 20; topMargin: 14 }
        readonly property real previewW: Math.round(width * 0.36)

        Label { id: codeLabel; text: qsTr("Fragment shader"); smallCaps: true; dim: true }
        Rectangle {
            id: codeBox
            anchors { left: parent.left; top: codeLabel.bottom; topMargin: 6; bottom: compileRow.top; bottomMargin: 8 }
            width: parent.width - body.previewW - 14
            color: ui.bg2; radius: theme.rounding; clip: true
            border.width: 1; border.color: code.activeFocus ? ui.accent : "transparent"
            Flickable {
                id: flick
                anchors.fill: parent; anchors.margins: 1
                contentWidth: width; contentHeight: code.contentHeight + 16
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                function ensureVisible(r) {
                    if (contentY >= r.y) contentY = r.y
                    else if (contentY + height <= r.y + r.height + 16) contentY = r.y + r.height + 16 - height
                }
                // The focused error's line (errors list has focus): faint accent tint, behind the text.
                Rectangle {
                    objectName: "focusLineTint"
                    visible: ed.focusedLine > 0
                    y: code.y + (ed.focusedLine > 0 ? ed.lineY(ed.focusedLine) : 0); width: parent.width; height: code.lineH
                    color: Qt.rgba(ui.accent.r, ui.accent.g, ui.accent.b, 0.09)
                }
                // Error line: faint tint and a 2 px red bar (the only red in the app).
                Repeater {
                    model: ed.upToDate ? ed.errors.filter(x => x.line > 0) : []
                    Rectangle {
                        required property var modelData
                        y: code.y + ed.lineY(modelData.line); width: parent.width; height: code.lineH
                        color: "transparent" // one red: just the 2 px bar and the gutter number
                        Rectangle { width: 2; height: parent.height; color: theme.error }
                    }
                }
                Column {
                    id: gutter
                    x: 0; y: code.y; width: 34
                    Repeater {
                        model: code.lineCount
                        Label { required property int index; width: gutter.width - 8; height: code.lineH
                                horizontalAlignment: Text.AlignRight; text: index + 1; font.pixelSize: 11
                                color: ed.upToDate && ed.errorLines.indexOf(index + 1) >= 0 ? theme.error : ui.muted }
                    }
                }
                TextEdit {
                    id: code
                    objectName: "code"
                    x: 42; y: 8; width: flick.width - 50
                    readonly property real lineH: lineCount > 0 ? contentHeight / lineCount : 16
                    color: ui.fg; selectionColor: Qt.rgba(ui.accent.r, ui.accent.g, ui.accent.b, 0.35)
                    font.family: theme.mono; font.pixelSize: 12
                    wrapMode: TextEdit.NoWrap
                    selectByMouse: true
                    textFormat: TextEdit.PlainText
                    KeyNavigation.tab: compileBtn; KeyNavigation.backtab: description
                    onCursorRectangleChanged: flick.ensureVisible(cursorRectangle)
                    Keys.onPressed: (e) => {
                        if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter) && (e.modifiers & Qt.ControlModifier)) {
                            ed.compile(); e.accepted = true
                        }
                    }
                    onTextChanged: if (!ed.licenseTouched) { const h = library.licenseHint(text); if (h) license.text = h }
                }
            }
        }
        Item {
            id: compileRow
            anchors { left: codeBox.left; right: codeBox.right; bottom: results.top; bottomMargin: 10 }
            height: 30
            Button { id: compileBtn; anchors.right: parent.right; text: qsTr("Compile"); keyHint: "Ctrl+↵"
                     onClicked: ed.compile(); KeyNavigation.tab: ed.broken ? errHead : slots.itemAt(0)
                     KeyNavigation.backtab: code }
        }

        // Compile result: full width under everything, flat on the background.
        // OK / compiling / not compiled is one quiet line. With errors: a summary
        // header ("3 errors · first at line 12", ▾ to collapse), a 1 px divider and
        // fixed 25 px rows (gutter-style number + one elided message). The focused
        // row's full message shows in the hint row. Up to 25% of the page, then scrolls.
        Item {
            id: results
            objectName: "results"
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            readonly property real rowH: 25
            readonly property real maxH: Math.max(errHead.height + 1 + rowH * 2, Math.round(body.height * 0.25))
            height: !ed.broken ? quiet.height
                  : !ed.errorsOpen ? errHead.height
                  : Math.min(maxH, errHead.height + 1 + errList.contentHeight)
            Label {
                id: quiet
                visible: !ed.broken
                width: parent.width; height: 24; verticalAlignment: Text.AlignVCenter
                font.pixelSize: 12; dim: true
                text: ed.compiling ? qsTr("Compiling…") : ed.upToDate ? qsTr("Compiled · 0 errors") : qsTr("Not compiled")
            }
            FocusScope {
                id: errHead
                objectName: "errorsHeader"
                visible: ed.broken
                width: parent.width; height: 26
                activeFocusOnTab: false
                KeyNavigation.tab: ed.errorsOpen ? errList : slots.itemAt(0); KeyNavigation.backtab: compileBtn
                Keys.onReturnPressed: ed.errorsOpen = !ed.errorsOpen
                Keys.onEnterPressed: ed.errorsOpen = !ed.errorsOpen
                Keys.onSpacePressed: ed.errorsOpen = !ed.errorsOpen
                Rectangle { anchors.fill: parent; color: "transparent"; radius: theme.rounding
                            border.width: errHead.activeFocus ? 1 : 0; border.color: ui.accent }
                Label {
                    x: 12; width: parent.width - 12 - toggle.width - 8
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: 12
                    text: ed.errorSummary
                }
                Label {
                    id: toggle
                    anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                    text: ed.errorsOpen ? "▾" : "▸"; dim: true; font.pixelSize: 12
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: ed.errorsOpen = !ed.errorsOpen }
            }
            Rectangle { visible: ed.broken && ed.errorsOpen; y: errHead.height; width: parent.width; height: 1
                        color: Qt.rgba(ui.muted.r, ui.muted.g, ui.muted.b, 0.25) }
            ListView {
                id: errList
                objectName: "errorList"
                visible: ed.broken && ed.errorsOpen
                anchors { left: parent.left; right: parent.right; top: errHead.bottom; topMargin: 1; bottom: parent.bottom }
                clip: true
                spacing: 0
                boundsBehavior: Flickable.StopAtBounds
                model: ed.broken ? ed.errors : []
                currentIndex: 0
                activeFocusOnTab: false
                KeyNavigation.tab: slots.itemAt(0); KeyNavigation.backtab: errHead
                Keys.onReturnPressed: ed.gotoLine(ed.errors[currentIndex].line)
                Keys.onEnterPressed: ed.gotoLine(ed.errors[currentIndex].line)
                // The focused row (and only it) expands in place: full message, wrapped
                // (max 4 lines, tooltip for the rest), and the source line under it.
                onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
                Keys.onEscapePressed: code.forceActiveFocus()
                Keys.onPressed: (e) => { if (e.key === Qt.Key_Question) { edKeys.open(); e.accepted = true } }
                delegate: Item {
                    id: errRow
                    required property var modelData
                    required property int index
                    readonly property bool focused: errList.activeFocus && ListView.isCurrentItem
                    readonly property string source: modelData.line > 0 ? ed.sourceLine(modelData.line) : ""
                    width: errList.width
                    height: focused ? Math.max(results.rowH, 4 + msg.height + (src.visible ? 6 + src.height + 8 : 3)) : results.rowH
                    clip: true
                    Behavior on height { enabled: theme.animations; NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                    onHeightChanged: if (focused) errList.positionViewAtIndex(index, ListView.Contain)
                    Rectangle { anchors.fill: parent; visible: errRow.focused
                                color: Qt.rgba(ui.accent.r, ui.accent.g, ui.accent.b, 0.12) }
                    Rectangle { visible: errRow.focused; width: 2; height: parent.height; color: ui.accent }
                    // Same column as the code gutter: right-aligned 11 px mono, top-aligned.
                    Label {
                        id: num
                        width: 26; y: 4; height: 18; verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: Text.AlignRight
                        text: errRow.modelData.line > 0 ? errRow.modelData.line : "–"
                        color: theme.error; font.pixelSize: 11
                    }
                    Label {
                        id: msg
                        objectName: "errorMessage"
                        x: 56; y: 4; width: parent.width - 56 - 12
                        text: errRow.modelData.text
                        font.pixelSize: 12
                        lineHeightMode: Text.FixedHeight; lineHeight: 18
                        wrapMode: errRow.focused ? Text.WrapAtWordBoundaryOrAnywhere : Text.NoWrap
                        maximumLineCount: errRow.focused ? 4 : 1
                        color: errRow.focused ? ui.fg : ui.muted
                        QQC.ToolTip.visible: errRow.focused && truncated
                        QQC.ToolTip.text: errRow.modelData.text
                        QQC.ToolTip.delay: 400
                    }
                    Label {
                        id: src
                        objectName: "errorSource"
                        visible: errRow.focused && errRow.modelData.line > 0
                        x: 56; y: msg.y + msg.height + 6; width: parent.width - 56 - 12
                        text: errRow.modelData.line + " │ " + errRow.source
                        font.pixelSize: 11; dim: true
                    }
                    MouseArea { anchors.fill: parent; cursorShape: errRow.modelData.line > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: { errList.currentIndex = errRow.index; ed.gotoLine(errRow.modelData.line) } }
                }
            }
        }

        Label { id: prevLabel; x: codeBox.width + 14; text: qsTr("Preview · last good compile"); smallCaps: true; dim: true
                width: body.previewW }
        Rectangle {
            id: previewBox
            x: codeBox.width + 14
            anchors { top: prevLabel.bottom; topMargin: 6 }
            width: body.previewW
            // Shrinks so the four channel slots always fit above the results pane.
            height: Math.max(40, Math.min(Math.round(width * 0.9), results.y - 14 - y - 14 - chBlock.height))
            color: ui.bg2; radius: theme.rounding; clip: true
            ShaderView {
                anchors.fill: parent
                source: ed.visible ? ed.previewUrl : ""
                channels: ed.previewChannels
                quality: 0.6
                fps: 30
                running: ed.visible
            }
            Label { anchors.centerIn: parent; visible: ed.previewUrl == ""; text: ed.compiling ? qsTr("Preparing…") : qsTr("Not compiled")
                    dim: true; font.pixelSize: 11 }
        }
    }

    // CHANNELS: four slots stacked under the preview (one per row, so labels fit).
    Item {
        id: chBlock
        parent: body
        x: codeBox.width + 14
        y: previewBox.y + previewBox.height + 14
        width: body.previewW
        height: chLabel.height + 6 + 4 * 40 + 3 * 6
        Label { id: chLabel; text: qsTr("Channels"); smallCaps: true; dim: true }
        Grid {
            y: chLabel.height + 6
            columns: 1; spacing: 6
            Repeater {
                id: slots
                model: 4
                Rectangle {
                    required property int index
                    readonly property var info: ed.previewChannels[index]
                    width: chBlock.width; height: 40
                    color: ui.bg2; radius: theme.rounding
                    border.width: activeFocus || (picker.visible && picker.slot === index) ? 1 : 0; border.color: ui.accent
                    activeFocusOnTab: false
                    KeyNavigation.tab: index < 3 ? slots.itemAt(index + 1) : saveBtn
                    KeyNavigation.backtab: index > 0 ? slots.itemAt(index - 1) : compileBtn
                    Keys.onReturnPressed: ed.openPicker(index, this)
                    Keys.onEnterPressed: ed.openPicker(index, this)
                    Keys.onSpacePressed: ed.openPicker(index, this)
                    Rectangle {
                        id: thumb
                        x: 6; anchors.verticalCenter: parent.verticalCenter
                        width: 30; height: 30; color: ui.bg; radius: Math.min(theme.rounding, 4); clip: true
                        border.width: info.source ? 0 : 1; border.color: Qt.rgba(ui.muted.r, ui.muted.g, ui.muted.b, 0.4)
                        Image { anchors.fill: parent; visible: !!info.source; source: info.source ? info.url : ""
                                fillMode: Image.PreserveAspectCrop; sourceSize: Qt.size(80, 80); smooth: info.filter !== "nearest" }
                        Label { anchors.centerIn: parent; visible: !info.source; text: "+"; dim: true }
                    }
                    Column {
                        anchors { left: thumb.right; leftMargin: 8; right: parent.right; rightMargin: 6; verticalCenter: parent.verticalCenter }
                        spacing: 2
                        Label { width: parent.width; text: "iChannel" + index; smallCaps: true; dim: true; font.pixelSize: 9 }
                        Label { width: parent.width; text: info.label || ""; font.pixelSize: 11; dim: !info.source }
                    }
                    MouseArea { anchors.fill: parent; onClicked: { parent.forceActiveFocus(); ed.openPicker(index, parent) } }
                }
            }
        }
    }
    function openPicker(i, item) {
        const p = item.mapToItem(ed, 0, item.height + 4)
        picker.open(i, channels[i], p.x - 60, p.y)
    }
    function pickerFor(i) { slots.itemAt(i).forceActiveFocus(); openPicker(i, slots.itemAt(i)) }
    ChannelPicker {
        id: picker
        objectName: "picker"
        z: 10
        onPicked: (i, v) => {
            const c = ed.channels.slice()
            c[i] = { source: v.source, filter: v.filter, wrap: v.wrap }
            ed.channels = c
            // Sampler type changes need a recompile; filter/wrap don't.
            ed.compile()
        }
        onVisibleChanged: if (!visible && slot >= 0 && slots.itemAt(slot)) slots.itemAt(slot).forceActiveFocus()
    }

    Rectangle {
        id: footer
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 54
        color: ui.bg
        Rectangle { width: parent.width; height: 1; color: ui.bg2 }
        // Hint row: keys only, one line.
        Hint {
            id: footLeft
            x: 20; anchors.verticalCenter: parent.verticalCenter
            width: footRight.x - 20 - 16
            singleLine: true
            keys: ed.broken && ed.errorsOpen
                  ? [["↵", qsTr("jump to line")], ["↑↓", qsTr("next error")], ["Tab", qsTr("leave")], ["?", qsTr("keys")]]
                  : [["Esc", qsTr("cancel")]]
        }
        Row {
            id: footRight
            anchors { right: parent.right; rightMargin: 20; verticalCenter: parent.verticalCenter }
            spacing: 16
            Label { visible: ed.broken && ed.width >= 560; text: qsTr("Saves as draft"); dim: true; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
            Button { id: saveBtn; primary: true; text: qsTr("Save"); onClicked: ed.save()
                     KeyNavigation.tab: name; KeyNavigation.backtab: slots.itemAt(3) }
        }
    }

    function lineY(n) {
        // y of line n (1-based) inside the TextEdit
        let pos = 0
        for (let i = 1; i < n; ++i) { const k = code.text.indexOf("\n", pos); if (k < 0) break; pos = k + 1 }
        return code.positionToRectangle(pos).y
    }

    KeySheet { id: edKeys; objectName: "editorKeys"; z: 20; onVisibleChanged: if (!visible) errList.forceActiveFocus() }

    Confirm {
        id: discard
        title: qsTr("Discard changes?")
        line: qsTr("Your edits aren't saved.")
        detail: ""
        actionText: qsTr("Discard")
        cancelText: qsTr("Keep editing")
        hintAction: qsTr("discard")
        onAccepted: ed.closed(ed.slug)
        onCanceled: code.forceActiveFocus()
    }

    Keys.onPressed: (e) => {
        if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter) && (e.modifiers & Qt.ControlModifier)) ed.compile()
        else if (e.key === Qt.Key_S && (e.modifiers & Qt.ControlModifier)) ed.save()
        else if (e.key === Qt.Key_Escape) ed.cancel()
        else return
        e.accepted = true
    }
}
