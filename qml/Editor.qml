import QtQuick

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
        previewUrl = ""; compiledCode = ""; errorLine = 0; errorText = ""; lastOk = false; pendingSave = false
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
        function onDraftCompiled(ok, url, line, text, src, types) {
            if (!ed.visible || src + types !== ed.pendingKey) return // a stale result
            ed.compiling = false
            ed.compiledCode = src
            ed.compiledKey = ed.pendingKey
            ed.lastOk = ok
            ed.errorLine = ok ? 0 : line
            ed.errorText = ok ? "" : text
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
            anchors { left: parent.left; top: codeLabel.bottom; topMargin: 6; bottom: status.top; bottomMargin: 8 }
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
                // Error line: faint tint and a 2 px red bar (the only red in the app).
                Rectangle {
                    visible: ed.errorLine > 0 && ed.upToDate
                    y: code.y + ed.lineY(ed.errorLine); width: parent.width; height: code.lineH
                    color: Qt.rgba(theme.error.r, theme.error.g, theme.error.b, 0.10)
                    Rectangle { width: 2; height: parent.height; color: theme.error }
                }
                Column {
                    id: gutter
                    x: 0; y: code.y; width: 34
                    Repeater {
                        model: code.lineCount
                        Label { required property int index; width: gutter.width - 8; height: code.lineH
                                horizontalAlignment: Text.AlignRight; text: index + 1; font.pixelSize: 11
                                color: index + 1 === ed.errorLine && ed.upToDate ? theme.error : ui.muted }
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
            id: status
            anchors { left: codeBox.left; right: codeBox.right; bottom: parent.bottom }
            height: 30
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                width: parent.width - compileBtn.width - 10
                Label { visible: ed.broken && ed.errorLine > 0; text: qsTr("Line %1").arg(ed.errorLine); color: theme.error; font.pixelSize: 12 }
                Label {
                    width: parent.width - 70
                    font.pixelSize: 12
                    dim: !ed.broken
                    text: ed.compiling ? qsTr("Compiling…")
                        : ed.broken ? ed.errorText
                        : ed.upToDate ? qsTr("Compiled · 0 errors") : qsTr("Not compiled")
                }
            }
            Button { id: compileBtn; anchors.right: parent.right; text: qsTr("Compile"); keyHint: "Ctrl+↵"
                     onClicked: ed.compile(); KeyNavigation.tab: slots.itemAt(0); KeyNavigation.backtab: code }
        }

        Label { id: prevLabel; x: codeBox.width + 14; text: qsTr("Preview · last good compile"); smallCaps: true; dim: true
                width: body.previewW }
        Rectangle {
            id: previewBox
            x: codeBox.width + 14
            anchors { top: prevLabel.bottom; topMargin: 6 }
            width: body.previewW; height: Math.round(width * 0.9)
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

    // CHANNELS: four slots in a 2x2 grid under the preview.
    Item {
        id: chBlock
        parent: body
        x: codeBox.width + 14
        y: previewBox.y + previewBox.height + 14
        width: body.previewW
        Label { id: chLabel; text: qsTr("Channels"); smallCaps: true; dim: true }
        Grid {
            y: chLabel.height + 6
            columns: 2; spacing: 6
            Repeater {
                id: slots
                model: 4
                Rectangle {
                    required property int index
                    readonly property var info: ed.previewChannels[index]
                    width: (chBlock.width - 6) / 2; height: 52
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
                        width: 40; height: 40; color: ui.bg; radius: Math.min(theme.rounding, 4); clip: true
                        border.width: info.source ? 0 : 1; border.color: Qt.rgba(ui.muted.r, ui.muted.g, ui.muted.b, 0.4)
                        Image { anchors.fill: parent; visible: !!info.source; source: info.source ? info.url : ""
                                fillMode: Image.PreserveAspectCrop; sourceSize: Qt.size(80, 80); smooth: info.filter !== "nearest" }
                        Label { anchors.centerIn: parent; visible: !info.source; text: "+"; dim: true }
                    }
                    Column {
                        anchors { left: thumb.right; leftMargin: 8; right: parent.right; rightMargin: 6; verticalCenter: parent.verticalCenter }
                        spacing: 2
                        Label { text: "iChannel" + index; smallCaps: true; dim: true; font.pixelSize: 9 }
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
        Row { x: 20; anchors.verticalCenter: parent.verticalCenter; spacing: 6
            Label { text: "Esc"; font.bold: true; font.pixelSize: 11 }
            Label { text: qsTr("cancel"); dim: true; font.pixelSize: 11 } }
        Row {
            anchors { right: parent.right; rightMargin: 20; verticalCenter: parent.verticalCenter }
            spacing: 16
            Label { visible: ed.broken; text: qsTr("Saves as draft"); dim: true; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
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
