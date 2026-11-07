// Chimera's TAStudio Settings (TAStudio's Settings > Open settings...),
// laid out as its form: the tabs Appearance, Autosave, Paint Patterns,
// Input Roll and Misc, in Chimera's words and number ranges, and Apply and
// Cancel under them. Everything is read when the window opens and written
// only by Apply (with Chimera's warning when markers are bound to input
// while hidden columns are not edited); Cancel lets it all go. Set Colors
// is Chimera's Edit TAStudio Colors (its twelve colours, OK, Cancel,
// Defaults), whose answer Apply also writes. Layout and bindings only,
// over the session, pianoRoll and branches view models.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ChimeraDialogs

ApplicationWindow {
    id: win
    objectName: "tastudioSettingsWindow"
    required property QtObject session
    required property QtObject pianoRoll
    required property QtObject branches
    // the roll's colours as the theme has them: {"TasCurrentFrame": "#rrggbb", ...}
    property var themeColorsOf: () => "{}"
    property Window ownerWindow: null
    title: qsTrId("tastudio_settings.tastudio_settings")
    width: 440
    height: 540
    minimumWidth: 440
    minimumHeight: 520
    modality: Qt.platform.os === "osx" ? Qt.WindowModal : Qt.ApplicationModal // a sheet on a Mac, which keeps an application-modal window over other apps (#541)
    transientParent: win.ownerWindow

    // ---- the copies the window edits, read when it opens ----
    property bool markersColor
    property bool markersIcons
    property bool statesColor
    property bool statesIcons
    property int lagToHide
    property bool hideWasLag
    property bool rotate
    property string rollFont
    property string rollColors
    property int autosaveSeconds
    property bool autosaveAsBackup
    property bool backupOnSave
    property bool followAlways
    property string followScroll
    property int wheelRows
    property bool scrollSync
    property bool editHidden
    property bool bindMarkers
    property bool pauseAtEnd
    property bool branchDoubleClick
    property int branchPreviewSide
    property bool oldBranches
    property bool copyFrameNumbers
    property int maxUndo
    property int rewindStep
    property int rewindStepFast
    property bool markersKeepStates
    property string patternMode
    property string patternKind
    // each control's pattern: {name: {runs: [{v, n}], loop, skipLag, button, min, max, neutral, text}}
    property var patterns: ({})

    onVisibleChanged: if (visible) win.load()
    function load() {
        const r = win.pianoRoll, s = win.session, b = win.branches
        win.markersColor = r.denoteMarkersColor; win.markersIcons = r.denoteMarkersIcons
        win.statesColor = r.denoteStatesColor; win.statesIcons = r.denoteStatesIcons
        win.lagToHide = r.lagFramesToHide; win.hideWasLag = r.hideWasLagFrames; win.rotate = r.horizontal
        win.rollFont = r.rollFont; win.rollColors = r.rollColors
        win.autosaveSeconds = s.autosaveSeconds; win.autosaveAsBackup = s.autosaveAsBackup; win.backupOnSave = s.backupOnSave
        win.followAlways = r.followAlways; win.followScroll = r.followScroll; win.wheelRows = r.wheelRows; win.scrollSync = r.scrollSync; win.editHidden = r.editHiddenColumns
        win.bindMarkers = r.bindMarkers; win.pauseAtEnd = s.pauseAtEnd; win.branchDoubleClick = b.loadOnDoubleClick
        win.branchPreviewSide = b.previewSide; win.oldBranches = b.oldControlScheme; win.copyFrameNumbers = r.copyFrameNumbers
        win.maxUndo = r.maxUndoSteps; win.rewindStep = s.rewindStep; win.rewindStepFast = s.rewindStepFast
        win.markersKeepStates = r.markersKeepStates; win.patternMode = r.patternMode; win.patternKind = r.patternKind
        win.patterns = {}
        win.readPatterns()
        tabs.currentIndex = 0
        patternEditor.choose(0)
    }
    // the project's patterns, those not edited here read again (a machine that opens after the window brings them)
    function readPatterns() {
        const all = Object.assign({}, win.patterns)
        for (let i = 0; i < patternRows.count; i++) {
            const p = patternRows.objectAt(i)
            if (!p || (all[p.name] && all[p.name].edited)) continue
            all[p.name] = { runs: win.parseRuns(p.runs, p.button), loop: p.loop, skipLag: p.skipLag, button: p.button,
                            min: p.min, max: p.max, neutral: p.neutral, edited: false }
        }
        win.patterns = all
        if (patternEditor.control === "" && controlBox.count > 0) patternEditor.choose(0)
    }
    // "On x2, Off x1" or "127 x2, 0 x1" as runs of {v, n}
    function parseRuns(text, button) {
        const out = []
        for (const part of text.split(",")) {
            const t = part.trim()
            if (t === "") continue
            const m = t.match(/^(.*\S)\s+x(\d+)$/)
            const value = m ? m[1] : t, n = m ? parseInt(m[2]) : 1
            out.push({ v: button ? value.toLowerCase() === "on" : parseInt(value), n: n })
        }
        if (out.length === 0) out.push({ v: button ? false : 0, n: 1 })
        return out
    }
    function runsText(p) { return p.runs.map(r => (p.button ? qsTrId(r.v ? "common.on" : "common.off") : String(r.v)) + " x" + r.n).join(", ") }

    function apply() {
        const r = win.pianoRoll
        // Chimera asks first when markers would move with input the edits cannot reach
        const wasBad = r.bindMarkers && !r.editHiddenColumns
        const isBad = win.bindMarkers && !win.editHidden
        if (isBad && !wasBad) { bindWarning.open(); return }
        win.write()
    }
    function write() {
        const r = win.pianoRoll, s = win.session, b = win.branches
        r.rollColors = win.rollColors; r.rollFont = win.rollFont
        // the patterns changed here, each as the project keeps it
        for (const name in win.patterns) {
            const p = win.patterns[name]
            if (p.edited) r.run("setPattern", [name, win.runsText(p), p.loop, p.skipLag])
        }
        r.denoteMarkersColor = win.markersColor; r.denoteMarkersIcons = win.markersIcons
        r.denoteStatesColor = win.statesColor; r.denoteStatesIcons = win.statesIcons
        r.lagFramesToHide = win.lagToHide; r.hideWasLagFrames = win.hideWasLag; r.horizontal = win.rotate
        s.autosaveSeconds = win.autosaveSeconds; s.autosaveAsBackup = win.autosaveAsBackup; s.backupOnSave = win.backupOnSave
        r.followAlways = win.followAlways; r.wheelRows = win.wheelRows; r.scrollSync = win.scrollSync; r.followScroll = win.followScroll
        r.editHiddenColumns = win.editHidden; r.bindMarkers = win.bindMarkers; s.pauseAtEnd = win.pauseAtEnd
        b.loadOnDoubleClick = win.branchDoubleClick; b.previewSide = win.branchPreviewSide; b.oldControlScheme = win.oldBranches
        r.copyFrameNumbers = win.copyFrameNumbers; r.maxUndoSteps = win.maxUndo
        s.rewindStep = win.rewindStep; s.rewindStepFast = win.rewindStepFast; r.markersKeepStates = win.markersKeepStates
        r.patternMode = win.patternMode; r.patternKind = win.patternKind
        win.close()
    }

    MessageDialog {
        id: bindWarning
        title: qsTrId("tastudio_settings.warning")
        text: qsTrId("tastudio_settings.you_have_enabled_bind_markers_to")
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: win.write()
    }

    // each control's pattern as the project has it
    Instantiator {
        id: patternRows
        model: win.pianoRoll.rows("patterns")
        delegate: QtObject {
            required property string name
            required property string runs
            required property int loop
            required property bool skipLag
            required property bool button
            required property int min
            required property int max
            required property int neutral
        }
        onObjectAdded: if (win.visible) Qt.callLater(win.readPatterns)
    }

    component Check: CheckBox { padding: 2 }
    component NumberBox: SpinBox { editable: true; Layout.preferredWidth: 100 }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: qsTrId("tastudio_settings.appearance"); width: implicitWidth }
            TabButton { text: qsTrId("tastudio_settings.autosave"); width: implicitWidth }
            TabButton { text: qsTrId("tastudio_settings.paint_patterns"); width: implicitWidth }
            TabButton { text: qsTrId("tastudio_settings.input_roll"); width: implicitWidth }
            TabButton { text: qsTrId("tastudio_settings.misc"); width: implicitWidth }
        }
        StackLayout {
            currentIndex: tabs.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true

            // ---- Appearance ----
            ColumnLayout {
                spacing: 4
                RowLayout {
                    Button { text: qsTrId("tastudio_settings.set_font"); Layout.preferredWidth: 90; onClicked: fontDialog.open() }
                    Button { text: qsTrId("tastudio_settings.set_colors"); Layout.preferredWidth: 90; onClicked: colorsWindow.openWith(win.rollColors) }
                }
                Check { text: qsTrId("tastudio_settings.denote_states_with_icons"); checked: win.statesIcons; onToggled: win.statesIcons = checked }
                Check { text: qsTrId("tastudio_settings.denote_states_with_background_color"); checked: win.statesColor; onToggled: win.statesColor = checked }
                Check { text: qsTrId("tastudio_settings.denote_markers_with_icons"); checked: win.markersIcons; onToggled: win.markersIcons = checked }
                Check { text: qsTrId("tastudio_settings.denote_markers_with_color_on_frame"); checked: win.markersColor; onToggled: win.markersColor = checked }
                Label { text: qsTrId("tastudio_settings.movie_settings"); font.bold: true; font.pointSize: Qt.application.font.pointSize * 1.2; Layout.topMargin: 6 }
                Check { text: qsTrId("tastudio_settings.rotate_input_roll"); checked: win.rotate; onToggled: win.rotate = checked }
                RowLayout {
                    Label { text: qsTrId("tastudio_settings.lag_frames_to_hide") }
                    NumberBox { from: 0; to: 10; value: win.lagToHide; onValueModified: win.lagToHide = value }
                }
                Check { text: qsTrId("tastudio_settings.hide_was_lag_frames"); checked: win.hideWasLag; onToggled: win.hideWasLag = checked }
                Item { Layout.fillHeight: true }
            }

            // ---- Autosave ----
            ColumnLayout {
                spacing: 4
                RowLayout {
                    Label { text: qsTrId("tastudio_settings.autosave_frequency") }
                    NumberBox { from: 0; to: 10000000; value: win.autosaveSeconds; onValueModified: win.autosaveSeconds = value; Layout.preferredWidth: 120 }
                    Label { text: qsTrId("tastudio_settings.seconds") }
                }
                Label { text: qsTrId("tastudio_settings.set_to_0_to_disable_autosave") }
                Check { text: qsTrId("tastudio_settings.save_as_backup_no_greenzone"); checked: win.autosaveAsBackup; onToggled: win.autosaveAsBackup = checked; Layout.topMargin: 30 }
                Check { text: qsTrId("tastudio_settings.when_manually_saving_also_save_backup"); checked: win.backupOnSave; onToggled: win.backupOnSave = checked }
                Item { Layout.fillHeight: true }
            }

            // ---- Paint Patterns ----
            ColumnLayout {
                spacing: 6
                RowLayout {
                    GroupBox {
                        title: qsTrId("tastudio_settings.use_pattern_for_painting")
                        Layout.alignment: Qt.AlignTop
                        ColumnLayout {
                            spacing: 0
                            Repeater {
                                model: [["always", qsTrId("tastudio_settings.always")], ["autofire", qsTrId("tastudio_settings.auto_fire_columns_only")], ["never", qsTrId("tastudio_settings.never")]]
                                RadioButton {
                                    required property var modelData
                                    padding: 2
                                    text: modelData[1]
                                    checked: win.patternMode === modelData[0]
                                    onToggled: if (checked) win.patternMode = modelData[0]
                                }
                            }
                        }
                    }
                    GroupBox {
                        title: qsTrId("tastudio_settings.choose_pattern")
                        Layout.alignment: Qt.AlignTop
                        ColumnLayout {
                            spacing: 0
                            Repeater {
                                model: [["hold", qsTrId("tastudio_settings.hold")], ["autofire", qsTrId("tastudio_settings.auto_fire")], ["custom", qsTrId("tastudio_settings.custom")]]
                                RadioButton {
                                    required property var modelData
                                    padding: 2
                                    text: modelData[1]
                                    checked: win.patternKind === modelData[0]
                                    onToggled: if (checked) win.patternKind = modelData[0]
                                    ToolTip.visible: hovered
                                    ToolTip.delay: 600
                                    ToolTip.text: qsTrId("tastudio_settings.you_can_set_a_column_as")
                                }
                            }
                        }
                    }
                }
                GroupBox {
                    id: patternEditor
                    title: qsTrId("tastudio_settings.set_custom_patterns_per_movie")
                    Layout.preferredWidth: 270
                    // the control picked, its runs, and the line picked (the last is "Loop to")
                    property string control: ""
                    property int line: 0
                    readonly property var pattern: win.patterns[patternEditor.control]
                    readonly property bool onLoop: patternEditor.pattern !== undefined && patternEditor.line >= patternEditor.pattern.runs.length
                    function choose(index) {
                        controlBox.currentIndex = index
                        patternEditor.control = index >= 0 && index < controlBox.count ? controlBox.textAt(index) : ""
                        patternEditor.line = 0
                    }
                    // a change to the pattern picked, kept in the window's copy
                    function edit(change) {
                        const all = Object.assign({}, win.patterns)
                        const p = Object.assign({}, all[patternEditor.control])
                        p.edited = true
                        p.runs = p.runs.map(r => ({ v: r.v, n: r.n }))
                        change(p)
                        all[patternEditor.control] = p
                        win.patterns = all
                    }
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 4
                        ComboBox {
                            id: controlBox
                            Layout.fillWidth: true
                            model: patternRows.count > 0 ? Array.from({ length: patternRows.count }, (_, i) => patternRows.objectAt(i) ? patternRows.objectAt(i).name : "") : []
                            onActivated: (i) => patternEditor.choose(i)
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 134
                            color: palette.base
                            border.color: palette.mid
                            ListView {
                                boundsBehavior: Flickable.StopAtBounds
                                id: runList
                                anchors.fill: parent
                                anchors.margins: 1
                                clip: true
                                ScrollBar.vertical: ScrollBar {}
                                // "0: On  (x2)" a run, from the frame it starts on; "Loop to: n" last
                                model: {
                                    const p = patternEditor.pattern
                                    if (!p) return []
                                    const out = []
                                    let at = 0
                                    for (const r of p.runs) {
                                        out.push(at + ": " + (p.button ? (r.v ? qsTrId("tastudio_settings.on") : qsTrId("tastudio_settings.off")) : String(r.v)) + "\t(x" + r.n + ")")
                                        at += r.n
                                    }
                                    out.push(qsTrId("tastudio_settings.loop_to").arg(p.loop))
                                    return out
                                }
                                delegate: Rectangle {
                                    required property string modelData
                                    required property int index
                                    width: runList.width
                                    height: runText.implicitHeight + 2
                                    color: index === patternEditor.line ? palette.highlight : "transparent"
                                    Label {
                                        id: runText
                                        leftPadding: 3
                                        text: parent.modelData.replace("\t", "    ")
                                        color: parent.index === patternEditor.line ? palette.highlightedText : palette.text
                                    }
                                    TapHandler { onTapped: patternEditor.line = parent.index }
                                }
                            }
                        }
                        RowLayout {
                            Label { text: qsTrId("tastudio_settings.value") }
                            // a button's run is on or off; an analog control's is a value in its range
                            CheckBox {
                                visible: patternEditor.pattern !== undefined && patternEditor.pattern.button
                                enabled: !patternEditor.onLoop
                                padding: 0
                                checked: patternEditor.pattern !== undefined && !patternEditor.onLoop && patternEditor.pattern.runs[patternEditor.line].v === true
                                onToggled: { const on = checked; patternEditor.edit(p => p.runs[patternEditor.line].v = on) }
                            }
                            SpinBox {
                                visible: patternEditor.pattern !== undefined && !patternEditor.pattern.button
                                enabled: !patternEditor.onLoop
                                editable: true
                                Layout.preferredWidth: 90
                                from: patternEditor.pattern !== undefined ? patternEditor.pattern.min : 0
                                to: patternEditor.pattern !== undefined ? patternEditor.pattern.max : 0
                                value: patternEditor.pattern !== undefined && !patternEditor.onLoop ? patternEditor.pattern.runs[patternEditor.line].v : 0
                                onValueModified: { const v = value; patternEditor.edit(p => p.runs[patternEditor.line].v = v) }
                            }
                            Item { Layout.fillWidth: true }
                            Label { text: qsTrId("tastudio_settings.count") }
                            // a run's count (never 0), or on the last line the frame it loops to
                            SpinBox {
                                editable: true
                                Layout.preferredWidth: 80
                                from: patternEditor.onLoop ? 0 : 1
                                to: 100
                                value: patternEditor.pattern === undefined ? 1 : patternEditor.onLoop ? patternEditor.pattern.loop : patternEditor.pattern.runs[patternEditor.line].n
                                onValueModified: {
                                    const v = value
                                    if (patternEditor.onLoop) patternEditor.edit(p => p.loop = v)
                                    else patternEditor.edit(p => p.runs[patternEditor.line].n = Math.max(1, v))
                                }
                            }
                        }
                        RowLayout {
                            Button {
                                text: qsTrId("tastudio_settings.insert")
                                Layout.preferredWidth: 70
                                enabled: patternEditor.pattern !== undefined
                                onClicked: patternEditor.edit(p => p.runs.splice(Math.min(patternEditor.line, p.runs.length), 0, { v: p.button ? false : p.neutral, n: 1 }))
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                text: qsTrId("tastudio_settings.delete")
                                Layout.preferredWidth: 70
                                enabled: patternEditor.pattern !== undefined && !patternEditor.onLoop && patternEditor.pattern.runs.length > 1
                                onClicked: patternEditor.edit(p => p.runs.splice(patternEditor.line, 1))
                            }
                        }
                        Check {
                            text: qsTrId("tastudio_settings.account_for_lag_frames")
                            enabled: patternEditor.pattern !== undefined
                            checked: patternEditor.pattern !== undefined && patternEditor.pattern.skipLag
                            onToggled: { const on = checked; patternEditor.edit(p => p.skipLag = on) }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }

            // ---- Input Roll ----
            ColumnLayout {
                spacing: 4
                Check { text: qsTrId("tastudio_settings.when_following_cursor_scroll_at_every"); checked: win.followAlways; onToggled: win.followAlways = checked }
                GroupBox {
                    title: qsTrId("tastudio_settings.scroll_to_cursor_method")
                    ColumnLayout {
                        spacing: 0
                        Repeater {
                            model: [["near", qsTrId("tastudio_settings.scroll_to_view")], ["top", qsTrId("tastudio_settings.scroll_to_top")],
                                    ["bottom", qsTrId("tastudio_settings.scroll_to_bottom")], ["center", qsTrId("tastudio_settings.scroll_to_center")]]
                            RadioButton {
                                required property var modelData
                                padding: 2
                                text: modelData[1]
                                checked: win.followScroll === modelData[0]
                                onToggled: if (checked) win.followScroll = modelData[0]
                            }
                        }
                    }
                }
                RowLayout {
                    Label { text: qsTrId("tastudio_settings.wheel_scroll_speed") }
                    NumberBox { from: 0; to: 100; value: win.wheelRows; onValueModified: win.wheelRows = value }
                    Label { text: qsTrId("tastudio_settings.frames_per_tick") }
                }
                Check { text: qsTrId("tastudio_settings.sync_scroll_across_all_input_rolls"); checked: win.scrollSync; onToggled: win.scrollSync = checked; Layout.topMargin: 6 }
                Check {
                    text: qsTrId("tastudio_settings.allow_editing_hidden_columns_with_clear")
                    checked: win.editHidden
                    onToggled: win.editHidden = checked
                }
                Item { Layout.fillHeight: true }
            }

            // ---- Misc ----
            ColumnLayout {
                spacing: 4
                GridLayout {
                    columns: 2
                    Label { text: qsTrId("tastudio_settings.rewind_step"); Layout.preferredWidth: 110 }
                    NumberBox { from: 1; to: 100; value: win.rewindStep; onValueModified: win.rewindStep = value }
                    Label { text: qsTrId("tastudio_settings.fast_rewind_step") }
                    NumberBox { from: 1; to: 100; value: win.rewindStepFast; onValueModified: win.rewindStepFast = value }
                    Label { text: qsTrId("tastudio_settings.max_undo_count") }
                    NumberBox { from: 1; to: 10000; value: win.maxUndo; onValueModified: win.maxUndo = value }
                }
                Check { text: qsTrId("tastudio_settings.bind_markers_to_input"); checked: win.bindMarkers; onToggled: win.bindMarkers = checked }
                Check { text: qsTrId("tastudio_settings.include_frame_numbers_when_copying_input"); checked: win.copyFrameNumbers; onToggled: win.copyFrameNumbers = checked }
                Check { text: qsTrId("tastudio_settings.automatically_pause_at_end_of_movie"); checked: win.pauseAtEnd; onToggled: win.pauseAtEnd = checked }
                Check { text: qsTrId("tastudio_settings.load_branch_by_double_clicking"); checked: win.branchDoubleClick; onToggled: win.branchDoubleClick = checked }
                Check { text: qsTrId("tastudio_settings.old_control_scheme_for_branches"); checked: win.oldBranches; onToggled: win.oldBranches = checked }
                Check { text: qsTrId("tastudio_settings.markers_keep_states_by_default"); checked: win.markersKeepStates; onToggled: win.markersKeepStates = checked }
                RowLayout {
                    Label { text: qsTrId("tastudio_settings.branch_screenshot_size_pixels") }
                    NumberBox {
                        from: 64
                        to: 4096
                        stepSize: 32
                        value: win.branchPreviewSide
                        onValueModified: win.branchPreviewSide = value
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTrId("tastudio_settings.the_longest_side_of_the_picture")
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }
        RowLayout {
            Item { Layout.fillWidth: true }
            Button { text: qsTrId("common.apply"); Layout.preferredWidth: 80; onClicked: win.apply() }
            Button { text: qsTrId("common.cancel"); Layout.preferredWidth: 80; onClicked: win.close() }
        }
    }

    FontDialog {
        id: fontDialog
        title: qsTrId("tastudio_settings.the_piano_roll_s_font")
        onAccepted: win.rollFont = JSON.stringify({ family: selectedFont.family, pointSize: selectedFont.pointSize,
                                                    bold: selectedFont.bold, italic: selectedFont.italic })
    }

    // Chimera's Edit TAStudio Colors: a colour a line, clicked to pick; OK keeps them for Apply, Defaults is the theme's
    ApplicationWindow {
        id: colorsWindow
        objectName: "tastudioColorsWindow"
        title: qsTrId("tastudio_settings.edit_tastudio_colors")
        width: 300
        height: 400
        modality: Qt.platform.os === "osx" ? Qt.WindowModal : Qt.ApplicationModal // a sheet on a Mac, which keeps an application-modal window over other apps (#541)
        transientParent: win
        // the colours being chosen ({} is the theme's), and the theme's
        property var own: ({})
        property var theme: ({})
        function openWith(json) {
            try { colorsWindow.own = JSON.parse(json || "{}") } catch (e) { colorsWindow.own = {} }
            try { colorsWindow.theme = JSON.parse(win.themeColorsOf()) } catch (e) { colorsWindow.theme = {} }
            colorsWindow.show()
            colorsWindow.raise()
        }
        function shown(role) { return colorsWindow.own[role] !== undefined ? colorsWindow.own[role] : (colorsWindow.theme[role] || "transparent") }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 2
            Repeater {
                model: [["TasCurrentFrame", qsTrId("tastudio_settings.emulated_frame_cursor")], ["TasGreenZone", qsTrId("tastudio_settings.frame_column")],
                        ["TasGreenZoneInput", qsTrId("tastudio_settings.input_log")], ["TasGreenZoneInputStated", qsTrId("tastudio_settings.savestate")],
                        ["TasGreenZoneInputInvalidated", qsTrId("tastudio_settings.invalidated")], ["TasLagZone", qsTrId("tastudio_settings.frame_column_lag")],
                        ["TasLagZoneInput", qsTrId("tastudio_settings.input_log_lag")], ["TasLagZoneInputStated", qsTrId("tastudio_settings.savestate_lag")],
                        ["TasLagZoneInputInvalidated", qsTrId("tastudio_settings.invalidated_lag")], ["TasMarker", qsTrId("tastudio_settings.marker")],
                        ["TasPermanentMarker", qsTrId("tastudio_settings.marker_run_start_last_input_end")], ["TasAnalogEdit", qsTrId("tastudio_settings.analog_edit_mode")]]
                RowLayout {
                    required property var modelData
                    spacing: 6
                    Rectangle {
                        width: 20; height: 20
                        border.color: palette.text
                        color: colorsWindow.shown(parent.modelData[0])
                        TapHandler {
                            onTapped: {
                                colorDialog.role = parent.parent.modelData[0]
                                colorDialog.selectedColor = colorsWindow.shown(parent.parent.modelData[0])
                                colorDialog.open()
                            }
                        }
                    }
                    Label { text: parent.modelData[1] }
                }
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button { text: qsTrId("tastudio_settings.defaults"); Layout.preferredWidth: 75; onClicked: colorsWindow.own = {} }
                Button { text: qsTrId("common.cancel"); Layout.preferredWidth: 75; onClicked: colorsWindow.close() }
                Button { text: qsTrId("common.ok"); Layout.preferredWidth: 75; onClicked: { win.rollColors = JSON.stringify(colorsWindow.own); colorsWindow.close() } }
            }
        }
        ColorDialog {
            id: colorDialog
            property string role: ""
            onAccepted: {
                const all = Object.assign({}, colorsWindow.own)
                all[colorDialog.role] = selectedColor.toString()
                colorsWindow.own = all
            }
        }
    }
}
