// The Lua Console, as Chimera's: its menus and toolbar; the scripts on the
// left, their count above them; the output on the right, with the line to
// type Lua into under it; the session's name at the bottom. Active
// Registered Functions and Lua Functions are windows of their own. Layout
// and bindings only, over the luaConsole view model.
import QtQuick
import QtQuick.Controls
import ChimeraDialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "luaConsoleWindow"
    required property QtObject vm
    // the main window's: Paths folders, file URLs, the clipboard, theme colours
    property var folderOf: null
    property var fileUrl: (path) => localFiles.folderUrl(path)
    property var localPath: (url) => url.toString()
    property QtObject clipboard: null
    property var themeColor: (role, fallback) => fallback
    // the lines between the scripts' rows and columns, as the theme gives Chimera's lists
    readonly property color gridColour: win.themeColor("RollGridLines", String(Qt.darker(palette.base, 1.25)))
    property QtObject customize: null
    property QtObject toolWindows: null
    property Window mainWindow: null
    title: qsTrId("lua_console.lua_console")
    width: 760
    height: 460

    function editFiles(paths) {
        for (const p of paths.split("\n")) if (p !== "") Qt.openUrlExternally(win.fileUrl(p))
    }
    function duplicateScript() {
        const name = win.vm.run("duplicateName")
        // a separator is copied at once, as Chimera's
        if (name === "") { win.vm.run("duplicate", [""]); return }
        duplicateDialog.currentFolder = win.fileUrl(name.substring(0, Math.max(name.lastIndexOf("/"), name.lastIndexOf("\\"))))
        duplicateDialog.selectedFile = win.fileUrl(name)
        duplicateDialog.open()
    }
    // Save Window Position, Stay on Top, Float From Main Window
    WindowOptions { id: windowOptions; window: win; tool: "luaConsole"; vm: win.toolWindows; mainWindow: win.mainWindow }
    onVisibleChanged: if (visible) windowOptions.placeBack(false)


    // Chimera asks before a saved session with changes is let go
    property var afterAsk: null
    property bool closingAsked: false
    function leaveSession(then) {
        if (!win.vm.changes || win.vm.sessionPath === "") { then(); return }
        win.afterAsk = then
        askSave.open()
    }
    MessageDialog {
        id: askSave
        title: qsTrId("common.closing_with_unsaved_changes")
        text: qsTrId("lua_console.save_lua_console_session")
        buttons: MessageDialog.Yes | MessageDialog.No | MessageDialog.Cancel
        onButtonClicked: (button) => {
            const then = win.afterAsk
            win.afterAsk = null
            if (button === MessageDialog.Cancel) return
            if (button === MessageDialog.Yes) win.vm.run("saveSession")
            if (then) then()
        }
    }
    // closing it stops every script, as Chimera's
    onClosing: (close) => {
        if (win.closingAsked) { win.closingAsked = false; return }
        if (win.vm.changes && win.vm.sessionPath !== "") {
            close.accepted = false
            win.leaveSession(() => { win.vm.run("closed"); win.closingAsked = true; win.close() })
            return
        }
        win.vm.run("closed")
    }

    FileDialog {
        id: openScriptDialog
        title: qsTrId("lua_console.open_script")
        fileMode: FileDialog.OpenFiles
        currentFolder: win.folderOf ? win.folderOf("Lua") : ""
        nameFilters: [qsTrId("lua_console.lua_scripts_lua_txt"), qsTrId("common.all_files")]
        onAccepted: { for (const f of selectedFiles) win.vm.run("openScript", [f]) }
    }
    FileDialog {
        id: newScriptDialog
        title: qsTrId("lua_console.new_script")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "lua"
        currentFolder: win.folderOf ? win.folderOf("Lua") : ""
        nameFilters: [qsTrId("lua_console.lua_scripts_lua")]
        onAccepted: {
            const made = win.vm.run("newScript", [selectedFile])
            if (made) win.editFiles(made)
        }
    }
    FileDialog {
        id: duplicateDialog
        title: qsTrId("lua_console.duplicate_script")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "lua"
        nameFilters: [qsTrId("lua_console.lua_scripts_lua")]
        onAccepted: {
            win.vm.run("duplicate", [selectedFile])
            win.editFiles(win.localPath(selectedFile))
        }
    }
    FileDialog {
        id: openSessionDialog
        title: qsTrId("lua_console.open_session")
        currentFolder: win.folderOf ? win.folderOf("Lua") : ""
        nameFilters: [qsTrId("lua_console.lua_session_files_luases"), qsTrId("common.all_files")]
        onAccepted: win.vm.run("openSession", [selectedFile])
    }
    FileDialog {
        id: saveSessionDialog
        title: qsTrId("lua_console.save_session_as")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "luases"
        currentFolder: win.folderOf ? win.folderOf("Lua") : ""
        nameFilters: [qsTrId("lua_console.lua_session_files_luases")]
        onAccepted: win.vm.run("saveSessionAs", [selectedFile])
    }

    // a menu item showing its own shortcut, as Chimera's menus do
    Component {
        id: shortcutItem
        MenuItem {
            id: item
            readonly property string keys: action && typeof action.shortcut === "string" ? action.shortcut
                : action && action.keysShown ? action.keysShown : ""
            rightPadding: padding + (keys !== "" ? keyLabel.implicitWidth + 24 : 0)
            Label {
                id: keyLabel
                text: item.keys
                anchors.right: parent.right
                anchors.rightMargin: item.padding
                anchors.verticalCenter: parent.verticalCenter
                color: item.highlighted ? item.palette.highlightedText : item.palette.text
            }
        }
    }

    // the actions, shared by the menus, the toolbar and the list's menu
    readonly property bool anyFile: win.vm.selectedScripts > 0
    readonly property bool anyRow: win.vm.selectedCount > 0
    Action { id: newScriptAction; text: qsTrId("lua_console.new_script"); icon.source: "qrc:/chimera/icons/NewFile.png"; shortcut: "Ctrl+N"; onTriggered: newScriptDialog.open() }
    Action { id: openScriptAction; text: qsTrId("lua_console.open_script_dialog"); icon.source: "qrc:/chimera/icons/OpenFile.png"; shortcut: "Ctrl+O"; onTriggered: openScriptDialog.open() }
    Action { id: refreshAction; text: qsTrId("lua_console.refresh_mnemonic"); icon.source: "qrc:/chimera/icons/Refresh.png"; shortcut: "F5"; onTriggered: win.vm.run("refresh") }
    Action { id: toggleAction; text: qsTrId("lua_console.toggle"); icon.source: "qrc:/chimera/icons/checkbox.png"; shortcut: "Ctrl+T"; onTriggered: win.vm.run("toggle") }
    Action { id: pauseAction; text: qsTrId("lua_console.pause_or_resume"); icon.source: "qrc:/chimera/icons/Pause.png"; enabled: win.anyFile; onTriggered: win.vm.run("pause") }
    Action { id: editAction; text: qsTrId("lua_console.edit_script_mnemonic"); icon.source: "qrc:/chimera/icons/pencil.png"; shortcut: "Ctrl+E"; enabled: win.anyFile; onTriggered: win.editFiles(win.vm.run("editPaths")) }
    Action { id: removeAction; text: qsTrId("lua_console.remove_script_mnemonic"); icon.source: "qrc:/chimera/icons/Delete.png"; shortcut: "Delete"; enabled: win.anyRow; onTriggered: win.vm.run("remove") }
    Action { id: duplicateAction; text: qsTrId("lua_console.duplicate_script_mnemonic"); icon.source: "qrc:/chimera/icons/Duplicate.png"; enabled: win.anyRow; onTriggered: win.duplicateScript() }
    Action { id: clearAction; text: qsTrId("lua_console.clear_output_mnemonic"); icon.source: "qrc:/chimera/icons/clear_console.png"; onTriggered: win.vm.run("clear") }
    Action { id: separatorAction; text: qsTrId("lua_console.insert_separator"); icon.source: "qrc:/chimera/icons/InsertSeparator.png"; shortcut: "Ctrl+I"; onTriggered: win.vm.run("insertSeparator") }
    Action { id: upAction; text: qsTrId("lua_console.move_up_mnemonic"); icon.source: "qrc:/chimera/icons/MoveUp.png"; shortcut: "Ctrl+U"; enabled: win.anyRow; onTriggered: win.vm.run("moveUp") }
    Action { id: downAction; text: qsTrId("lua_console.move_down_mnemonic"); icon.source: "qrc:/chimera/icons/MoveDown.png"; shortcut: "Ctrl+D"; enabled: win.anyRow; onTriggered: win.vm.run("moveDown") }
    Action { id: selectAllAction; property string keysShown: "Ctrl+A"; text: qsTrId("common.select_all"); enabled: scriptList.count > 0; onTriggered: win.vm.run("selectAll") }
    Action { id: stopAllAction; text: qsTrId("lua_console.stop_all_scripts"); icon.source: "qrc:/chimera/icons/Stop.png"; enabled: win.vm.anyEnabled; onTriggered: win.vm.run("stopAll") }
    Action { id: functionsAction; text: qsTrId("lua_console.registered_functions_dialog"); shortcut: "F12"; onTriggered: { functionsWindow.show(); functionsWindow.raise() } }
    Action { id: eraseAction; text: qsTrId("lua_console.erase_stale_stuck_lua_drawing_layers"); icon.source: "qrc:/chimera/icons/Erase.png"; onTriggered: win.vm.run("eraseDrawings") }
    Action { id: clearFunctionsAction; text: qsTrId("lua_console.clear_registered_functions"); icon.source: "qrc:/chimera/icons/Delete.png"; enabled: win.vm.functionCount > 0; onTriggered: win.vm.run("removeAllFunctions") }

    menuBar: MenuBar {
        Menu {
            title: qsTrId("common.file")
            delegate: shortcutItem
            Action {
                text: qsTrId("lua_console.new_session"); shortcut: "Ctrl+Shift+N"; icon.source: "qrc:/chimera/icons/NewFile.png"
                onTriggered: win.leaveSession(() => win.vm.run("newSession"))
            }
            Action {
                text: qsTrId("lua_console.open_session_dialog"); shortcut: "Ctrl+Shift+O"; icon.source: "qrc:/chimera/icons/OpenFile.png"
                onTriggered: win.leaveSession(() => openSessionDialog.open())
            }
            Action {
                text: qsTrId("lua_console.save_session"); shortcut: "Ctrl+S"; icon.source: "qrc:/chimera/icons/SaveAs.png"
                enabled: win.vm.changes
                onTriggered: win.vm.sessionPath === "" ? saveSessionDialog.open() : win.vm.run("saveSession")
            }
            Action { text: qsTrId("lua_console.save_session_as_dialog"); shortcut: "Ctrl+Shift+S"; onTriggered: saveSessionDialog.open() }
            MenuDivider {}
            Menu {
                delegate: MenuEntry {}
                id: recentSessionsMenu
                title: qsTrId("lua_console.recent_sessions")
                enabled: win.vm.recentSessions !== ""
                Instantiator {
                    model: win.vm.recentSessions === "" ? [] : win.vm.recentSessions.split("\n")
                    delegate: MenuItem {
                        required property string modelData
                        text: modelData
                        onTriggered: win.leaveSession(() => win.vm.run("openSession", [modelData]))
                    }
                    onObjectAdded: (index, object) => recentSessionsMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => recentSessionsMenu.removeItem(object)
                }
            }
            Menu {
                delegate: MenuEntry {}
                id: recentScriptsMenu
                title: qsTrId("lua_console.recent_scripts")
                icon.source: "qrc:/chimera/icons/Recent.png"
                enabled: win.vm.recentScripts !== ""
                Instantiator {
                    model: win.vm.recentScripts === "" ? [] : win.vm.recentScripts.split("\n")
                    delegate: MenuItem {
                        required property string modelData
                        text: modelData
                        onTriggered: win.vm.run("openScript", [modelData])
                    }
                    onObjectAdded: (index, object) => recentScriptsMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => recentScriptsMenu.removeItem(object)
                }
            }
            MenuDivider {}
            Action { property string keysShown: "Alt+F4"; text: qsTrId("common.close_mnemonic"); onTriggered: win.close() }
        }
        Menu {
            title: qsTrId("lua_console.script_mnemonic")
            delegate: shortcutItem
            MenuItem { action: newScriptAction }
            MenuItem { action: openScriptAction }
            MenuItem { action: refreshAction }
            MenuItem { action: toggleAction; enabled: win.anyFile }
            MenuItem { action: pauseAction }
            MenuItem { action: editAction }
            MenuItem { action: removeAction }
            MenuItem { action: duplicateAction }
            MenuItem { action: clearAction }
            MenuDivider {}
            MenuItem { action: separatorAction }
            MenuItem { action: upAction }
            MenuItem { action: downAction }
            MenuItem { action: selectAllAction }
            MenuDivider {}
            MenuItem { action: stopAllAction }
            MenuItem { action: functionsAction }
        }
        Menu {
            delegate: MenuEntry {}
            title: qsTrId("common.settings")
            MenuItem { text: qsTrId("lua_console.disable_scripts_on_load"); checkable: true; checked: win.vm.disableOnLoad; onTriggered: win.vm.disableOnLoad = checked }
            MenuItem { text: qsTrId("lua_console.toggle_all_if_none_selected"); checkable: true; checked: win.vm.toggleAllIfNoneSelected; onTriggered: win.vm.toggleAllIfNoneSelected = checked }
            MenuItem { text: qsTrId("lua_console.reload_when_script_file_changes"); checkable: true; checked: win.vm.reloadOnChange; onTriggered: win.vm.reloadOnChange = checked }
            MenuItem { text: qsTrId("lua_console.duplicate_to_stdout"); checkable: true; checked: win.vm.duplicateToStdout; onTriggered: win.vm.duplicateToStdout = checked }
            MenuDivider {}
            // Chimera's: Sublime Text 2's Lua completions written for it; said as found ("detected") or written ("installed")
            Menu {
                delegate: MenuEntry {}
                title: qsTrId("lua_console.register_to_text_editors")
                onAboutToShow: win.vm.run("textEditors")
                MenuItem {
                    text: win.vm.sublimeState === "installed" ? qsTrId("lua_console.sublime_text_2_installed")
                        : win.vm.sublimeState === "detected" ? qsTrId("lua_console.sublime_text_2_detected") : qsTrId("lua_console.sublime_text_2")
                    font.italic: win.vm.sublimeState === "detected"
                    checkable: true
                    checked: win.vm.sublimeState === "installed"
                    onTriggered: {
                        win.vm.run("registerSublime")
                        checked = Qt.binding(() => win.vm.sublimeState === "installed")
                    }
                }
            }
            MenuDivider {}
            MenuItem { text: qsTrId("common.save_window_position"); checkable: true; checked: windowOptions.savePosition; onTriggered: windowOptions.set("save", checked) }
            MenuItem {
                text: qsTrId("common.stay_on_top"); checkable: true; checked: windowOptions.onTop
                visible: windowOptions.canStayOnTop; height: visible ? implicitHeight : 0
                onTriggered: windowOptions.set("top", checked)
            }
            MenuItem { text: qsTrId("common.float_from_main_window"); checkable: true; checked: windowOptions.floating; onTriggered: windowOptions.set("float", checked) }
            MenuItem { text: qsTrId("common.restore_defaults_mnemonic"); onTriggered: { win.vm.run("restoreDefaults"); windowOptions.restore() } }
        }
        Menu {
            title: qsTrId("lua_console.help")
            delegate: shortcutItem
            Action { text: qsTrId("lua_console.lua_functions_list"); shortcut: "F1"; onTriggered: { functionsListWindow.show(); functionsListWindow.raise() } }
        }
    }

    header: ToolBar {
        RowLayout {
            spacing: 0
            component Tool: ToolButton {
                property string tip: action ? action.text.replace("&", "") : ""
                display: AbstractButton.IconOnly
                ToolTip.visible: hovered
                ToolTip.text: tip
                ToolTip.delay: 500
            }
            Tool { action: newScriptAction; tip: qsTrId("lua_console.new_lua_script") }
            Tool { action: openScriptAction; tip: qsTrId("lua_console.open_script") }
            Tool { action: toggleAction; tip: qsTrId("lua_console.toggle_script") }
            Tool { action: refreshAction; tip: qsTrId("lua_console.refresh") }
            Tool { action: pauseAction }
            Tool { action: editAction; tip: qsTrId("lua_console.edit_script") }
            Tool { action: removeAction; tip: qsTrId("lua_console.remove_script") }
            Tool { action: duplicateAction; tip: qsTrId("lua_console.duplicate_script") }
            ToolSeparator {}
            Tool { action: upAction; tip: qsTrId("lua_console.move_up") }
            Tool { action: downAction; tip: qsTrId("lua_console.move_down") }
            Tool { action: separatorAction }
            ToolSeparator {}
            Tool { action: clearAction; tip: qsTrId("lua_console.clear_output") }
            Tool { action: eraseAction }
        }
    }

    footer: Label {
        objectName: "luaConsoleStatus"
        text: win.vm.error !== "" ? win.vm.error : win.vm.status
        padding: 4
        elide: Text.ElideRight
    }

    // the list's right-click menu
    Menu {
        delegate: MenuEntry {}
        id: scriptMenu
        MenuItem { text: qsTrId("lua_console.toggle"); icon.source: "qrc:/chimera/icons/Refresh.png"; enabled: win.anyFile; onTriggered: win.vm.run("toggle") }
        MenuItem { text: qsTrId("lua_console.pause_or_resume"); icon.source: "qrc:/chimera/icons/Pause.png"; enabled: win.anyFile; onTriggered: win.vm.run("pause") }
        MenuItem { text: qsTrId("common.edit"); icon.source: "qrc:/chimera/icons/Cut.png"; enabled: win.anyFile; onTriggered: win.editFiles(win.vm.run("editPaths")) }
        MenuItem { text: qsTrId("common.remove_mnemonic"); icon.source: "qrc:/chimera/icons/Close.png"; enabled: win.anyRow; onTriggered: win.vm.run("remove") }
        MenuItem { text: qsTrId("lua_console.insert_seperator"); icon.source: "qrc:/chimera/icons/InsertSeparator.png"; onTriggered: win.vm.run("insertSeparator") }
        MenuDivider { visible: win.vm.anyEnabled; height: visible ? implicitHeight : 0 }
        MenuItem { action: stopAllAction; visible: win.vm.anyEnabled; height: visible ? implicitHeight : 0 }
        MenuItem { action: clearFunctionsAction }
    }
    // the output's right-click menu
    Menu {
        delegate: MenuEntry {}
        id: outputMenu
        MenuItem {
            text: qsTrId("common.copy"); enabled: win.vm.outputFirst >= 0
            onTriggered: if (win.clipboard) win.clipboard.setText(win.vm.run("copyOutput"))
        }
        MenuItem { text: qsTrId("common.select_all"); enabled: win.vm.lines > 0; onTriggered: win.vm.run("selectAllOutput") }
        MenuItem { text: qsTrId("lua_console.clear"); enabled: win.vm.lines > 0; onTriggered: win.vm.run("clear") }
        MenuDivider {}
        MenuItem { text: qsTrId("lua_console.registered_functions"); enabled: win.vm.functionCount > 0; onTriggered: functionsAction.trigger() }
        MenuItem { action: clearFunctionsAction }
    }

    // Reload When Script File Changes: the files looked at twice a second
    Timer { interval: 500; repeat: true; running: win.vm.reloadOnChange && scriptList.count > 0; onTriggered: win.vm.run("checkFiles") }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        ColumnLayout {
            SplitView.preferredWidth: 300
            SplitView.minimumWidth: 120
            spacing: 0
            Label { objectName: "luaScriptCount"; text: win.vm.summary; padding: 4 }
            // the columns: the state's icon, the script's name, its path;
            // a click on a heading sorts by it
            RowLayout {
                spacing: 0
                Layout.fillWidth: true
                Repeater {
                    model: [{key: "Path", text: " ", width: 24}, {key: "Script", text: qsTrId("lua_console.script"), width: 92}, {key: "Path", text: qsTrId("lua_console.path"), width: -1}]
                    delegate: Button {
                        required property var modelData
                        text: modelData.text
                        flat: true
                        padding: 2
                        Layout.preferredWidth: modelData.width > 0 ? modelData.width : -1
                        Layout.fillWidth: modelData.width < 0
                        onClicked: win.vm.run("sort", [modelData.key])
                    }
                }
            }
            ListView {
                id: scriptList
                objectName: "luaScriptList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                focus: true
                model: win.vm.rows("scripts")
                ScrollBar.vertical: ScrollBar {}
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) { win.vm.run("selectAll"); event.accepted = true }
                }
                delegate: Rectangle {
                    id: row
                    required property int index
                    required property var model
                    readonly property color ink: row.model.selected ? palette.highlightedText : palette.text
                    width: scriptList.width
                    height: 22
                    color: row.model.selected ? palette.highlight
                         : row.model.separator ? palette.window
                         : row.model.state === "paused" ? win.themeColor("RowPaused", "#FFB6C1")
                         : row.model.state === "running" ? win.themeColor("RowActive", "#E0FFFF")
                         : palette.base
                    // the grid, as Chimera's list draws it: a line under every row and between the columns
                    Repeater {
                        model: [24, 116]
                        delegate: Rectangle { required property int modelData; x: modelData; width: 1; height: row.height; color: win.gridColour }
                    }
                    Rectangle { y: row.height - 1; width: row.width; height: 1; color: win.gridColour }
                    Image {
                        x: 4
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !row.model.separator
                        source: row.model.state === "running" ? "qrc:/chimera/icons/arrow_green.png"
                              : row.model.state === "paused" ? "qrc:/chimera/icons/Pause.png" : "qrc:/chimera/icons/Stop.png"
                    }
                    Label {
                        x: 26; width: 88
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.model.name
                        elide: Text.ElideRight
                        color: row.ink
                    }
                    Label {
                        x: 118; width: parent.width - 120
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.model.path
                        elide: Text.ElideLeft
                        color: row.ink
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onPressed: (mouse) => {
                            scriptList.forceActiveFocus()
                            if (mouse.button === Qt.RightButton) {
                                if (!row.model.selected) win.vm.run("select", [row.index, "only"])
                                scriptMenu.popup()
                                return
                            }
                            win.vm.run("select", [row.index, (mouse.modifiers & Qt.ShiftModifier) ? "range"
                                : (mouse.modifiers & Qt.ControlModifier) ? "toggle" : "only"])
                        }
                        onDoubleClicked: (mouse) => { if (mouse.button === Qt.LeftButton) win.vm.run("toggleRow", [row.index]) }
                    }
                }
                // a click below the rows selects none; a right click there opens the menu
                MouseArea {
                    z: -1
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPressed: (mouse) => {
                        scriptList.forceActiveFocus()
                        win.vm.run("select", [-1, "none"])
                        if (mouse.button === Qt.RightButton) scriptMenu.popup()
                    }
                }
            }
        }

        GroupBox {
            title: qsTrId("lua_console.output")
            SplitView.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 4
                ListView {
                    id: output
                    objectName: "luaOutput"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: win.vm.rows("output")
                    ScrollBar.vertical: ScrollBar {}
                    onCountChanged: positionViewAtEnd()
                    delegate: Rectangle {
                        id: outLine
                        required property int index
                        required property var model
                        width: output.width
                        height: lineText.implicitHeight
                        color: outLine.model.selected ? palette.highlight : "transparent"
                        Label {
                            id: lineText
                            width: parent.width
                            text: outLine.model.line
                            font.family: "monospace"
                            wrapMode: Text.Wrap
                            color: outLine.model.selected ? palette.highlightedText : palette.text
                        }
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onPressed: (mouse) => {
                                output.forceActiveFocus()
                                if (mouse.button === Qt.RightButton) { outputMenu.popup(); return }
                                win.vm.run("selectOutput", [outLine.index, (mouse.modifiers & Qt.ShiftModifier) ? "range" : "only"])
                            }
                        }
                    }
                    MouseArea {
                        z: -1
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton
                        onPressed: outputMenu.popup()
                    }
                    Keys.onPressed: (event) => {
                        if (event.matches(StandardKey.Copy) && win.clipboard) { win.clipboard.setText(win.vm.run("copyOutput")); event.accepted = true }
                        else if (event.matches(StandardKey.SelectAll)) { win.vm.run("selectAllOutput"); event.accepted = true }
                    }
                }
                // a line of Lua: Enter runs it, Up and Down go through the lines typed before
                TextField {
                    id: input
                    objectName: "luaInput"
                    Layout.fillWidth: true
                    font.family: "monospace"
                    function runLine() { const c = text; clear(); win.vm.run("execute", [c]) }
                    function recall(step) {
                        const t = win.vm.run("recall", [step])
                        if (t !== undefined && t !== null) { text = t; cursorPosition = text.length }
                    }
                    Keys.onReturnPressed: runLine()
                    Keys.onEnterPressed: runLine()
                    Keys.onUpPressed: recall(1)
                    Keys.onDownPressed: recall(-1)
                }
            }
        }
    }

    // Active Registered Functions, as Chimera's: by event and name, each to
    // call or remove
    ApplicationWindow {
        id: functionsWindow
        objectName: "registeredFunctionsWindow"
        title: qsTrId("lua_console.active_registered_functions")
        width: 560
        height: 300
        property int current: -1
        property string currentGuid: ""
        function pick(i, guid) { functionsWindow.current = i; functionsWindow.currentGuid = guid }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            RowLayout {
                spacing: 0
                Label { text: qsTrId("lua_console.event"); Layout.preferredWidth: 150; font.bold: true }
                Label { text: qsTrId("common.name"); Layout.preferredWidth: 130; font.bold: true }
                Label { text: qsTrId("lua_console.guid"); Layout.fillWidth: true; font.bold: true }
            }
            ListView {
                id: functionList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: win.vm.rows("functions")
                ScrollBar.vertical: ScrollBar {}
                focus: true
                onCountChanged: functionsWindow.pick(-1, "")
                Keys.onPressed: (event) => {
                    if (functionsWindow.current < 0) return
                    if (event.key === Qt.Key_Delete) { removeButton.clicked(); event.accepted = true }
                    else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) { callButton.clicked(); event.accepted = true }
                }
                delegate: Rectangle {
                    id: fn
                    required property int index
                    required property var model
                    readonly property bool picked: fn.index === functionsWindow.current
                    width: functionList.width
                    height: 22
                    color: fn.picked ? palette.highlight : "transparent"
                    RowLayout {
                        anchors.fill: parent
                        spacing: 0
                        Label { text: fn.model.event; Layout.preferredWidth: 150; elide: Text.ElideRight; color: fn.picked ? palette.highlightedText : palette.text }
                        Label { text: fn.model.name; Layout.preferredWidth: 130; elide: Text.ElideRight; color: fn.picked ? palette.highlightedText : palette.text }
                        Label { text: fn.model.guid; Layout.fillWidth: true; elide: Text.ElideRight; font.family: "monospace"; color: fn.picked ? palette.highlightedText : palette.text }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onPressed: { functionsWindow.pick(fn.index, fn.model.guid); functionList.forceActiveFocus() }
                        onDoubleClicked: callButton.clicked()
                    }
                }
            }
            RowLayout {
                Button {
                    id: callButton
                    text: qsTrId("lua_console.call")
                    enabled: functionsWindow.current >= 0
                    onClicked: win.vm.run("callFunction", [functionsWindow.currentGuid])
                }
                Button {
                    id: removeButton
                    text: qsTrId("common.remove_mnemonic")
                    enabled: functionsWindow.current >= 0
                    onClicked: win.vm.run("removeFunction", [functionsWindow.currentGuid])
                }
                Button { text: qsTrId("lua_console.remove_all"); enabled: win.vm.functionCount > 0; onClicked: win.vm.run("removeAllFunctions") }
                Item { Layout.fillWidth: true }
                Button { text: qsTrId("common.ok_mnemonic"); onClicked: functionsWindow.close() }
            }
        }
    }

    // Chimera's Lua Functions: every function Chimera documents, in its words
    // (Return, Library, Name, Parameters, Description), those the window
    // lacks marked [Not available] and deprecated ones [Deprecated]; a column's
    // name sorts by it (again: the other way), Filter finds a name or a word of
    // the description, Copy (the right button or Ctrl+C) puts the call and its
    // example on the clipboard, OK closes
    ApplicationWindow {
        id: functionsListWindow
        objectName: "luaFunctionsListWindow"
        title: qsTrId("lua_console.lua_functions")
        width: 760
        height: 360
        minimumWidth: 200
        minimumHeight: 80
        property var entries: []
        // Chimera's sort: by library, then name, at first; a column's name sorts by it
        property int sortColumn: 1
        property bool descending: false
        property int picked: -1
        readonly property var keys: ["returns", "library", "name", "params", "description"]
        readonly property var widths: [64, 72, 160, 200, 300]
        onVisibleChanged: if (visible) {
            const all = JSON.parse(win.vm.run("functionsList") || "[]")
            all.sort((a, b) => a.library.localeCompare(b.library) || a.name.localeCompare(b.name))
            functionsListWindow.entries = all
            functionsListWindow.sortColumn = 1
            functionsListWindow.descending = false
            functionsListWindow.picked = -1
            filter.text = ""
            filter.forceActiveFocus()
        }
        function sortBy(column) {
            if (column === functionsListWindow.sortColumn) functionsListWindow.descending = !functionsListWindow.descending
            functionsListWindow.sortColumn = column
            const key = functionsListWindow.keys[column], down = functionsListWindow.descending
            const all = functionsListWindow.entries.slice()
            all.sort((a, b) => down ? b[key].localeCompare(a[key]) : a[key].localeCompare(b[key]))
            functionsListWindow.entries = all
        }
        readonly property var shown: functionsListWindow.entries.filter((e) => filter.text === ""
            || (e.library + "." + e.name).toLowerCase().indexOf(filter.text.toLowerCase()) >= 0
            || e.description.toLowerCase().indexOf(filter.text.toLowerCase()) >= 0)
        // Chimera's copy: "--library.name(parameters)", then the example
        function copyPicked() {
            const e = functionsListWindow.shown[functionsListWindow.picked]
            if (!e || !win.clipboard) return
            win.clipboard.setText("--" + e.library + "." + e.name + e.params + "\n" + (e.example !== "" ? e.example + "\n" : ""))
        }
        Shortcut { sequences: [StandardKey.Copy]; enabled: functionsListWindow.active && !filter.activeFocus; onActivated: functionsListWindow.copyPicked() }
        Menu {
            delegate: MenuEntry {}
            id: copyMenu
            MenuItem { text: qsTrId("lua_console.copy"); onTriggered: functionsListWindow.copyPicked() }
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: palette.base
                border.color: palette.mid
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 1
                    spacing: 0
                    Row {
                        Repeater {
                            model: [qsTrId("lua_console.return"), qsTrId("lua_console.library"), qsTrId("common.name"), qsTrId("lua_console.parameters"), qsTrId("lua_console.description")]
                            delegate: Label {
                                required property int index
                                required property string modelData
                                text: modelData
                                width: functionsListWindow.widths[index]
                                leftPadding: 4; topPadding: 2; bottomPadding: 2
                                elide: Text.ElideRight
                                background: Rectangle { color: palette.button; border.color: palette.mid }
                                TapHandler { onTapped: functionsListWindow.sortBy(parent.index) }
                            }
                        }
                    }
                    ListView {
                        id: docList
                        objectName: "luaFunctionsList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        ScrollBar.vertical: ScrollBar {}
                        model: functionsListWindow.shown
                        delegate: Rectangle {
                            id: doc
                            required property var modelData
                            required property int index
                            width: docList.width
                            height: 20
                            readonly property bool isPicked: doc.index === functionsListWindow.picked
                            color: doc.isPicked ? palette.highlight : "transparent"
                            readonly property color ink: doc.isPicked ? palette.highlightedText : palette.text
                            ToolTip.visible: docHover.hovered && doc.modelData.description !== ""
                            ToolTip.delay: 800
                            ToolTip.text: doc.modelData.description
                            HoverHandler { id: docHover }
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                Label { text: doc.modelData.returns; width: functionsListWindow.widths[0]; leftPadding: 4; elide: Text.ElideRight; color: doc.ink }
                                Label { text: doc.modelData.library; width: functionsListWindow.widths[1]; leftPadding: 4; elide: Text.ElideRight; color: doc.ink }
                                Label {
                                    text: (doc.modelData.unavailable ? qsTrId("lua_console.not_available") : "") + (doc.modelData.deprecated ? qsTrId("lua_console.deprecated") : "") + doc.modelData.name
                                    width: functionsListWindow.widths[2]; leftPadding: 4; elide: Text.ElideRight; color: doc.ink
                                }
                                Label { text: doc.modelData.params; width: functionsListWindow.widths[3]; leftPadding: 4; elide: Text.ElideRight; color: doc.ink }
                                Label { text: doc.modelData.description; width: functionsListWindow.widths[4]; leftPadding: 4; elide: Text.ElideRight; color: doc.ink }
                            }
                            TapHandler {
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onTapped: (point, button) => {
                                    functionsListWindow.picked = doc.index
                                    if (button === Qt.RightButton) copyMenu.popup()
                                }
                            }
                        }
                    }
                }
            }
            RowLayout {
                TextField { id: filter; Layout.preferredWidth: 180; onTextChanged: functionsListWindow.picked = -1 }
                Label { text: qsTrId("lua_console.filter") }
                Item { Layout.fillWidth: true }
                Button { text: qsTrId("common.ok_mnemonic"); Layout.preferredWidth: 80; onClicked: functionsListWindow.close() }
            }
        }
    }
}
