// Chimera's TAStudio: the project's own window, opened beside the main one
// when a project opens and closed with it. Its menus (Edit, Metadata,
// Settings, Window, << Hide), the piano roll, and the boxes at its
// side (Playback, Greenzone, Branches, Markers), as Chimera lays them out.
// Layout and bindings only, over the session, pianoRoll, branches and
// projectDetails view models.
import QtQuick
import QtQuick.Controls
import ChimeraDialogs
import QtQuick.Layouts
import Chimera

ApplicationWindow {
    id: tas
    objectName: "tastudioWindow"
    // the first input roll (the scroll bars and the measurements follow it)
    readonly property Item mainRoll: rollRepeater.count > 0 ? rollRepeater.itemAt(0) : null
    required property QtObject session
    required property QtObject pianoRoll
    required property QtObject branches
    required property QtObject projectDetails
    required property QtObject clipboard
    required property QtObject customize
    // the main window's menu item that shows a hotkey's keys
    required property Component keyedItem
    // a Paths folder for a file dialog to open in
    required property var folderOf
    // the window it sits beside
    required property var mainWindow
    // whether Shift is held (the main window dragged alone)
    required property QtObject keyNamer
    property QtObject toolWindows: null
    // Config > Controllers, where the columns shown are chosen (issue 391)
    property var showControllers: () => {}
    // every hotkey's keys, {"action": "keys"}, for the tooltips that name them
    property var hotkeyKeys: ({})
    // a hotkey's keys, "" when it has none
    function bindingName(hotkey) { return tas.hotkeyKeys[hotkey] || "" }
    // a hotkey's binding as text: "Hotkey: Shift+Q", or what the mouse does too
    function bindingText(hotkey, mouse) {
        const raw = tas.bindingName(hotkey).split(",").map(k => k.trim()).filter(k => k !== "").join(qsTrId("common.or"))
        if (raw === "") return qsTrId("tastudio.hotkey").arg(mouse || qsTrId("tastudio.unbound"))
        return mouse ? qsTrId("tastudio.hotkey_or").arg(mouse).arg(raw) : qsTrId("tastudio.hotkey").arg(raw)
    }
    // the frame each branch was made on, in the list's order (the roll numbers them)
    property var branchFrames: []
    // what this game calls its controls, by their declared names (the session's, read as a machine opens)
    property var controlLabels: ({})
    function readControlLabels() {
        const out = {}
        for (const line of String(tas.session.run("controlLabels") || "").split("\n")) {
            const tab = line.indexOf("\t")
            if (tab > 0) out[line.substring(0, tab)] = line.substring(tab + 1)
        }
        tas.controlLabels = out
    }
    Connections { target: tas.session; function onOpensChanged() { tas.readControlLabels() } }
    ToolTip { id: titleTip; delay: 400 }
    function readBranchFrames() {
        tas.branchFrames = (tas.branches.run("list") || "").split("\n").filter(l => l !== "").map(l => parseInt(l.split("\t")[1]))
    }
    Connections {
        target: tas.branches.rows("branches")
        function onModelReset() { tas.readBranchFrames() }
        function onDataChanged() { tas.readBranchFrames() }
        function onRowsInserted() { tas.readBranchFrames() }
        function onRowsRemoved() { tas.readBranchFrames() }
    }
    // a marker's roles by its row in the list
    function markerRoles(row) { return markerList.rolesAt(row) }
    // a branch's text by its place
    function branchName(i) {
        const line = (tas.branches.run("list") || "").split("\n")[i] || ""
        return line.split("\t").slice(2).join("\t")
    }
    // the bars are where they were left once the window has been laid out, and moving them is kept from then
    property bool sidesPlaced: false
    onVisibleChanged: if (visible && !sidesPlaced) Qt.callLater(() => tas.sidesPlaced = true)
    // Save Window Position (its size: it opens beside the main window), Stay on Top, Float From Main Window
    WindowOptions { id: windowOptions; window: tas; tool: "tastudio"; vm: tas.toolWindows; mainWindow: tas.mainWindow }
    // "<< Hide": the main window offers it back
    property bool hiddenByPerson: false
    title: !tas.session.hasProject ? qsTrId("tastudio.tastudio")
         : qsTrId("tastudio.tastudio_2").arg(tas.session.projectTitle).arg(tas.session.unsaved ? "*" : "")
    width: 720
    height: 600
    minimumWidth: 200
    minimumHeight: 148

    // opened at the main window's right shoulder, tops aligned, on every
    // open, as far right as the screen allows; never off it
    function placeBesideMain() {
        const main = tas.mainWindow
        if (main.visibility !== Window.Windowed) return
        const right = main.screen.virtualX + main.screen.desktopAvailableWidth
        const bottom = main.screen.virtualY + main.screen.desktopAvailableHeight
        let x = main.x + main.width
        if (x + tas.width > right) x = Math.max(main.screen.virtualX, right - tas.width)
        tas.x = x
        tas.y = Math.max(main.screen.virtualY, Math.min(main.y, bottom - tas.height))
    }
    // Header Info, Edit Comments or Edit Subtitles, for the main window's right-click menu
    function showDetails(part) {
        const w = part === "comments" ? commentsWindow : part === "subtitles" ? subtitlesWindow : headerWindow
        w.show()
        w.raise()
    }
    function engage() {
        tas.hiddenByPerson = false
        // a run nobody watches shows no window; the project is TAStudio's all the same
        if (headlessRun.enabled) return
        if (!tas.visible) {
            windowOptions.placeBack(true)
            tas.placeBesideMain()
        }
        tas.show()
        tas.raise()
    }
    // closed, it closes the project, as Chimera's TAStudio did: the changes are
    // asked about first, and Cancel keeps both open (issue 400). "<< Hide" only
    // hides it. A program on its way out closes it as it goes
    onClosing: (close) => {
        if (!tas.session.hasProject || tas.mainWindow.leaving) return
        close.accepted = false
        tas.mainWindow.leaveProject(() => tas.session.run("close"), tas)
    }
    // Move with Main Window: the main window dragged brings this one along,
    // keeping its place beside it; this one dragged moves alone, and so does
    // the main window dragged with Shift held, as Chimera's
    property real mainX: 0
    property real mainY: 0
    Connections {
        target: tas.mainWindow
        function onXChanged() { tas.followMain() }
        function onYChanged() { tas.followMain() }
    }
    function followMain() {
        const main = tas.mainWindow
        const dx = main.x - tas.mainX, dy = main.y - tas.mainY
        tas.mainX = main.x
        tas.mainY = main.y
        if (!tas.visible || !tas.pianoRoll.moveWithMainWindow || tas.keyNamer.shiftHeld()) return
        if (main.visibility !== Window.Windowed || tas.visibility !== Window.Windowed) return
        tas.x += dx
        tas.y += dy
    }
    Component.onCompleted: { tas.mainX = tas.mainWindow.x; tas.mainY = tas.mainWindow.y; tas.readControlLabels() }

    function selectBetweenMarkers() {
        const f = tas.pianoRoll.currentFrame
        const before = tas.pianoRoll.run("previousMarker", [f + 1])
        const after = tas.pianoRoll.run("nextMarker", [f])
        tas.pianoRoll.run("select", [before >= 0 ? before : 0, after >= 0 ? after - 1 : tas.pianoRoll.frameCount - 1])
    }
    // the hotkeys whose action is one of TAStudio's
    readonly property var hotkeyActions: ({
        "Undo": undoAction, "Redo": redoAction, "Sel. bet. Markers": betweenMarkersAction, "Select All": selectAllAction,
        "Clear Frames": clearFramesAction, "Delete Frames": deleteFramesAction, "Insert Frame": insertFrameAction,
        "Insert # Frames": insertFramesAction, "Clone Frames": cloneFramesAction, "Paste Insert": pasteInsertAction,
        "Clone # Times": cloneTimesAction, "Reselect Clip.": reselectAction
    })

    menuBar: RowLayout {
        spacing: 0
        MenuBar {
            Layout.fillWidth: true
            Menu {
                id: editMenu
                // Customize's "pause when a menu is open"
                delegate: tas.keyedItem
                // on the frames picked, or the current one
                title: qsTrId("common.edit")
                readonly property bool picked: tas.pianoRoll.selectFirst >= 0
                readonly property int at: picked ? tas.pianoRoll.selectFirst : tas.pianoRoll.currentFrame
                Action { id: undoAction; property string hotkey: "Undo"; text: qsTrId("tastudio.undo_mnemonic"); enabled: tas.pianoRoll.undoEnabled; onTriggered: tas.pianoRoll.run("undo") }
                Action { id: redoAction; property string hotkey: "Redo"; text: qsTrId("tastudio.redo"); enabled: tas.pianoRoll.redoEnabled; onTriggered: tas.pianoRoll.run("redo") }
                Action { text: qsTrId("tastudio.show_undo_history"); onTriggered: { undoHistoryWindow.show(); undoHistoryWindow.raise() } }
                // Chimera has these, never enabled
                Action { text: qsTrId("tastudio.selection_undo"); enabled: false }
                Action { text: qsTrId("tastudio.selection_redo"); enabled: false }
                MenuDivider {}
                Action { text: qsTrId("tastudio.deselect"); enabled: editMenu.picked; onTriggered: tas.pianoRoll.run("select", [-1, -1]) }
                Action {
                    id: betweenMarkersAction; property string hotkey: "Sel. bet. Markers"
                    text: qsTrId("tastudio.select_between_markers"); enabled: tas.pianoRoll.frameCount > 0
                    onTriggered: tas.selectBetweenMarkers()
                }
                Action {
                    id: selectAllAction; property string hotkey: "Select All"
                    text: qsTrId("common.select_all"); enabled: tas.pianoRoll.frameCount > 0
                    onTriggered: tas.pianoRoll.run("select", [0, tas.pianoRoll.frameCount - 1])
                }
                Action { id: reselectAction; property string hotkey: "Reselect Clip."; text: qsTrId("tastudio.reselect_clipboard"); enabled: tas.pianoRoll.clipboardFrames > 0; onTriggered: tas.pianoRoll.run("reselectClipboard") }
                Action { property string hotkey: "Seek To..."; text: qsTrId("tastudio.go_to_frame_dialog"); enabled: tas.session.seekEnabled; onTriggered: seekDialog.open() }
                MenuDivider {}
                Action {
                    text: qsTrId("common.copy"); shortcut: StandardKey.Copy; enabled: editMenu.picked
                    onTriggered: {
                        tas.pianoRoll.run("copyFrames", [tas.pianoRoll.selectFirst, tas.pianoRoll.selectLast])
                        tas.clipboard.setText(tas.pianoRoll.clipboardText)
                    }
                }
                Action {
                    // the lines on the system's clipboard, as Chimera's Paste reads them
                    text: qsTrId("tastudio.paste_mnemonic"); shortcut: StandardKey.Paste; enabled: tas.session.isOpen && tas.clipboard.hasText
                    onTriggered: tas.pianoRoll.run("pasteText", [Math.min(editMenu.at, tas.pianoRoll.frameCount), tas.clipboard.text(), false])
                }
                Action {
                    id: pasteInsertAction; property string hotkey: "Paste Insert"
                    text: qsTrId("tastudio.paste_insert_mnemonic"); enabled: tas.session.isOpen && tas.clipboard.hasText
                    onTriggered: tas.pianoRoll.run("pasteText", [Math.min(editMenu.at, tas.pianoRoll.frameCount), tas.clipboard.text(), true])
                }
                Action {
                    text: qsTrId("tastudio.cut"); shortcut: StandardKey.Cut; enabled: editMenu.picked
                    onTriggered: {
                        tas.pianoRoll.run("cutFrames", [tas.pianoRoll.selectFirst, tas.pianoRoll.selectLast])
                        tas.clipboard.setText(tas.pianoRoll.clipboardText)
                    }
                }
                MenuDivider {}
                Action {
                    id: clearFramesAction; property string hotkey: "Clear Frames"
                    text: qsTrId("common.clear"); enabled: editMenu.picked
                    onTriggered: tas.pianoRoll.run("clearFrames", [tas.pianoRoll.selectFirst, tas.pianoRoll.selectLast])
                }
                Action {
                    id: deleteFramesAction; property string hotkey: "Delete Frames"
                    text: qsTrId("tastudio.delete_mnemonic"); enabled: editMenu.picked
                    onTriggered: tas.pianoRoll.run("deleteFrames", [tas.pianoRoll.selectFirst, tas.pianoRoll.selectLast])
                }
                Action {
                    id: insertFrameAction; property string hotkey: "Insert Frame"
                    text: qsTrId("tastudio.insert_mnemonic"); enabled: tas.session.isOpen
                    onTriggered: tas.pianoRoll.run("insertFrames", [Math.min(editMenu.at, tas.pianoRoll.frameCount), 1])
                }
                Action { id: insertFramesAction; property string hotkey: "Insert # Frames"; text: qsTrId("tastudio.insert_of_frames"); enabled: tas.session.isOpen; onTriggered: insertDialog.open() }
                Action {
                    id: cloneFramesAction; property string hotkey: "Clone Frames"
                    text: qsTrId("tastudio.clone_mnemonic"); enabled: editMenu.picked
                    onTriggered: tas.pianoRoll.run("cloneFrames", [tas.pianoRoll.selectFirst, tas.pianoRoll.selectLast])
                }
                Action { id: cloneTimesAction; property string hotkey: "Clone # Times"; text: qsTrId("tastudio.clone_times"); enabled: editMenu.picked; onTriggered: cloneDialog.open() }
                MenuDivider {}
                Action {
                    text: qsTrId("tastudio.truncate_movie_mnemonic"); enabled: tas.session.isOpen && editMenu.at < tas.pianoRoll.frameCount
                    onTriggered: tas.pianoRoll.run("truncate", [editMenu.at])
                }
                Action { text: qsTrId("tastudio.clear_greenzone_mnemonic"); enabled: tas.session.isOpen; onTriggered: tas.session.run("clearGreenzone") }
                MenuDivider {}
                Action { text: qsTrId("tastudio.state_history_integrity_check"); enabled: tas.session.isOpen; onTriggered: tas.session.run("checkStateHistory") }
                MenuDivider {}
                Action { text: qsTrId("tastudio.save_selection_to_macro"); enabled: editMenu.picked; onTriggered: saveMacroDialog.open() }
                Action { text: qsTrId("tastudio.place_macro_at_selection"); enabled: editMenu.picked; onTriggered: placeMacroDialog.open() }
                Menu {
                    delegate: MenuEntry {}
                    id: recentMacrosMenu
                    title: qsTrId("tastudio.recent_macros"); icon.source: "qrc:/chimera/icons/Recent.png"
                    enabled: editMenu.picked && tas.pianoRoll.recentMacros !== ""
                    Instantiator {
                        model: tas.pianoRoll.recentMacros === "" ? [] : tas.pianoRoll.recentMacros.split("\n")
                        delegate: MenuItem {
                            required property string modelData
                            text: modelData
                            onTriggered: tas.pianoRoll.run("placeMacro", [tas.pianoRoll.selectFirst, modelData])
                        }
                        onObjectAdded: (index, object) => recentMacrosMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => recentMacrosMenu.removeItem(object)
                    }
                }
            }
            Menu {
                delegate: MenuEntry {}
                title: qsTrId("tastudio.metadata")
                enabled: tas.session.hasProject
                Action { text: qsTrId("tastudio.header"); onTriggered: { headerWindow.show(); headerWindow.raise() } }
                Action { text: qsTrId("tastudio.comments"); onTriggered: { commentsWindow.show(); commentsWindow.raise() } }
                Action { text: qsTrId("tastudio.subtitles"); onTriggered: { subtitlesWindow.show(); subtitlesWindow.raise() } }
            }
            Menu {
                delegate: MenuEntry {}
                title: qsTrId("common.settings")
                Action { text: qsTrId("tastudio.open_settings"); onTriggered: { tastudioSettingsWindow.show(); tastudioSettingsWindow.raise() } }
                // the Columns menu was here: the columns are chosen beside each control in Config > Controllers (issue 391)
                Action { text: qsTrId("tastudio.columns_config_controllers"); enabled: tas.session.isOpen; onTriggered: tas.showControllers() }
            }
            Menu {
                delegate: MenuEntry {}
                title: qsTrId("tastudio.window")
                Action {
                    text: qsTrId("tastudio.move_with_main_window")
                    checkable: true
                    checked: tas.pianoRoll.moveWithMainWindow
                    onToggled: tas.pianoRoll.moveWithMainWindow = checked
                }
                // a Window menu takes the window's options, with no line before them, as Chimera's
                MenuItem { text: qsTrId("common.save_window_position"); checkable: true; checked: windowOptions.savePosition; onTriggered: windowOptions.set("save", checked) }
                MenuItem {
                    text: qsTrId("common.stay_on_top"); checkable: true; checked: windowOptions.onTop
                    visible: windowOptions.canStayOnTop; height: visible ? implicitHeight : 0
                    onTriggered: windowOptions.set("top", checked)
                }
                MenuItem { text: qsTrId("common.float_from_main_window"); checkable: true; checked: windowOptions.floating; onTriggered: windowOptions.set("float", checked) }
                MenuItem {
                    text: qsTrId("common.restore_defaults_mnemonic")
                    // TAStudio's own: every column shown again
                    onTriggered: { tas.pianoRoll.run("showAllColumns"); tas.pianoRoll.mainSplit = 0; tas.pianoRoll.sideSplit = 0; windowOptions.restore() }
                }
            }
        }
        // at the right of the menus, as Chimera's
        ToolButton {
            objectName: "hideTAStudio"
            text: qsTrId("tastudio.hide")
            flat: true
            onClicked: { tas.hiddenByPerson = true; tas.hide() }
        }
    }

    // Chimera's status strip: what was last said, a seek's progress, and the frames picked, the
    // states held and the frames copied
    footer: RowLayout {
        spacing: 6
        readonly property bool seeking: tas.session.seekingTo >= 0 && tas.session.seekingTo - tas.session.seekFrom > 1
        Label {
            padding: 4
            text: parent.seeking ? qsTrId("tastudio.seeking") : tas.branches.error !== "" ? tas.branches.error : tas.status
            elide: Text.ElideRight
            Layout.maximumWidth: tas.width / 2
        }
        ProgressBar {
            objectName: "seekProgress"
            visible: parent.seeking
            Layout.preferredWidth: 100
            from: 0; to: 100
            value: parent.seeking ? Math.max(0, Math.min(100, 100 * (tas.pianoRoll.currentFrame - tas.session.seekFrom) / (tas.session.seekingTo - tas.session.seekFrom))) : 0
        }
        Item { Layout.fillWidth: true }
        Label {
            objectName: "splicerStatus"
            padding: 4
            rightPadding: 8
            readonly property int picked: tas.pianoRoll.selectFirst >= 0 ? tas.pianoRoll.selectLast - tas.pianoRoll.selectFirst + 1 : 0
            readonly property int copied: tas.pianoRoll.clipboardFrames
            text: qsTrId("tastudio.selected_states").arg(picked).arg(picked === 1 ? qsTrId("tastudio.frame") : qsTrId("tastudio.frames")).arg(tas.session.greenzoneStates)
                + (copied > 0 ? qsTrId("tastudio.clipboard").arg(copied).arg(copied === 1 ? qsTrId("tastudio.frame") : qsTrId("tastudio.frames")) : "")
        }
    }
    // the status line: what was last said here
    property string status: qsTrId("tastudio.tastudio_engaged")
    Connections { target: tas.branches; function onStatusChanged() { if (tas.branches.status !== "") tas.status = tas.branches.status } }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        Item {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 120
            // TAStudio's column menu, on a column's name
            Menu {
                delegate: MenuEntry {}
                id: columnMenu
                objectName: "columnMenu"
                property string name: ""
                property bool autoHeld: false
                MenuItem {
                    text: qsTrId("tastudio.auto_hold")
                    checkable: true
                    checked: columnMenu.autoHeld
                    // an analog column asks what value to hold, as Chimera does
                    onTriggered: {
                        const question = tas.pianoRoll.run("autoHoldQuestion", [columnMenu.name])
                        if (question) {
                            holdValueDialog.name = columnMenu.name
                            holdValueDialog.question = question
                            holdValueBox.from = tas.pianoRoll.autoHoldMin
                            holdValueBox.to = tas.pianoRoll.autoHoldMax
                            holdValueBox.value = tas.pianoRoll.autoHoldInitial
                            holdValueDialog.open()
                        } else tas.pianoRoll.run("toggleAutoHold", [columnMenu.name])
                    }
                }
                MenuItem { text: qsTrId("tastudio.hide_column"); onTriggered: tas.pianoRoll.run("hideColumn", [columnMenu.name]) }
                // Chimera's Show columns: Config > Controllers, a box beside each control (issue 391)
                MenuItem { text: qsTrId("tastudio.show_columns"); onTriggered: tas.showControllers() }
                MenuItem { text: qsTrId("tastudio.new_input_roll"); onTriggered: tas.pianoRoll.run("newRoll") }
                MenuItem {
                    text: qsTrId("tastudio.delete_input_roll")
                    visible: tas.pianoRoll.rollCount > 1
                    height: visible ? implicitHeight : 0
                    onTriggered: tas.pianoRoll.run("deleteRoll")
                }
            }
            // TAStudio's input rolls, as Chimera's: the first and any made with New input roll, side by side
            // (one above another turned on its side), each with columns of its own; the one clicked is the one
            // used, and they scroll together unless Sync scroll is off
            GridLayout {
                id: rollsArea
                anchors { left: parent.left; top: parent.top; bottom: tas.pianoRoll.horizontal ? across.top : axisBar.top; right: tas.pianoRoll.horizontal ? parent.right : scroll.left }
                flow: tas.pianoRoll.horizontal ? GridLayout.TopToBottom : GridLayout.LeftToRight
                rows: tas.pianoRoll.horizontal ? Math.max(1, tas.pianoRoll.rollCount) : 1
                columns: tas.pianoRoll.horizontal ? 1 : Math.max(1, tas.pianoRoll.rollCount)
                rowSpacing: 3
                columnSpacing: 3
                Repeater {
                    id: rollRepeater
                    model: tas.pianoRoll.rollCount
                    delegate: PianoRollItem {
                        id: oneRoll
                        required property int index
                        view: index
                        objectName: index === 0 ? "pianoRoll" : "pianoRoll" + (index + 1)
                        // as Chimera's: each as wide as its columns (as tall, turned), the last taking the rest
                        readonly property bool last: index === tas.pianoRoll.rollCount - 1
                        Layout.fillWidth: tas.pianoRoll.horizontal || last
                        Layout.fillHeight: !tas.pianoRoll.horizontal || last
                        Layout.preferredWidth: tas.pianoRoll.horizontal || last ? -1 : contentExtent
                        Layout.preferredHeight: !tas.pianoRoll.horizontal || last ? -1 : contentExtent
                        // where an input roll is when it does not scroll with the first
                        property int ownTop: 0
                        readonly property bool synced: tas.pianoRoll.scrollSync || index === 0
                        // the input roll clicked becomes the one used: its columns are the menus'
                        function use() { if (tas.pianoRoll.activeRoll !== oneRoll.index) tas.pianoRoll.run("setActiveRoll", [oneRoll.index]) }
                        horizontal: tas.pianoRoll.horizontal
                        // a script's own columns, and the colours and text it gives cells
                        extraColumns: tas.pianoRoll.scriptColumns
                        queries: tas.pianoRoll.scriptQueries
                        iconEpoch: tas.pianoRoll.iconEpoch
                        rows: tas.pianoRoll.rows("frames")
                        currentRow: tas.pianoRoll.currentFrame
                        topRow: oneRoll.synced ? tas.pianoRoll.topFrame : oneRoll.ownTop
                        onVisibleRowsChanged: if (oneRoll.index === 0) tas.pianoRoll.visibleFrames = visibleRows
                        onTopRowRequested: (row) => { if (oneRoll.synced) tas.pianoRoll.topFrame = row; else oneRoll.ownTop = row }
                        wheelRows: tas.pianoRoll.wheelRows
                        denoteStatesColor: tas.pianoRoll.denoteStatesColor
                        denoteStatesIcons: tas.pianoRoll.denoteStatesIcons
                        denoteMarkersColor: tas.pianoRoll.denoteMarkersColor
                        denoteMarkersIcons: tas.pianoRoll.denoteMarkersIcons
                        colors: tas.pianoRoll.rollColors
                        rollFont: tas.pianoRoll.rollFont
                        onCellPressed: (row, column, alt) => { oneRoll.use(); tas.pianoRoll.run("press", [row, column, alt]) }
                        onColumnMenuRequested: (column, name, autoHeld, x, y) => { oneRoll.use(); columnMenu.name = name; columnMenu.autoHeld = autoHeld; columnMenu.popup() }
                        // a control's title: its name, and beside it what this game calls it, as Chimera's tooltip
                        onTitleHovered: (name, x, y) => {
                            if (name === "") { titleTip.close(); return }
                            const label = tas.controlLabels[name]
                            titleTip.text = label ? name + " (" + label + ")" : name
                            titleTip.x = x + 12
                            titleTip.y = y + 18
                            titleTip.parent = oneRoll
                            titleTip.open()
                        }
                        // Chimera's menu for the frames picked; nothing picked, no menu
                        onRowMenuRequested: (row) => { oneRoll.use(); if (tas.pianoRoll.selectFirst >= 0 && tas.pianoRoll.selectLast < tas.pianoRoll.frameCount) { rowMenu.row = row; rowMenu.popup() } }
                        onCellDragged: (row, column) => tas.pianoRoll.run("drag", [row, column])
                        onFrameClicked: (row) => { oneRoll.use(); tas.session.run("seek", [row]) }
                        // Chimera's right-button drags on the frame numbers, and a marker moved with Alt
                        onRightDragStarted: (row, ctrl, shift, alt) => { oneRoll.use(); tas.pianoRoll.run("rightDragStart", [row, ctrl, shift, alt]) }
                        onRightDraggedTo: (row) => tas.pianoRoll.run("rightDragTo", [row])
                        onRightDragEnded: tas.pianoRoll.run("rightDragEnd")
                        onMarkerDropped: (from, to) => tas.pianoRoll.run("moveMarker", [from, to])
                        // Chimera's right button held and the wheel turned: a frame back a notch up, from the
                        // current frame at the first notch of a hold
                        onWheelSeekRequested: (notches, first) => tas.session.run("wheelSeek", [notches, first])
                        selectFirst: tas.pianoRoll.selectFirst
                        selectLast: tas.pianoRoll.selectLast
                        // Chimera's numbers for the branches, its green arrow, and the row a seek is going to
                        branchFrames: tas.branchFrames
                        restoreRow: tas.session.restoreFrame
                        seekRow: tas.session.seekingTo
                        // the analog cell being edited: its column is after the buttons
                        pickedRow: tas.pianoRoll.pickedFrame
                        pickedColumn: tas.pianoRoll.pickedAxis < 0 ? -1 : tas.session.buttonCount + tas.pianoRoll.pickedAxis
                        onSelectionRequested: (first, last) => { oneRoll.use(); tas.pianoRoll.run("select", [first, last]) }
                    }
                }
            }
            // the analog cell picked: its value, set here
            RowLayout {
                id: axisBar
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: visible ? 4 : 0 }
                height: visible ? implicitHeight : 0
                visible: tas.pianoRoll.pickedAxis >= 0
                Label { text: qsTrId("tastudio.frame_2").arg(tas.pianoRoll.axisName).arg(tas.pianoRoll.pickedFrame) }
                SpinBox {
                    id: axisSpin
                    editable: true
                    from: tas.pianoRoll.axisMin
                    to: tas.pianoRoll.axisMax
                    value: tas.pianoRoll.axisValue
                    Keys.onReturnPressed: axisSet.clicked()
                }
                Button { id: axisSet; text: qsTrId("tastudio.set"); onClicked: tas.pianoRoll.run("setAxis", [tas.pianoRoll.pickedFrame, tas.pianoRoll.pickedAxis, axisSpin.value]) }
                Item { Layout.fillWidth: true }
                Button { text: qsTrId("tastudio.done"); onClicked: tas.pianoRoll.run("unpick") }
            }
            // turned on its side: the frames scroll across, along the bottom
            ScrollBar {
                id: across
                visible: tas.pianoRoll.horizontal
                anchors { left: parent.left; right: parent.right; bottom: axisBar.top }
                height: visible ? implicitHeight : 0
                orientation: Qt.Horizontal
                policy: ScrollBar.AlwaysOn
                size: tas.mainRoll && tas.mainRoll.rowCount > 0 ? Math.min(1, tas.mainRoll.visibleRows / tas.mainRoll.rowCount) : 1
                position: tas.mainRoll && tas.mainRoll.rowCount > 0 ? tas.mainRoll.topRow / tas.mainRoll.rowCount : 0
                onPositionChanged: if (pressed && tas.mainRoll) tas.pianoRoll.topFrame = Math.round(position * tas.mainRoll.rowCount)
            }
            ScrollBar {
                id: scroll
                visible: !tas.pianoRoll.horizontal
                anchors { right: parent.right; top: parent.top; bottom: axisBar.top }
                width: visible ? implicitWidth : 0
                orientation: Qt.Vertical
                policy: ScrollBar.AlwaysOn
                size: tas.mainRoll && tas.mainRoll.rowCount > 0 ? Math.min(1, tas.mainRoll.visibleRows / tas.mainRoll.rowCount) : 1
                position: tas.mainRoll && tas.mainRoll.rowCount > 0 ? tas.mainRoll.topRow / tas.mainRoll.rowCount : 0
                // only a drag of the bar moves the roll; the binding above moves the bar
                onPositionChanged: if (pressed && tas.mainRoll) tas.pianoRoll.topFrame = Math.round(position * tas.mainRoll.rowCount)
            }
        }

        // the boxes at the side, as Chimera's: Playback, Greenzone and Branches
        // above, Markers below, a bar between them to move; the side keeps its
        // width while the window is made wider or narrower
        SplitView {
            id: sidePanel
            objectName: "sidePanel"
            orientation: Qt.Vertical
            SplitView.preferredWidth: tas.pianoRoll.mainSplit > 0 ? tas.pianoRoll.mainSplit : 208
            SplitView.minimumWidth: 160
            onWidthChanged: if (tas.visible && tas.sidesPlaced) tas.pianoRoll.mainSplit = Math.round(width)

            ColumnLayout {
                SplitView.fillHeight: true
                SplitView.minimumHeight: 280
                spacing: 0

                GroupFrame {
                    title: qsTrId("tastudio.playback")
                    Layout.fillWidth: true
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0
                        RowLayout {
                            spacing: -1
                            PlayButton {
                                text: "<<"
                                tip: tas.bindingText("Seek To Prev Marker") + "\n" + qsTrId("tastudio.seek_to_previous_marker")
                                enabled: tas.session.isOpen
                                onClicked: tas.session.run("seek", [Math.max(0, tas.pianoRoll.run("previousMarker", [tas.pianoRoll.currentFrame]))])
                            }
                            PlayButton {
                                text: "<"
                                tip: tas.bindingText("Rewind", qsTrId("tastudio.right_mouse_button_wheel_up")) + "\n" + qsTrId("tastudio.go_back_1_step_size_of")
                                    + "\n" + qsTrId("tastudio.wheel_is_always_1_frame_per")
                                enabled: tas.session.isOpen && tas.pianoRoll.currentFrame > 0
                                autoRepeat: true
                                autoRepeatDelay: 1000
                                autoRepeatInterval: 100
                                onClicked: tas.session.run("seek", [tas.pianoRoll.currentFrame - 1])
                            }
                            PlayButton {
                                text: "> ||"
                                tip: tas.bindingText("Pause", qsTrId("tastudio.middle_mouse_button")) + "\n" + qsTrId("tastudio.toggle_pause")
                                enabled: tas.session.isOpen
                                onClicked: tas.session.run(tas.session.running ? "pause" : "play")
                            }
                            PlayButton {
                                text: ">"
                                tip: tas.bindingText("Frame Advance", qsTrId("tastudio.right_mouse_button_wheel_down")) + "\n" + qsTrId("tastudio.frame_advance")
                                enabled: tas.session.isOpen
                                autoRepeat: true
                                autoRepeatDelay: 500
                                autoRepeatInterval: 50
                                onClicked: tas.session.run("frameAdvance")
                            }
                            PlayButton {
                                text: ">>"
                                tip: tas.bindingText("Seek To Next Marker") + "\n" + qsTrId("tastudio.seek_to_next_marker")
                                enabled: tas.session.isOpen
                                onClicked: {
                                    const next = tas.pianoRoll.run("nextMarker", [tas.pianoRoll.currentFrame])
                                    if (next >= 0) tas.session.run("seek", [next])
                                }
                            }
                        }
                        RowLayout {
                            spacing: 8
                            BoxCheck {
                                text: qsTrId("tastudio.follow_cursor")
                                tip: tas.bindingText("Toggle Follow Cursor") + "\n" + qsTrId("tastudio.when_enabled_the_current_emulator_frame")
                                    + "\n" + qsTrId("tastudio.made_visible_every_time_the_current")
                                checked: tas.pianoRoll.followPlayback
                                onToggled: tas.pianoRoll.followPlayback = checked
                            }
                            BoxCheck {
                                objectName: "turboSeek"
                                text: qsTrId("tastudio.turbo_seek")
                                tip: tas.bindingText("Toggle Turbo Seek") + "\n" + qsTrId("tastudio.when_enabled_tastudio_will_run_the")
                                checked: tas.session.turboSeek
                                onToggled: tas.session.turboSeek = checked
                            }
                        }
                        BoxCheck {
                            text: qsTrId("tastudio.auto_restore_last_position")
                            tip: tas.bindingText("Toggle Auto-Restore") + "\n" + qsTrId("tastudio.when_enabled_and_you_edit_a")
                                + "\n" + qsTrId("tastudio.tastudio_will_automatically_seek_back_to")
                            checked: tas.session.autoRestore
                            onToggled: tas.session.autoRestore = checked
                        }
                        BoxCheck {
                            objectName: "recordingMode"
                            text: qsTrId("tastudio.recording_mode")
                            tip: tas.bindingText("Toggle read-only") + "\n" + qsTrId("tastudio.when_enabled_you_can_record_inputs")
                            checked: tas.session.recordingMode
                            enabled: tas.session.hasProject
                            onToggled: tas.session.recordingMode = checked
                        }
                    }
                }

                // what the greenzone stores: every frame, every N, or nothing
                GroupFrame {
                    title: qsTrId("tastudio.greenzone")
                    Layout.fillWidth: true
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0
                        RadioButton {
                            padding: 1
                            text: qsTrId("tastudio.every_frame")
                            checked: tas.session.greenzonePeriod === 1
                            onToggled: if (checked) tas.session.greenzonePeriod = 1
                        }
                        RowLayout {
                            spacing: 2
                            RadioButton {
                                id: everyN
                                padding: 1
                                text: qsTrId("tastudio.every")
                                checked: tas.session.greenzonePeriod > 1
                                onToggled: if (checked) tas.session.greenzonePeriod = everyCount.value
                            }
                            SpinBox {
                                id: everyCount
                                from: 2; to: 999; editable: true
                                Layout.preferredWidth: 70
                                value: tas.session.greenzoneEvery
                                onValueModified: {
                                    tas.session.greenzoneEvery = value
                                    if (everyN.checked) tas.session.greenzonePeriod = value
                                }
                            }
                            Label { text: qsTrId("tastudio.frames") }
                        }
                        RadioButton {
                            padding: 1
                            text: qsTrId("tastudio.off")
                            checked: tas.session.greenzonePeriod === 0
                            onToggled: if (checked) tas.session.greenzonePeriod = 0
                        }
                    }
                }

                GroupFrame {
                    title: qsTrId("tastudio.branches")
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 4
                        readonly property bool picked: tas.branches.selected >= 0 && tas.branches.selected < tas.branches.count
                        RollTable {
                            id: branchList
                            objectName: "branchList"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 40
                            model: tas.branches.rows("branches")
                            columns: [{ title: "#", width: 30 }, { title: qsTrId("common.frame"), width: 64 }, { title: qsTrId("tastudio.usertext"), width: 90 }]
                            current: tas.branches.selected
                            rollColor: tas.rollColor
                            // a branch is put in another's place by its number, as Chimera's
                            dragFirstColumn: true
                            cellText: (roles, row, column) => column === 0 ? String(row + 1) : column === 1 ? String(roles.frame) : roles.name
                            // as Chimera colours its list: the branch loaded, else its frame's greenzone or lag colour
                            cellColor: (roles, row, column) => {
                                if (row === tas.branches.current) return tas.rollColor("TasCurrentFrame")
                                const zone = tas.pianoRoll.greenEnd >= 0 ? tas.pianoRoll.run("zoneOf", [roles.frame]) : ""
                                return zone === "lag" ? tas.rollColor("TasLagZoneInput") : zone === "green" ? tas.rollColor("TasGreenZoneInput") : ""
                            }
                            onClicked: (row, column) => tas.branches.selected = row
                            // TAStudio's "load branch by double clicking"
                            onDoubleClicked: (row) => { if (tas.branches.loadOnDoubleClick) tas.branches.run("loadWithWarning", [row]) }
                            onRightClicked: (row) => { if (row >= 0) tas.branches.selected = row; branchMenu.popup() }
                            onDropped: (from, to) => tas.branches.run("swap", [from, to])
                            // the branch's picture while the pointer is over it, as Chimera shows it
                            property int previewing: -1
                            onPointed: (row) => {
                                if (row === previewing) return
                                previewing = row
                                if (row >= 0 && tas.branches.run("preview", [row])) {
                                    const at = branchList.mapToItem(tas.contentItem, 0, 0)
                                    preview.x = Math.max(0, at.x - tas.branches.previewWidth - 8)
                                    preview.y = at.y
                                    preview.open()
                                } else {
                                    preview.close()
                                    tas.branches.run("preview", [-1])
                                }
                            }
                        }
                        RowLayout {
                            spacing: 6
                            IconButton {
                                icon.source: "qrc:/chimera/icons/add.png"
                                tip: tas.bindingName("Add Branch") !== "" ? qsTrId("tastudio.add_branch").arg(tas.bindingName("Add Branch")) : qsTrId("tastudio.add_branch_2")
                                enabled: tas.branches.createEnabled
                                onClicked: tas.branches.run("create", [""])
                            }
                            IconButton {
                                icon.source: "qrc:/chimera/icons/AddEdit.png"; tip: qsTrId("tastudio.add_branch_with_text")
                                enabled: tas.branches.createEnabled
                                onClicked: branchTextDialog.openFor("add")
                            }
                            IconButton {
                                icon.source: "qrc:/chimera/icons/Debugger.png"; tip: qsTrId("tastudio.load_branch")
                                enabled: parent.parent.picked
                                onClicked: tas.branches.run("loadWithWarning", [tas.branches.selected])
                            }
                            IconButton {
                                icon.source: "qrc:/chimera/icons/reboot.png"; tip: qsTrId("tastudio.update_branch")
                                enabled: parent.parent.picked
                                onClicked: tas.branches.run("saveSlot", [tas.branches.selected])
                            }
                            IconButton {
                                icon.source: "qrc:/chimera/icons/JumpTo.png"; tip: qsTrId("tastudio.jump_to_branch_frame")
                                enabled: parent.parent.picked
                                onClicked: tas.branches.run("jumpTo", [tas.branches.selected])
                            }
                            // Chimera's one step of branch undo
                            IconButton {
                                icon.source: "qrc:/chimera/icons/undo.png"; tip: tas.branches.undoText
                                enabled: tas.branches.undoText !== ""
                                onClicked: tas.branches.run("undo")
                            }
                        }
                    }
                }
            }

            GroupFrame {
                title: qsTrId("tastudio.markers")
                SplitView.preferredHeight: tas.pianoRoll.sideSplit > 0 ? tas.pianoRoll.sideSplit : 151
                SplitView.minimumHeight: 90
                onHeightChanged: if (tas.visible && tas.sidesPlaced) tas.pianoRoll.sideSplit = Math.round(height)
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4
                    RollTable {
                        id: markerList
                        objectName: "markerList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 30
                        model: tas.pianoRoll.rows("markers")
                        columns: [{ title: qsTrId("common.frame"), width: 52 }, { title: qsTrId("common.name"), width: 149 }]
                        rollColor: tas.rollColor
                        current: -1
                        property var pickedFrame: -1
                        property string pickedText: ""
                        // the run's own: nothing to rename, move or remove
                        property bool pickedPermanent: false
                        readonly property bool pickedEditable: pickedFrame > 0 && !pickedPermanent
                        Connections {
                            target: markerList.model
                            function onModelReset() { markerList.current = -1; markerList.pickedFrame = -1; markerList.pickedPermanent = false }
                        }
                        cellText: (roles, row, column) => column === 0 ? String(roles.frame) : roles.text
                        // as Chimera colours its list: the run's own (start, last input, end), else the marker
                        // the machine is at or past, else its frame's greenzone or lag colour, the frame's column
                        // in the roll's frame-column shade
                        cellColor: (roles, row, column) => {
                            if (roles.permanent) return tas.rollColor("TasPermanentMarker")
                            if (roles.frame === tas.pianoRoll.currentMarker) return tas.rollColor("TasCurrentFrame")
                            if (roles.zone === "") return ""
                            const lag = roles.zone === "lag"
                            return tas.rollColor(column === 0 ? (lag ? "TasLagZone" : "TasGreenZone") : (lag ? "TasLagZoneInput" : "TasGreenZoneInput"))
                        }
                        function pick(row) {
                            const roles = tas.markerRoles(row)
                            markerList.current = row
                            markerList.pickedFrame = roles.frame
                            markerList.pickedText = roles.text
                            markerList.pickedPermanent = roles.permanent
                        }
                        onClicked: (row, column) => markerList.pick(row)
                        // Jump To, as a double click in Chimera's list
                        onDoubleClicked: (row) => tas.session.run("seek", [tas.markerRoles(row).frame])
                        onRightClicked: (row) => { if (row >= 0) markerList.pick(row); markerMenu.popup() }
                    }
                    RowLayout {
                        spacing: 4
                        IconButton {
                            icon.source: "qrc:/chimera/icons/add.png"
                            tip: tas.bindingName("Set Marker") !== "" ? qsTrId("tastudio.add_marker_to_emulated_frame").arg(tas.bindingName("Set Marker")) : qsTrId("tastudio.add_marker_to_emulated_frame_2")
                            enabled: tas.session.isOpen
                            onClicked: tas.pianoRoll.run("addMarker", [tas.pianoRoll.currentFrame, ""])
                        }
                        IconButton {
                            icon.source: "qrc:/chimera/icons/AddEdit.png"
                            tip: tas.bindingName("Set Marker") !== "" ? qsTrId("tastudio.add_marker_with_text_to_emulated").arg(tas.bindingName("Set Marker")) : qsTrId("tastudio.add_marker_with_text_to_emulated_2")
                            enabled: tas.session.isOpen
                            onClicked: markerDialog.openFor(tas.pianoRoll.currentFrame, "")
                        }
                        IconButton {
                            icon.source: "qrc:/chimera/icons/JumpTo.png"; tip: qsTrId("tastudio.jump_to_marker_frame")
                            enabled: markerList.pickedFrame >= 0
                            onClicked: tas.session.run("seek", [markerList.pickedFrame])
                        }
                        IconButton {
                            icon.source: "qrc:/chimera/icons/ScrollTo.png"; tip: qsTrId("tastudio.scroll_view_to_marker_frame")
                            enabled: markerList.pickedFrame >= 0
                            onClicked: tas.pianoRoll.topFrame = Math.max(0, markerList.pickedFrame - 2)
                        }
                        IconButton {
                            icon.source: "qrc:/chimera/icons/pencil.png"; tip: qsTrId("tastudio.edit_marker_text")
                            enabled: markerList.pickedEditable
                            onClicked: markerDialog.openFor(markerList.pickedFrame, markerList.pickedText)
                        }
                        IconButton {
                            icon.source: "qrc:/chimera/icons/Clock.png"; tip: qsTrId("tastudio.edit_marker_frame_alt_drag")
                            enabled: markerList.pickedEditable
                            onClicked: markerFrameDialog.openFor(markerList.pickedFrame)
                        }
                        IconButton {
                            icon.source: "qrc:/chimera/icons/Delete.png"
                            tip: tas.bindingName("Delete Marker") !== "" ? qsTrId("tastudio.remove_marker_on_marker_s_frame").arg(tas.bindingName("Delete Marker")) : qsTrId("tastudio.remove_marker")
                            enabled: markerList.pickedEditable
                            onClicked: tas.pianoRoll.run("removeMarker", [markerList.pickedFrame])
                        }
                    }
                }
            }
        }
    }

    // the panel's boxes: Chimera's group box, its buttons and its check boxes
    component GroupFrame: GroupBox {
        id: frame
        padding: 6
        topPadding: frameTitle.implicitHeight + 4
        label: Label {
            id: frameTitle
            x: 6
            leftPadding: 2
            rightPadding: 2
            text: frame.title
            background: Rectangle { color: frame.palette.window }
        }
        background: Rectangle {
            y: frameTitle.implicitHeight / 2
            width: frame.width
            height: frame.height - y
            color: "transparent"
            border.color: frame.palette.mid
            radius: 2
        }
    }
    component PlayButton: Button {
        property string tip: ""
        padding: 2
        implicitWidth: 38
        implicitHeight: 23
        ToolTip.text: tip
        ToolTip.visible: hovered && tip !== ""
        ToolTip.delay: 500
    }
    component IconButton: Button {
        property string tip: ""
        padding: 3
        implicitWidth: 24
        implicitHeight: 24
        display: AbstractButton.IconOnly
        icon.width: 16
        icon.height: 16
        icon.color: "transparent"
        ToolTip.text: tip
        ToolTip.visible: hovered && tip !== ""
        ToolTip.delay: 500
    }
    component BoxCheck: CheckBox {
        property string tip: ""
        padding: 1
        ToolTip.text: tip
        ToolTip.visible: hovered && tip !== ""
        ToolTip.delay: 500
    }

    // the roll's menu on the frames picked, in Chimera's order
    Menu {
        id: rowMenu
        objectName: "rowMenu"
        // the row it was opened on
        property var row: -1
        delegate: tas.keyedItem
        Action {
            text: qsTrId("tastudio.set_markers")
            onTriggered: {
                const count = tas.pianoRoll.selectLast - tas.pianoRoll.selectFirst + 1
                if (count > 50) manyMarkersDialog.open()
                else tas.setMarkers()
            }
        }
        Action { text: qsTrId("tastudio.set_marker_with_text"); onTriggered: markerDialog.openFor(tas.pianoRoll.selectFirst, "") }
        // only when a marker is among the frames picked, as Chimera's
        Action {
            text: qsTrId("tastudio.remove_markers")
            enabled: { const next = tas.pianoRoll.run("nextMarker", [tas.pianoRoll.selectFirst - 1]); return next >= 0 && next <= tas.pianoRoll.selectLast }
            onTriggered: tas.removeMarkers()
        }
        MenuDivider {}
        Action { text: qsTrId("tastudio.deselect"); onTriggered: tas.pianoRoll.run("select", [-1, -1]) }
        Action { property string hotkey: "Sel. bet. Markers"; text: qsTrId("tastudio.select_between_markers"); onTriggered: tas.selectBetweenMarkers() }
        MenuDivider {}
        Action { text: qsTrId("tastudio.clear_greenzone"); onTriggered: tas.session.run("clearGreenzone") }
        // a seek that plays (Turbo seek off) stopped where it is
        Action { property string hotkey: "Cancel Seek"; text: qsTrId("tastudio.cancel_seek"); enabled: tas.session.seekingTo >= 0; onTriggered: tas.session.run("pause") }
        MenuDivider {}
        Action {
            text: qsTrId("common.copy")
            onTriggered: {
                tas.pianoRoll.run("copyFrames", [tas.pianoRoll.selectFirst, tas.pianoRoll.selectLast])
                tas.clipboard.setText(tas.pianoRoll.clipboardText)
            }
        }
        Action { text: qsTrId("tastudio.paste"); enabled: tas.clipboard.hasText; onTriggered: tas.pianoRoll.run("pasteText", [tas.pianoRoll.selectFirst, tas.clipboard.text(), false]) }
        Action { property string hotkey: "Paste Insert"; text: qsTrId("tastudio.paste_insert"); enabled: tas.clipboard.hasText; onTriggered: tas.pianoRoll.run("pasteText", [tas.pianoRoll.selectFirst, tas.clipboard.text(), true]) }
        Action {
            text: qsTrId("common.cut")
            onTriggered: {
                tas.pianoRoll.run("cutFrames", [tas.pianoRoll.selectFirst, tas.pianoRoll.selectLast])
                tas.clipboard.setText(tas.pianoRoll.clipboardText)
            }
        }
        MenuDivider {}
        Action { property string hotkey: "Clear Frames"; text: qsTrId("common.clear"); onTriggered: clearFramesAction.trigger() }
        Action { property string hotkey: "Delete Frames"; text: qsTrId("tastudio.delete"); onTriggered: deleteFramesAction.trigger() }
        Action { property string hotkey: "Insert Frame"; text: qsTrId("tastudio.insert"); onTriggered: insertFrameAction.trigger() }
        Action { property string hotkey: "Insert # Frames"; text: qsTrId("tastudio.insert_of_frames"); onTriggered: insertDialog.open() }
        Action { property string hotkey: "Clone Frames"; text: qsTrId("tastudio.clone"); onTriggered: cloneFramesAction.trigger() }
        Action { property string hotkey: "Clone # Times"; text: qsTrId("tastudio.clone_times"); onTriggered: cloneDialog.open() }
        MenuDivider {}
        Action { text: qsTrId("tastudio.truncate_movie"); onTriggered: tas.pianoRoll.run("truncate", [tas.pianoRoll.selectFirst]) }
        // on the machine's own frame only, as Chimera's
        MenuItem {
            text: qsTrId("tastudio.branch")
            enabled: tas.branches.createEnabled
            visible: rowMenu.row === tas.pianoRoll.currentFrame
            height: visible ? implicitHeight : 0
            onTriggered: tas.branches.run("create", [""])
        }
    }
    // a marker on each frame picked; more than 50 asks first, as Chimera does
    function setMarkers() { tas.pianoRoll.run("addMarkers", [tas.pianoRoll.selectFirst, tas.pianoRoll.selectLast]) }
    function removeMarkers() { tas.pianoRoll.run("removeMarkers", [tas.pianoRoll.selectFirst, tas.pianoRoll.selectLast]) }
    Dialog {
        id: manyMarkersDialog
        title: qsTrId("tastudio.add_markers")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label { text: qsTrId("tastudio.are_you_sure_you_want_to") }
        onAccepted: tas.setMarkers()
    }
    // Chimera's branch menu, on the branch picked
    Menu {
        delegate: MenuEntry {}
        id: branchMenu
        readonly property bool picked: tas.branches.selected >= 0 && tas.branches.selected < tas.branches.count
        Action { text: qsTrId("tastudio.add"); icon.source: "qrc:/chimera/icons/add.png"; enabled: tas.branches.createEnabled; onTriggered: tas.branches.run("create", [""]) }
        Action { text: qsTrId("tastudio.add_with_text"); icon.source: "qrc:/chimera/icons/AddEdit.png"; enabled: tas.branches.createEnabled; onTriggered: branchTextDialog.openFor("add") }
        Action { text: qsTrId("tastudio.load"); icon.source: "qrc:/chimera/icons/Debugger.png"; enabled: branchMenu.picked; onTriggered: tas.branches.run("loadWithWarning", [tas.branches.selected]) }
        Action { text: qsTrId("tastudio.update"); icon.source: "qrc:/chimera/icons/updateBranch.png"; enabled: branchMenu.picked; onTriggered: tas.branches.run("saveSlot", [tas.branches.selected]) }
        Action { text: qsTrId("tastudio.update_with_text"); icon.source: "qrc:/chimera/icons/updateWithText.png"; enabled: branchMenu.picked; onTriggered: branchTextDialog.openFor("update") }
        Action { text: qsTrId("tastudio.edit_text"); icon.source: "qrc:/chimera/icons/pencil.png"; enabled: branchMenu.picked; onTriggered: branchTextDialog.openFor("rename") }
        Action { text: qsTrId("tastudio.jump_to"); icon.source: "qrc:/chimera/icons/JumpTo.png"; enabled: branchMenu.picked; onTriggered: tas.branches.run("jumpTo", [tas.branches.selected]) }
        Action { text: tas.branches.undoText !== "" ? tas.branches.undoText : qsTrId("tastudio.undo"); icon.source: "qrc:/chimera/icons/undo.png"; enabled: tas.branches.undoText !== ""; onTriggered: tas.branches.run("undo") }
        MenuDivider {}
        Action { text: qsTrId("common.remove"); icon.source: "qrc:/chimera/icons/Delete.png"; enabled: branchMenu.picked; onTriggered: tas.branches.run("remove", [tas.branches.selected]) }
    }
    Dialog {
        id: branchTextDialog
        property string doing: "add"
        function openFor(what) {
            branchTextDialog.doing = what
            branchTextField.text = what === "add" ? "" : tas.branchName(tas.branches.selected)
            branchTextDialog.open()
        }
        title: qsTrId("tastudio.branch_text")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        TextField { id: branchTextField; width: 300; placeholderText: qsTrId("tastudio.branch_if_empty").arg(tas.branches.count + 1) }
        onOpened: branchTextField.forceActiveFocus()
        onAccepted: {
            if (doing === "add") tas.branches.run("create", [branchTextField.text])
            else {
                if (doing === "update") tas.branches.run("saveSlot", [tas.branches.selected])
                tas.branches.run("rename", [tas.branches.selected, branchTextField.text])
            }
        }
    }
    // Chimera's marker menu, on the marker picked
    Menu {
        delegate: MenuEntry {}
        id: markerMenu
        Action { text: qsTrId("tastudio.jump_to"); icon.source: "qrc:/chimera/icons/JumpTo.png"; onTriggered: tas.session.run("seek", [markerList.pickedFrame]) }
        Action { text: qsTrId("tastudio.scroll_to"); icon.source: "qrc:/chimera/icons/ScrollTo.png"; onTriggered: tas.pianoRoll.topFrame = Math.max(0, markerList.pickedFrame - 2) }
        Action { text: qsTrId("tastudio.edit_text"); icon.source: "qrc:/chimera/icons/pencil.png"; enabled: markerList.pickedEditable; onTriggered: markerDialog.openFor(markerList.pickedFrame, markerList.pickedText) }
        Action { text: qsTrId("tastudio.edit_frame_alt_drag"); icon.source: "qrc:/chimera/icons/Clock.png"; enabled: markerList.pickedEditable; onTriggered: markerFrameDialog.openFor(markerList.pickedFrame) }
        Action { text: qsTrId("tastudio.add"); icon.source: "qrc:/chimera/icons/add.png"; enabled: tas.session.isOpen; onTriggered: tas.pianoRoll.run("addMarker", [tas.pianoRoll.currentFrame, ""]) }
        MenuDivider {}
        Action { text: qsTrId("common.remove"); icon.source: "qrc:/chimera/icons/Delete.png"; enabled: markerList.pickedEditable; onTriggered: tas.pianoRoll.run("removeMarker", [markerList.pickedFrame]) }
    }
    Popup {
        id: preview
        objectName: "branchPreview"
        padding: 2
        closePolicy: Popup.NoAutoClose
        PictureItem {
            width: tas.branches.previewWidth
            height: tas.branches.previewHeight
            source: tas.branches.picture("preview")
        }
    }
    Dialog {
        id: cloneDialog
        title: qsTrId("tastudio.clone_the_frames_picked_how_many")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        SpinBox { id: cloneTimes; from: 1; to: 100000; value: 1; editable: true }
        onAccepted: tas.pianoRoll.run("cloneFrames", [tas.pianoRoll.selectFirst, tas.pianoRoll.selectLast, cloneTimes.value])
    }
    Dialog {
        id: insertDialog
        title: qsTrId("tastudio.insert_frames")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        RowLayout {
            Label { text: qsTrId("tastudio.how_many") }
            SpinBox { id: insertCount; from: 1; to: 1000000; value: 1; editable: true }
        }
        onAccepted: tas.pianoRoll.run("insertFrames", [Math.min(editMenu.at, tas.pianoRoll.frameCount), insertCount.value])
    }
    // Chimera's Go to Frame: the machine paused, a frame index typed
    Dialog {
        id: seekDialog
        title: qsTrId("tastudio.go_to_frame")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: { tas.session.run("pause"); seekTo.text = ""; seekTo.forceActiveFocus() }
        ColumnLayout {
            Label { text: qsTrId("tastudio.jump_seek_to_frame_index") }
            TextField {
                id: seekTo
                validator: IntValidator { bottom: 0 }
                Layout.preferredWidth: 200
                onAccepted: seekDialog.accept()
            }
        }
        onAccepted: if (seekTo.text !== "") tas.session.run("seek", [parseInt(seekTo.text)])
    }
    Dialog {
        id: markerDialog
        property int frame: 0
        function openFor(f, text) { markerDialog.frame = f; markerText.text = text; markerDialog.open() }
        title: qsTrId("tastudio.marker_on_frame").arg(markerDialog.frame)
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        modal: true
        TextField { id: markerText; placeholderText: qsTrId("tastudio.what_happens_here"); width: 300; focus: true }
        onOpened: markerText.forceActiveFocus()
        onAccepted: tas.pianoRoll.run("addMarker", [markerDialog.frame, markerText.text])
    }
    // Chimera's Edit Marker Frame: the marker to another frame, not onto one that has a marker
    Dialog {
        id: markerFrameDialog
        property int frame: 0
        function openFor(f) { markerFrameDialog.frame = f; markerFrameText.text = String(f); markerFrameDialog.open() }
        title: qsTrId("tastudio.marker_for_frame").arg(markerFrameDialog.frame)
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        modal: true
        ColumnLayout {
            Label { text: qsTrId("tastudio.enter_a_frame_number") }
            TextField {
                id: markerFrameText
                validator: IntValidator { bottom: 0 }
                Layout.preferredWidth: 200
                onAccepted: markerFrameDialog.accept()
            }
        }
        onOpened: { markerFrameText.forceActiveFocus(); markerFrameText.selectAll() }
        onAccepted: if (markerFrameText.text !== "") tas.pianoRoll.run("moveMarker", [markerFrameDialog.frame, parseInt(markerFrameText.text)])
    }
    Dialog {
        id: holdValueDialog
        objectName: "holdValueDialog"
        property string name: ""
        property string question: ""
        title: qsTrId("tastudio.auto_hold")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            Label { text: holdValueDialog.question }
            SpinBox { id: holdValueBox; editable: true }
        }
        onAccepted: tas.pianoRoll.run("toggleAutoHold", [holdValueDialog.name, holdValueBox.value])
    }
    FileDialog {
        id: saveMacroDialog
        title: qsTrId("tastudio.save_the_frames_picked_as_a")
        currentFolder: tas.folderOf("Macros")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "chimeraMacro"
        nameFilters: [qsTrId("tastudio.movie_macros_chimeramacro")]
        onAccepted: tas.pianoRoll.run("saveMacro", [tas.pianoRoll.selectFirst, tas.pianoRoll.selectLast, selectedFile])
    }
    FileDialog {
        id: placeMacroDialog
        title: qsTrId("tastudio.place_a_macro_at_the_frames")
        currentFolder: tas.folderOf("Macros")
        nameFilters: [qsTrId("tastudio.movie_macros_chimeramacro"), qsTrId("common.all_files")]
        onAccepted: tas.pianoRoll.run("placeMacro", [tas.pianoRoll.selectFirst, selectedFile])
    }
    // Chimera's Header Info, Edit Comments and Edit Subtitles, each a window of its own
    ProjectDetails { id: headerWindow; part: "header"; vm: tas.projectDetails; visible: false }
    ProjectDetails { id: commentsWindow; part: "comments"; vm: tas.projectDetails; visible: false }
    ProjectDetails { id: subtitlesWindow; part: "subtitles"; vm: tas.projectDetails; visible: false }
    UndoHistory { id: undoHistoryWindow; vm: tas.pianoRoll; rollColor: tas.rollColor; visible: false }
    // a TAStudio colour as the roll draws it: its own (Set Colors), else the
    // theme's, each read when it changes (asked for every cell of the branch
    // and marker lists on every frame, read each time it kept the window busy)
    readonly property var ownRollColors: {
        try { return JSON.parse(tas.pianoRoll.rollColors || "{}") } catch (e) { return {} }
    }
    readonly property var themeRollColors: {
        // read again when the theme changes: its colours and the palette it gives the window
        if (!tas.mainRoll || !tas.mainWindow.themeColorMap || !tas.palette.base) return {}
        try { return JSON.parse(tas.mainRoll.themeColors()) } catch (e) { return {} }
    }
    function rollColor(role) {
        if (tas.ownRollColors[role] !== undefined) return tas.ownRollColors[role]
        return tas.themeRollColors[role] || ""
    }
    TAStudioSettings { id: tastudioSettingsWindow; session: tas.session; pianoRoll: tas.pianoRoll; branches: tas.branches; themeColorsOf: () => tas.mainRoll ? tas.mainRoll.themeColors() : "{}"; ownerWindow: tas; visible: false }
}
