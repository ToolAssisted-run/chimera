// Chimera's Theme window (Config > Theme):
// the themes on the left (Theme and Look; the built-in two first, then the
// files by name), a drawing of Chimera in the theme picked on the right
// (pointing at a part names the colour it is painted with), the theme's
// name, what it says of itself and who made it, a line of how many there
// are and what would not load, and Open Themes Folder, Write a Copy to
// Edit, Reload Themes, OK and Cancel. Picking only shows a theme; OK (or a
// double click) puts it on. Layout and bindings only, over the themes view
// model.
import QtQuick
import QtQuick.Controls
import ChimeraDialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "themesWindow"
    required property QtObject vm
    required property QtObject colors
    property Window mainWindow: null
    title: qsTrId("themes.theme")
    width: 900
    height: 566
    minimumWidth: 720
    minimumHeight: 480

    // the theme on when the window opened, and the one picked in the list
    property string inUse: ""
    property int picked: -1
    readonly property var themeNames: JSON.parse(win.vm.names)
    // what the picked theme says of itself
    readonly property var pickedRow: win.pickedName === "" ? null : JSON.parse(win.vm.run("about", [win.pickedName]) || "null")
    onVisibleChanged: if (visible) {
        win.inUse = win.vm.current
        win.vm.run("refresh")
        win.pick(win.inUse)
    }
    function pick(name) {
        const at = win.themeNames.findIndex(n => n.toLowerCase() === name.toLowerCase())
        if (at < 0) return
        win.picked = at
        list.positionViewAtIndex(at, ListView.Contain)
    }
    readonly property string pickedName: win.picked >= 0 && win.picked < win.themeNames.length ? win.themeNames[win.picked] : ""
    // the picked theme's colours, and the built-in Light's for what a file leaves out
    readonly property var previewed: win.pickedName === "" ? null : JSON.parse(win.vm.run("colorsOf", [win.pickedName]) || "{}")
    readonly property var fallback: JSON.parse(win.vm.run("colorsOf", ["Light"]) || "{}")
    function accept() {
        if (win.pickedName === "") return
        win.vm.run("choose", [win.pickedName])
        win.close()
    }
    // a theme's colours with the desktop's own painted, for a copy that has colours of its own
    function painted(name) {
        const raw = JSON.parse(win.vm.run("colorsOf", [name]) || "{}")
        const out = {}
        for (const role of Object.keys(raw)) {
            const c = win.colors.resolve(raw[role])
            const hex = (v) => ("0" + Math.round(v * 255).toString(16)).slice(-2).toUpperCase()
            out[role] = "#" + (c.a < 1 ? hex(c.a) : "") + hex(c.r) + hex(c.g) + hex(c.b)
        }
        return JSON.stringify(out)
    }

    MessageDialog {
        id: failuresDialog
        title: qsTrId("themes.these_theme_files_were_not_loaded")
        text: win.vm.failures
        buttons: MessageDialog.Ok
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12
            // the list: Theme and Look
            Rectangle {
                Layout.preferredWidth: 260
                Layout.fillHeight: true
                color: palette.base
                border.color: palette.mid
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 1
                    spacing: 0
                    Row {
                        Label { text: qsTrId("themes.theme"); width: 160; leftPadding: 4; topPadding: 2; bottomPadding: 2; background: Rectangle { color: palette.button; border.color: palette.mid } }
                        Label { text: qsTrId("themes.look"); width: 98; leftPadding: 4; topPadding: 2; bottomPadding: 2; background: Rectangle { color: palette.button; border.color: palette.mid } }
                    }
                    ListView {
                        boundsBehavior: Flickable.StopAtBounds
                        id: list
                        objectName: "themeList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: win.vm.rows("themes")
                        currentIndex: win.picked
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            id: theme
                            required property int index
                            required property string name
                            required property bool dark
                            required property string full
                            width: list.width
                            height: 20
                            color: theme.index === win.picked ? palette.highlight : "transparent"
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                Label { text: theme.name; width: 160; leftPadding: 4; elide: Text.ElideRight; color: theme.index === win.picked ? palette.highlightedText : palette.text }
                                Label { text: theme.dark ? qsTrId("themes.dark") : qsTrId("themes.light"); leftPadding: 4; color: theme.index === win.picked ? palette.highlightedText : palette.text }
                            }
                            ToolTip.visible: themeHover.hovered && theme.full !== ""
                            ToolTip.delay: 500
                            ToolTip.text: theme.full
                            HoverHandler { id: themeHover }
                            TapHandler {
                                onTapped: win.picked = theme.index
                                onDoubleTapped: { win.picked = theme.index; win.accept() }
                            }
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 2
                // Chimera in the theme picked, drawn: every colour from that theme and nothing else
                Canvas {
                    id: preview
                    objectName: "themePreview"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    property var theme: win.previewed
                    onThemeChanged: requestPaint()
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    // the parts drawn, each with the role it was painted with (the drawing's own coordinates)
                    property var parts: []
                    readonly property real zoom: Math.max(0.1, Math.min((width - 2) / 520, (height - 2) / 400))
                    readonly property real originX: (width - 520 * zoom) / 2
                    readonly property real originY: (height - 400 * zoom) / 2
                    function c(role) {
                        const v = (preview.theme && preview.theme[role]) || win.fallback[role] || "#FF00FF"
                        return win.colors.resolve(v)
                    }
                    onPaint: {
                        const g = getContext("2d")
                        g.reset()
                        g.fillStyle = palette.window
                        g.fillRect(0, 0, width, height)
                        const parts = []
                        if (!preview.theme) { preview.parts = parts; return }
                        g.save()
                        g.translate(originX, originY)
                        g.scale(zoom, zoom)
                        const font = "11px sans-serif"
                        g.font = font
                        g.textBaseline = "top"
                        const fill = (role, x, y, w, h) => { g.fillStyle = preview.c(role); g.fillRect(x, y, w, h); parts.push([x, y, w, h, role]) }
                        const edge = (role, x1, y1, x2, y2) => parts.push([Math.min(x1, x2) - 1, Math.min(y1, y2) - 1, Math.abs(x2 - x1) + 2, Math.abs(y2 - y1) + 2, role])
                        const line = (role, x1, y1, x2, y2) => { g.strokeStyle = preview.c(role); g.lineWidth = 1; g.beginPath(); g.moveTo(x1 + 0.5, y1 + 0.5); g.lineTo(x2 + 0.5, y2 + 0.5); g.stroke(); edge(role, x1, y1, x2, y2) }
                        const outline = (role, x, y, w, h) => { g.strokeStyle = preview.c(role); g.lineWidth = 1; g.strokeRect(x + 0.5, y + 0.5, w, h); edge(role, x, y, x + w, y); edge(role, x, y + h, x + w, y + h); edge(role, x, y, x, y + h); edge(role, x + w, y, x + w, y + h) }
                        const write = (role, text, x, y, f) => { g.font = f || font; g.fillStyle = preview.c(role); g.fillText(text, x, y); const w = g.measureText(text).width; parts.push([x, y, w, 13, role]); g.font = font; return w }
                        const bold = "bold 11px sans-serif"
                        const poly = (role, pts) => { g.fillStyle = preview.c(role); g.beginPath(); g.moveTo(pts[0][0], pts[0][1]); for (let i = 1; i < pts.length; i++) g.lineTo(pts[i][0], pts[i][1]); g.closePath(); g.fill() }
                        const mark = (role, cx, cy) => { g.fillStyle = preview.c(role); g.beginPath(); g.ellipse(cx - 6, cy - 6, 12, 12); g.fill(); parts.push([cx - 6, cy - 6, 12, 12, role]) }
                        const button = (text, x, y, w) => { fill("ButtonBackground", x, y, w, 24); outline("ButtonBorder", x, y, w - 1, 23); const tw = g.measureText(text).width; write("ButtonText", text, x + (w - tw) / 2, y + 6) }

                        fill("WindowBackground", 0, 0, 520, 400)
                        // the tool strip
                        fill("ToolStripBackground", 0, 22, 520, 26)
                        let x = 8
                        // words drawn as the window would draw them (a menu's & left out)
                        const said = (key) => qsTrId(key).replace("&", "")
                        for (const label of ["themes.preview_play", "themes.preview_pause", "themes.preview_record", "hotkey.frame_advance"]) x += write("ToolStripText", said(label), x, 29) + 16
                        // a form: fields, a drop-down, a disabled one, a read-only one, a link, coloured words, marks, a meter, buttons
                        const left = 10, fw = 236
                        write("WindowText", said("themes.preview_project_name"), left, 58)
                        fill("InputBackground", left, 74, fw, 20); outline("Border", left, 74, fw, 20); write("InputText", said("themes.preview_run_name"), left + 4, 77)
                        fill("InputBackground", left, 100, 116, 20); outline("Border", left, 100, 116, 20); write("InputText", said("themes.preview_player").arg(1), left + 4, 103)
                        poly("GlyphForeground", [[left + 100, 108], [left + 108, 108], [left + 104, 113]]); parts.push([left + 99, 106, 10, 8, "GlyphForeground"])
                        fill("DisabledBackground", left + 120, 100, 116, 20); outline("Border", left + 120, 100, 116, 20); write("DisabledText", said("themes.preview_player").arg(2), left + 124, 103)
                        poly("GlyphShadow", [[left + 220, 108], [left + 228, 108], [left + 224, 113]]); parts.push([left + 219, 106, 10, 8, "GlyphShadow"])
                        fill("ReadOnlyBackground", left, 126, fw, 20); outline("Border", left, 126, fw, 20); write("MutedText", said("themes.preview_read_only_file"), left + 4, 129)
                        const lw = write("LinkText", said("themes.open_themes_folder"), left, 152); line("LinkText", left, 164, left + lw, 164)
                        write("MutedText", said("themes.preview_note"), left + 128, 152)
                        write("AccentGood", said("themes.preview_verified"), left, 174); write("AccentReady", said("themes.preview_ready"), left + 62, 174)
                        write("AccentWarning", said("themes.preview_out_of_date"), left + 112, 174); write("AccentError", said("themes.preview_missing"), left + 190, 174)
                        fill("AccentWarningBackground", left, 194, fw, 22); write("WindowText", said("themes.preview_warning"), left + 6, 198)
                        mark("GlyphGood", left + 8, 232); write("WindowText", said("themes.preview_good"), left + 18, 225)
                        poly("GlyphWarning", [[left + 66, 225], [left + 73, 238], [left + 59, 238]]); parts.push([left + 59, 225, 14, 13, "GlyphWarning"])
                        fill("GlyphWarningInk", left + 65, 229, 2, 5); fill("GlyphWarningInk", left + 65, 235, 2, 2)
                        write("WindowText", said("themes.preview_unknown"), left + 76, 225)
                        mark("GlyphError", left + 140, 232); write("WindowText", said("themes.preview_bad"), left + 150, 225)
                        mark("GlyphNeutral", left + 190, 232); write("WindowText", said("common.none"), left + 200, 225)
                        // the level meter, the analog range box and a little hex editor
                        const top = 248
                        write("WindowText", said("themes.preview_level"), left, top)
                        fill("MeterTrough", left, top + 16, 140, 12); fill("MeterLow", left + 2, top + 18, 70, 8)
                        fill("MeterMid", left + 72, top + 18, 30, 8); fill("MeterHigh", left + 102, top + 18, 16, 8); fill("MeterPeak", left + 126, top + 16, 3, 12)
                        const box = left + fw - 76
                        fill("AnalogRangeBackground", box, top, 76, 76); fill("AnalogRangeField", box + 6, top + 6, 64, 64)
                        line("AnalogRangeAxis", box + 38, top + 6, box + 38, top + 70); line("AnalogRangeAxis", box + 6, top + 38, box + 70, top + 38)
                        g.strokeStyle = preview.c("AnalogRangeLimit"); g.beginPath(); g.ellipse(box + 10, top + 10, 56, 56); g.stroke(); parts.push([box + 10, top + 10, 4, 56, "AnalogRangeLimit"])
                        mark("AnalogRangeDot", box + 52, top + 26)
                        write("WindowText", said("hex_editor.hex"), left, top + 36)
                        fill("HexMenuBar", left, top + 52, 140, 6); fill("HexBackground", left, top + 58, 140, 30)
                        const bytes = [["00", "3F", "1A", "C0", "7E", "FF"], ["12", "00", "00", "8B", "41", "02"]]
                        for (let row = 0; row < 2; row++)
                            for (let i = 0; i < 6; i++) {
                                const bx = left + 3 + i * 22, by = top + 60 + row * 13
                                const behind = row === 0 && i === 1 ? "HexHighlight" : row === 0 && i === 3 ? "HexFreeze" : row === 0 && i === 5 ? "HexHighlightFreeze" : ""
                                if (behind !== "") fill(behind, bx - 1, by, 20, 13)
                                write("HexText", bytes[row][i], bx, by)
                            }
                        button(said("common.ok"), left + 96, 342, 66)
                        button(said("common.cancel"), left + 170, 342, 66)
                        fill("ShadedBackground", left, 342, 80, 24); write("MeterText", "-12 dB", left + 6, 347)
                        // a list: a header, rows plain, alternate, chosen, under the pointer, chosen without the focus
                        const ll = 258, lwid = 252, split = 150
                        let ly = 56
                        fill("HeaderBackground", ll, ly, lwid, 20)
                        write("HeaderText", said("common.name"), ll + 6, ly + 3, bold); write("HeaderText", said("cores.system"), ll + split + 6, ly + 3, bold)
                        line("GridLines", ll + split, ly, ll + split, ly + 20)
                        ly += 20
                        for (const r of [["Aizome", "NES", "InputBackground", "InputText"], ["Bubblegum Pop", "SNES", "AlternateRowBackground", "InputText"],
                                         [said("themes.preview_chosen_row"), "N64", "Selection", "SelectionText"], [said("themes.preview_pointed_row"), "GBA", "HoverBackground", "InputText"],
                                         [said("themes.preview_chosen_unfocused"), "PS2", "InactiveSelection", "InactiveSelectionText"]]) {
                            fill(r[2], ll, ly, lwid, 18)
                            write(r[3], r[0], ll + 6, ly + 2); write(r[3], r[1], ll + split + 6, ly + 2)
                            line("GridLines", ll, ly + 17, ll + lwid, ly + 17)
                            ly += 18
                        }
                        outline("Border", ll, 56, lwid, ly - 56)
                        // a piano roll with every kind of row
                        const pt = 178, rh = 14, fc = 52
                        const cols = [ll + fc, ll + fc + 40, ll + fc + 80, ll + fc + 120, ll + fc + 160]
                        const ph = 18 + 11 * rh
                        fill("RollBackground", ll, pt, lwid, ph)
                        fill("RollColumnBackground", ll, pt, lwid, 18)
                        write("RollText", said("common.frame"), ll + 4, pt + 2, bold)
                        const names = ["A", "B", "Up", "Start", "X"]
                        for (let i = 0; i < 5; i++) { line("RollColumnBorder", cols[i], pt, cols[i], pt + 18); write("RollText", names[i], cols[i] + 4, pt + 2, bold) }
                        const rows = [[100, "TasGreenZone", "", "RollText"], [101, "TasGreenZoneInput", "A", "RollText"], [102, "TasGreenZoneInputStated", "A B", "RollText"],
                                      [103, "TasLagZone", "", "RollText"], [104, "TasLagZoneInput", "B", "RollText"], [105, "TasMarker", "Up", "RollText"],
                                      [106, "TasCurrentFrame", "A", "RollText"], [107, "RollSelection", "Start", "RollSelectionText"],
                                      [108, "TasGreenZoneInputInvalidated", "B", "RollText"], [109, "TasAnalogEdit", "X", "RollText"]]
                        let ry = pt + 18
                        for (const r of rows) {
                            fill(r[1], ll, ry, lwid, rh)
                            write(r[3], String(r[0]), ll + 18, ry + 1)
                            for (const p of r[2].split(" ")) { const at = names.indexOf(p); if (at >= 0) write(r[3], p, cols[at] + 4, ry + 1) }
                            if (r[1] === "TasCurrentFrame") { poly("TasIconPlayback", [[ll + 6, ry + 3], [ll + 13, ry + 7], [ll + 6, ry + 11]]); parts.push([ll + 6, ry + 3, 7, 8, "TasIconPlayback"]) }
                            if (r[1] === "TasMarker") { fill("TasIconMarker", ll + 6, ry + 3, 7, 6); fill("TasIconMarker", ll + 6, ry + 3, 1.5, 9) }
                            line("RollGridLines", ll, ry + rh - 1, ll + lwid, ry + rh - 1)
                            ry += rh
                        }
                        fill("TasCursorColumn", cols[3], ry, 40, rh); fill("RollEmphasisColumn", cols[4], ry, 40, rh)
                        write("RollHintText", said("themes.preview_end_of_movie"), ll + 18, ry + 1)
                        line("RollGridLines", ll, ry + rh - 1, ll + lwid, ry + rh - 1)
                        for (let i = 0; i < 5; i++) line("RollGridLines", cols[i], pt + 18, cols[i], pt + ph)
                        fill("TasFrameColumnWash", ll, pt + 18, fc, ph - 18)
                        fill("TasAlternatePlayer", cols[2], pt + 18, 80, ph - 18)
                        outline("RollColumnBorder", ll, pt, lwid, ph)
                        // the status bar
                        fill("StatusBarBackground", 0, 376, 520, 24)
                        write("StatusBarText", said("themes.preview_status"), 8, 381)
                        g.fillStyle = preview.c("TasIconRecording"); g.beginPath(); g.ellipse(500, 382, 11, 11); g.fill(); parts.push([500, 382, 11, 11, "TasIconRecording"])
                        // the menu last, as it opens over what is under it
                        fill("MenuBackground", 0, 0, 520, 22)
                        let mx = 8
                        for (const t of ["common.file", "themes.preview_emulation", "main.view", "main.config_mnemonic", "main.tools", "main.help"].map(said)) {
                            const w = g.measureText(t).width + 12
                            if (t === said("main.config_mnemonic")) {
                                fill("MenuSelectedBackground", mx, 2, w, 18); write("MenuSelectedText", t, mx + 6, 5)
                                const items = [said("main.paths"), "", said("main.theme")]
                                let mh = 6
                                for (const it of items) mh += it === "" ? 7 : 20
                                fill("MenuBackground", mx, 22, 120, mh); outline("MenuBorder", mx, 22, 119, mh - 1)
                                let iy = 25
                                for (const it of items) {
                                    if (it === "") { line("MenuSeparator", mx + 6, iy + 3, mx + 114, iy + 3); iy += 7; continue }
                                    if (it === said("main.theme")) { fill("MenuSelectedBackground", mx + 2, iy, 116, 20); write("MenuSelectedText", it, mx + 26, iy + 3) }
                                    else write("MenuText", it, mx + 26, iy + 3)
                                    iy += 20
                                }
                            } else write("MenuText", t, mx + 6, 5)
                            mx += w
                        }
                        g.restore()
                        g.strokeStyle = palette.mid
                        g.strokeRect(originX - 0.5, originY - 0.5, 520 * zoom + 1, 400 * zoom + 1)
                        preview.parts = parts
                    }
                    // what is under the pointer: the colour it is painted with, and its value
                    property string pointedAt: ""
                    HoverHandler {
                        onPointChanged: {
                            const x = (point.position.x - preview.originX) / preview.zoom, y = (point.position.y - preview.originY) / preview.zoom
                            let said = ""
                            for (let i = preview.parts.length - 1; i >= 0; i--) {
                                const p = preview.parts[i]
                                if (x >= p[0] && x <= p[0] + p[2] && y >= p[1] && y <= p[1] + p[3]) {
                                    const col = preview.c(p[4])
                                    said = p[4] + " " + col.toString().toUpperCase()
                                    break
                                }
                            }
                            preview.pointedAt = said
                        }
                    }
                    ToolTip.visible: preview.pointedAt !== ""
                    ToolTip.text: preview.pointedAt
                }
                Label {
                    text: win.pickedName === "" ? "" : win.pickedName + (win.pickedName.toLowerCase() === win.inUse.toLowerCase() ? qsTrId("themes.in_use") : "")
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    text: win.pickedRow ? win.pickedRow.about : ""
                    elide: Text.ElideRight
                    maximumLineCount: 2
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                }
                Label {
                    text: !win.pickedRow ? "" : (win.pickedRow.author !== "" ? qsTrId("themes.by").arg(win.pickedRow.author) : "")
                        + (win.pickedRow.builtIn ? qsTrId("themes.built_into_chimera") : qsTrId("themes.file").arg(win.pickedRow.origin))
                    opacity: 0.7
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }
        // how many, from where, and a link to why some would not load
        Label {
            objectName: "themeStatus"
            text: win.vm.failures === "" || !win.vm.status.endsWith("why?") ? win.vm.status
                : win.vm.status.substring(0, win.vm.status.length - 4) + "<a href=\"why\">why?</a>"
            textFormat: win.vm.failures === "" ? Text.PlainText : Text.StyledText
            color: win.vm.failures === "" ? palette.windowText : "#c62828"
            onLinkActivated: failuresDialog.open()
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        RowLayout {
            Button {
                text: qsTrId("themes.open_themes_folder"); Layout.preferredWidth: 140
                onClicked: Qt.openUrlExternally(localFiles.folderUrl(win.vm.folder))
            }
            Button {
                text: qsTrId("themes.write_a_copy_to_edit"); Layout.preferredWidth: 150
                onClicked: {
                    const source = win.pickedName !== "" ? win.pickedName : win.inUse
                    const made = win.vm.run("writeCopy", [source, win.painted(source)])
                    if (made) win.pick(made)
                }
            }
            Button {
                text: qsTrId("themes.reload_themes"); Layout.preferredWidth: 120
                onClicked: {
                    const keep = win.pickedName
                    win.vm.run("refresh")
                    win.pick(keep !== "" ? keep : win.vm.current)
                }
            }
            Item { Layout.fillWidth: true }
            Button { text: qsTrId("common.ok"); Layout.preferredWidth: 90; onClicked: win.accept() }
            Button { text: qsTrId("common.cancel"); Layout.preferredWidth: 90; onClicked: win.close() }
        }
    }
}
