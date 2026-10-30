// The main window: layout and bindings only. Every decision is the view
// models' (session, pianoRoll); this file says where things go and which
// command a gesture is.
import QtQuick
import QtQuick.Controls
import ChimeraDialogs
import QtQuick.Layouts
import Chimera
import "Shapes.js" as Shapes

ApplicationWindow {
    id: root
    required property QtObject session
    required property QtObject pianoRoll
    required property QtObject ramSearch
    required property QtObject ramWatch
    required property QtObject newProject
    required property QtObject hexEditor
    required property QtObject controls
    required property QtObject hotkeys
    required property QtObject keyNamer
    required property QtObject hotkeyRelay
    required property QtObject projectDetails
    required property QtObject themes
    required property QtObject themeColors
    required property QtObject luaConsole
    required property QtObject toolWindows
    required property QtObject gameProperties
    required property QtObject branches
    required property QtObject cores
    required property QtObject clipboard
    required property QtObject pictureFiles
    // Capture Mouse: keeps the pointer over the picture
    required property QtObject pointerCapture
    // the pointer's buttons, routed as keys (to the machine, a hotkey, or both)
    required property QtObject buttonRouter
    required property QtObject help
    // Tools > MCP Server...: an AI agent working on the open project
    required property QtObject mcp
    required property QtObject mediaMaker
    required property QtObject cacheManager
    required property QtObject customize
    required property QtObject messages
    required property QtObject display
    required property QtObject paths
    required property QtObject firmware
    required property QtObject configFiles
    required property QtObject dataFolder
    // the machine's settings, for File > Project Settings
    required property QtObject settings
    required property QtObject precompiled
    required property QtObject traceLogger
    // "debugger" is a word of JavaScript's own
    required property QtObject debuggerTool
    required property QtObject surfaceViewer
    required property QtObject guiOverlay
    required property QtObject idleEye
    required property QtObject macroTool
    required property QtObject encoder
    required property QtObject hostInput
    required property QtObject forms
    // a Paths folder for a file dialog to open in (made when it is not there)
    function folderOf(name) {
        root.paths.changed; root.paths.base; root.paths.lastRomFolder; root.paths.recentForRoms
        return root.pictureFiles.folderUrl(root.paths.run("dialogFolder", [name]) || "")
    }

    // the picture alone: TAStudio is a window of its own beside it
    width: 800
    height: 680
    // none is shown in a run nobody watches (--headless)
    visible: !headlessRun.enabled
    // Chimera's title: the speed while nothing is drawn, the game and its system, the project's file
    // (marked when it has changes), then "Chimera" or the words Display's Misc gives; only those
    // last words when titles are kept static
    readonly property string titleSuffix: root.display.titleSuffix.trim() !== "" ? root.display.titleSuffix : qsTrId("main.chimera")
    title: root.display.staticWindowTitles ? root.titleSuffix
         : (session.displayNothing ? qsTrId("main.fps").arg(Math.round(session.fps)) : "")
         + (session.isOpen ? qsTrId("main.text").arg(session.gameName).arg(session.systemName) : "")
         + (session.isOpen && session.hasProject
            ? (session.unsaved ? "*" : "") + session.projectPath.replace(/^.*[\\/]/, "") + " - " : "")
         // said in the title for as long as it is so, so it is not left on unnoticed
         + (root.mcp.started ? qsTrId("main.mcp_server_started") : "")
         + root.titleSuffix
    // Display's window chrome, as Chimera does: windowed, the caption's buttons, the frame and
    // the menu; full screen, the menu; kept over other windows when asked
    flags: Qt.Window
         | (root.visibility !== Window.FullScreen && (root.display.frameWindowed === 0 || root.chromeless) ? Qt.FramelessWindowHint
            : Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowCloseButtonHint
              | (root.display.captionWindowed ? Qt.WindowMinMaxButtonsHint : 0))
         | (root.display.stayOnTop || (root.capturing && root.display.mouseCaptureForcesTop) ? Qt.WindowStaysOnTopHint : 0)
    // Chimera's --chromeless: no menu, status bar or frame, full screen or not
    property bool chromeless: false
    function synchChrome() { root.menuBar.visible = !root.chromeless && (root.visibility === Window.FullScreen ? root.display.menuFullscreen : root.display.menuWindowed) }
    onVisibilityChanged: {
        root.synchChrome()
        // a size that waited for the window to be windowed again
        if (root.visibility === Window.Windowed && root.resizePending) {
            root.resizePending = false
            root.frameBufferResized(false)
        }
    }
    Connections {
        target: root.display
        function onMenuWindowedChanged() { root.synchChrome() }
        function onMenuFullscreenChanged() { root.synchChrome() }
    }
    Component.onCompleted: root.synchChrome()

    // the theme's colours for what is drawn over the picture, as Chimera's themes give them
    readonly property var themeColorMap: JSON.parse(root.themes.colors)
    function osdColor(role, fallback) { return root.themeColors.resolve(root.themeColorMap[role] || fallback) }
    // Config > Messages: a colour left at Chimera's default is the theme's, one chosen there wins
    function messageColor(chosen, chimeraDefault, role) {
        return chosen.toUpperCase() === chimeraDefault ? root.osdColor(role, "#" + chimeraDefault.substring(3)) : chosen
    }
    readonly property color osdMessageColor: root.messageColor(root.messages.messagesColor, "#FFFFFFFF", "OsdMessage")
    readonly property color osdAlertColor: root.messageColor(root.messages.alertColor, "#FFFF0000", "OsdAlert")
    readonly property color osdMovieInputColor: root.messageColor(root.messages.movieInputColor, "#FF808080", "OsdMovieInput")
    // where each thing over the picture goes: a corner and a distance from it, as Chimera does
    readonly property var osdPositions: JSON.parse(root.messages.positions)
    // Config > Display's Scale OSD text with system scale: off, what is over
    // the picture keeps its size at 100% (the screen's pixels), as Chimera's;
    // and Scale OSD text with the window: its 12 pixels are a 480-pixel-tall
    // picture area's, and it grows with the window, never smaller (issue 401)
    readonly property real osdScale: (root.display.scaleOsd || root.Screen.devicePixelRatio <= 0 ? 1 : 1 / root.Screen.devicePixelRatio)
        * (root.display.osdFollowsWindow ? Math.max(1, pictureArea.height / 480) : 1)
    function osdX(name, w) {
        const p = root.osdPositions[name]
        if (!p) return 0
        return p.anchor === 0 || p.anchor === 2 ? p.x * root.osdScale : pictureArea.width - p.x * root.osdScale - w
    }
    // how wide a thing over the picture may be: the picture's width less its
    // distance from the side, on both sides (a long message wraps, issue 378)
    function osdRoom(name, w) {
        const p = root.osdPositions[name]
        return Math.max(40, w - 2 * (p ? p.x : 0) * root.osdScale)
    }
    function osdY(name, h) {
        const p = root.osdPositions[name]
        if (!p) return 0
        return p.anchor < 2 ? p.y * root.osdScale : pictureArea.height - p.y * root.osdScale - h
    }
    // Encode Video's Capture OSD: what is over the picture now, as lines of
    // text placed for an area w by h at the window's scale (the encoder
    // paints them over each frame as the picture area shows it): {text, x,
    // y, color} each, the text 12 * osdScale pixels, a line 14 * osdScale high
    TextMetrics { id: osdMetrics; font.family: "monospace"; font.pixelSize: Math.round(12 * root.osdScale) }
    function osdLayer(w, h) {
        const out = []
        if (!root.session.isOpen || root.session.displayNothing) return out
        const k = root.osdScale, lh = 14 * k
        function widthOf(t) { osdMetrics.text = t; return osdMetrics.advanceWidth }
        // a message longer than the picture is wide, in lines that fit, broken between words
        function wrap(t, room) {
            const lines = []
            let line = ""
            for (const word of t.split(" ")) {
                const longer = line === "" ? word : line + " " + word
                if (line !== "" && widthOf(longer) > room) { lines.push(line); line = word } else line = longer
            }
            lines.push(line)
            return lines
        }
        function place(name, lines, color) {
            const p = root.osdPositions[name]
            lines = lines.filter(t => t !== "")
            if (!p || lines.length === 0) return
            const bw = Math.max(...lines.map(widthOf)), bh = lines.length * lh
            const x = p.anchor === 0 || p.anchor === 2 ? p.x * k : w - p.x * k - bw
            const y = p.anchor < 2 ? p.y * k : h - p.y * k - bh
            lines.forEach((t, i) => out.push({text: t, x: Math.round(x), y: Math.round(y + i * lh), color: String(color)}))
        }
        place("Fps", [[root.session.showFps ? root.session.fpsText : "",
                       root.session.showAverageFps ? root.session.averageFpsText : ""].filter(t => t !== "").join("   ")], root.osdMessageColor)
        if (root.session.showGameTime) place("Game Time", [root.session.gameTimeText], root.osdMessageColor)
        if (root.session.showRerecords) place("Rerecords", [String(root.session.rerecords)], root.osdMessageColor)
        if (root.session.showMessages) {
            const all = []
            for (let i = 0; i < onScreenMessages.count; i++) all.push(onScreenMessages.get(i).message)
            const shown = root.messages.stackMessages ? all : all.slice(-1)
            const p = root.osdPositions["Messages"]
            const room = w - 2 * (p ? p.x : 0) * k
            place("Messages", (p && p.anchor < 2 ? shown.reverse() : shown).flatMap(m => wrap(m, room)), root.osdMessageColor)
        }
        if (root.session.showFrameCounter)
            place("Frame Counter", [root.session.frameCounterText], root.session.lagFrame ? root.osdAlertColor : root.osdMessageColor)
        if (root.session.showInput) place("Input Display", [root.session.inputText], root.osdMovieInputColor)
        if (root.session.showLagCounter) place("Lag Counter", [String(root.session.lagCount)], root.osdAlertColor)
        if (root.session.stickyArmed) place("Autohold", [root.session.stickyText], root.osdColor("OsdAutoHold", "#FFFFFF"))
        for (const s of JSON.parse(root.projectDetails.onScreen))
            out.push({text: s.text, x: Math.round(s.x), y: Math.round(s.y), color: String(s.color)})
        if (root.ramWatch.onScreenText !== "") place("Watches", root.ramWatch.onScreenText.split("\n"), "white")
        return out
    }
    // the keys of every hotkey, for the menus to show beside their actions
    readonly property var hotkeyKeys: JSON.parse(root.hotkeys.bound)
    // Chimera's Capture Mouse: while the window is active the pointer is hidden and kept over the picture
    readonly property bool capturing: root.session.captureMouse && root.active
    onCapturingChanged: root.capturing ? root.pointerCapture.confine(pictureArea) : root.pointerCapture.release()
    // a menu item that shows the keys of the hotkey its action names
    Component {
        id: keyedMenuItem
        MenuEntry {
            id: keyedItem
            readonly property string keys: action && action.hotkey ? (root.hotkeyKeys[action.hotkey] || "") : ""
            rightPadding: padding + (keys !== "" ? keyLabel.implicitWidth + 24 : 0)
            Label {
                id: keyLabel
                text: keyedItem.keys
                anchors.right: parent.right
                anchors.rightMargin: keyedItem.padding
                anchors.verticalCenter: parent.verticalCenter
                color: keyedItem.highlighted ? keyedItem.palette.highlightedText : keyedItem.palette.text
            }
        }
    }
    // Chimera asks before a project with changes is left - closed, another
    // opened, the program quit - and saves it first when told to
    property var afterAsk: null
    property var afterSave: null
    property bool leaving: false
    // asked from a window that is modal over this one (New Project), the
    // question and the Save As picker are shown over that window, which
    // takes the clicks while it is open (issue 392)
    function leaveProject(then, over) {
        // Customize's "never ask to save": the changes are let go
        if (!root.session.unsaved || root.customize.neverAskSave) { then(); return }
        root.afterAsk = then
        askSaveDialog.parent = over ? over.contentItem : root.contentItem
        saveAsDialog.parentWindow = over ? over : root
        askSaveDialog.open()
    }
    // Save Project As, as Chimera's: its own file once it has one, else the
    // game's name in the Movies folder (issue 399)
    function askSaveAs() {
        const folder = root.paths.run("dialogFolder", ["Movies"]) || ""
        const suggested = root.session.run("suggestedProjectFile", [folder]) || folder
        saveAsDialog.currentFolder = root.pictureFiles.folderUrl(suggested === folder ? folder : suggested.replace(/[\\/][^\\/]*$/, ""))
        if (suggested !== folder) saveAsDialog.selectedFile = root.pictureFiles.folderUrl(suggested)
        saveAsDialog.open()
    }
    function quitProgram() { root.leaveProject(() => { root.leaving = true; Qt.quit() }) }
    // a project started: the open one's changes asked about, then Chimera's
    // question when freezes are on (Yes keeps them, No switches them off,
    // Cancel opens nothing), then the project
    property var afterCheats: null
    function openProject(path, packagePath) {
        root.leaveProject(() => {
            const open = () => root.session.run("openProject", packagePath ? [path, packagePath] : [path])
            if (root.session.frozenCount === 0) { open(); return }
            root.afterCheats = open
            cheatsDialog.open()
        })
    }
    // a file a later start of the program handed over (Customize's single
    // instance mode), as Chimera does: a project as Open Project
    // opens one (the open project's changes asked about first), with the core
    // package named after it when there is one; a game starts New Project
    function openHanded(path, packagePath) {
        if (path.toLowerCase().endsWith(".chimeraproject")) { root.openProject(path, packagePath); return }
        root.newProjectFrom(path)
    }
    // files dropped on the window, sorted by kind as Chimera's are (the tool
    // windows' sortFiles): a game starts New Project from it, a watch list
    // opens in RAM Watch, a Lua session and a Lua script in the Lua Console, a
    // project as Open Project opens one; what does not open is said
    function openFiles(files) {
        const paths = []
        // a dropped file is a URL; a handed-over one a path already
        for (const f of files) paths.push(typeof f === "string" && !f.startsWith("file:") ? f : localFiles.localPath(f))
        const steps = root.toolWindows.run("sortFiles", [paths.join("\n")]).split("\n")
        for (const step of steps) {
            const part = step.split("\t")
            if (part[0] === "say") { root.note(part[1]); continue }
            if (part[0] !== "open") continue
            const kind = part[1], path = part.slice(2).join("\t")
            if (kind === "game") root.newProjectFrom(path)
            else if (kind === "watch") { root.ramWatch.run("open", [path, false]); root.bring(ramWatchWindow) }
            else if (kind === "luaSession") { root.luaConsole.run("openSession", [path]); root.bring(luaConsoleWindow) }
            else if (kind === "lua") { root.luaConsole.run("openScript", [path]); root.bring(luaConsoleWindow) }
            else if (kind === "project") root.openProject(path)
        }
    }
    // a window shown and brought over the others
    function bring(w) { if (headlessRun.enabled) return; w.show(); w.raise() }
    // a game file named at the start or handed over: a game runs only in a
    // project (owner, 2026-10-26), so New Project starts from it, as a file
    // dropped on the window does
    function newProjectFrom(path) {
        // the wizard waits for a person
        if (headlessRun.enabled) { headlessRun.blocked(qsTrId("main.new_project"), path); return }
        root.newProject.run("startFrom", [path])
        newProjectWindow.show()
        newProjectWindow.raise()
    }
    // a script's client.exit, as Chimera's: nobody is asked on the way out. The
    // script's unsaved changes are let go, the project on disk as it was, and said
    Connections {
        target: root.toolWindows
        function onExitAskedChanged() {
            const code = root.toolWindows.exitAsked
            if (code < 0) return
            if (root.session.unsaved)
                root.toolWindows.run("tell", ["[tastudio] closing unattended with unsaved changes; the project on disk is unchanged"])
            root.leaving = true
            Qt.exit(code)
        }
    }
    onClosing: (close) => {
        if (root.session.unsaved && !root.leaving && !root.customize.neverAskSave) {
            close.accepted = false
            root.quitProgram()
            return
        }
        // the program quits with this window, and TAStudio is closed on the way
        // without closing the project first
        root.leaving = true
    }
    // frames copied or cut, by a menu, a script or a tool, go on the system's clipboard
    Connections {
        target: root.pianoRoll
        function onClipboardTextChanged() { root.clipboard.setText(root.pianoRoll.clipboardText) }
    }
    Connections {
        target: root.session
        // what waited for the save goes on once it is written
        function onUnsavedChanged() {
            if (!root.session.unsaved && root.afterSave) {
                const then = root.afterSave
                root.afterSave = null
                then()
            }
        }
    }
    // what a hotkey does when it is the window's: a menu's action, a dialog, a
    // window; TAStudio's editing is TAStudio's
    readonly property var hotkeyActions: ({
        "RAM Search": ramSearchAction, "RAM Watch": ramWatchAction, "Hex Editor": hexEditorAction, "Lua Console": luaConsoleAction,
        "Open Project": openProjectAction, "Close Project": closeAction, "Trace Logger": traceLoggerAction
    })
    Connections {
        target: root.hotkeyRelay
        function onPressed(action) {
            if (action === "Full Screen") root.visibility = root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
            else if (action === "Exit Program") root.quitProgram()
            else if (action === "Load Last Project") {
                const last = root.session.recentProjects.split("\n")[0]
                if (last !== "") root.openProject(last)
            }
            else if (action === "Seek To...") { if (root.session.seekEnabled) seekDialog.open() }
            else if (action === "Toggle Menu") root.menuBar.visible = !root.menuBar.visible
            else if (action === "TAStudio") { if (root.session.hasProject) tastudioWindow.engage() }
            // Chimera's Capture Mouse: only with something bound to the pointer, and said
            else if (action === "Capture Mouse") {
                if (!root.session.mouseBound) root.note(qsTrId("main.nothing_bound_to_mouse_not_capturing"))
                else {
                    root.session.captureMouse = !root.session.captureMouse
                    if (root.session.captureMouse) root.note(qsTrId("main.mouse_cursor_captured_press_to_uncapture").arg(root.hotkeyKeys["Capture Mouse"] || ""), 7)
                    else root.note(qsTrId("main.mouse_cursor_uncaptured"))
                }
            }
            else if (action === "Toggle Stay on Top") {
                root.display.stayOnTop = !root.display.stayOnTop
                root.note(root.display.stayOnTop ? qsTrId("main.stay_on_top_enabled") : qsTrId("main.stay_on_top_disabled"))
            }
            else if (action === "Play Movie") root.leaveProject(() => projectDialog.open())
            else if (action === "Record Movie") { root.newProject.run("openOnLastProject"); root.bring(newProjectWindow) }
            else if (action === "Stop Movie") { if (root.session.hasProject) root.leaveProject(() => root.session.run("stopMovie")) }
            else if (action === "Play from beginning") {
                // as TAStudio's: the project as its file has it, from frame 0
                if (root.session.projectPath !== "") root.openProject(root.session.projectPath)
            }
            else if (action === "Save Movie") {
                if (root.session.hasProject) root.session.projectPath === "" ? root.askSaveAs() : root.session.run("saveProject")
            }
            else if (action === "Encode Video") root.encodeVideo()
            else if (action === "Larger Window" || action === "Smaller Window") {
                // as Chimera's: the system's scale one up or down (1 to 10), said, and the window sized to it
                if (!root.session.isOpen) return
                const was = root.display.run("scaleFor", [root.session.systemId]) || 2
                const n = Math.max(1, Math.min(10, was + (action === "Larger Window" ? 1 : -1)))
                root.display.run("setScaleFor", [root.session.systemId, n])
                root.note(qsTrId("main.screensize_set_to_x").arg(n))
                root.frameBufferResized(true)
            }
            else if (root.hotkeyActions[action] && root.hotkeyActions[action].enabled) root.hotkeyActions[action].trigger()
            else if (tastudioWindow.hotkeyActions[action] && tastudioWindow.hotkeyActions[action].enabled) tastudioWindow.hotkeyActions[action].trigger()
        }
    }

    menuBar: RowLayout {
        spacing: 0
        // Chimera's menus, in its order: File, System, View, Config, Tools,
        // Emulator (Game for a game core), Help. System and Emulator are there
        // only while a machine runs, as Chimera's are
        MenuBar {
            Layout.fillWidth: true
            delegate: MenuBarItem {
                id: menuBarItem
                visible: menuBarItem.menu !== null && menuBarItem.menu.shown !== false
                width: visible ? implicitWidth : 0
            }
            Menu {
                // Customize's "pause when a menu is open"
                delegate: keyedMenuItem
                title: qsTrId("common.file")
                // opens on the last project's answers, as Chimera's does
                Action { text: qsTrId("main.new_chimera_project"); icon.source: "qrc:/chimera/menu/NewProject.png"; onTriggered: { root.newProject.run("openOnLastProject"); root.bring(newProjectWindow) } }
                Action { id: openProjectAction; property string hotkey: "Open Project"; text: qsTrId("main.open_project"); icon.source: "qrc:/chimera/menu/OpenProject.png"; onTriggered: root.leaveProject(() => projectDialog.open()) }
                Menu {
                    delegate: keyedMenuItem
                    id: recentMenu
                    title: qsTrId("main.recent_projects"); icon.source: "qrc:/chimera/menu/RecentProjects.png"
                    enabled: root.session.recentProjects !== ""
                    // as wide as the longest path, up to most of the screen; each path whole in its tooltip
                    property real widest: 0
                    width: Math.min(Math.max(200, recentMenu.widest), Math.max(400, Screen.desktopAvailableWidth * 0.7))
                    function fitWidth() {
                        let w = 0
                        for (let i = 0; i < recentMenu.count; i++) {
                            const item = recentMenu.itemAt(i)
                            if (item) w = Math.max(w, item.implicitWidth)
                        }
                        recentMenu.widest = w + recentMenu.leftPadding + recentMenu.rightPadding
                    }
                    Instantiator {
                        model: root.session.recentProjects === "" ? [] : root.session.recentProjects.split("\n")
                        delegate: MenuItem {
                            required property string modelData
                            text: modelData
                            onTriggered: root.openProject(modelData)
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            ToolTip.text: modelData
                        }
                        onObjectAdded: (index, object) => { recentMenu.insertItem(index, object); Qt.callLater(recentMenu.fitWidth) }
                        onObjectRemoved: (index, object) => { recentMenu.removeItem(object); Qt.callLater(recentMenu.fitWidth) }
                    }
                }
                // a project not saved yet asks where it goes
                Action { text: qsTrId("main.save_project"); icon.source: "qrc:/chimera/menu/SaveProject.png"; shortcut: "Ctrl+S"; enabled: root.session.hasProject; onTriggered: root.session.projectPath === "" ? root.askSaveAs() : root.session.run("saveProject") }
                Action { text: qsTrId("main.save_project_as"); icon.source: "qrc:/chimera/menu/SaveProjectAs.png"; shortcut: "Ctrl+Shift+S"; enabled: root.session.hasProject; onTriggered: root.askSaveAs() }
                Action { text: qsTrId("main.save_project_backup"); icon.source: "qrc:/chimera/menu/SaveProjectBackup.png"; enabled: root.session.projectPath !== ""; onTriggered: root.session.run("saveBackup") }
                // the open project's settings, changed: its machine starts again with them (issue 370)
                Action { text: qsTrId("main.project_settings"); icon.source: "qrc:/chimera/menu/Customize.png"; enabled: root.session.hasProject && root.session.isOpen; onTriggered: { root.bring(projectSettingsWindow) } }
                Action { id: closeAction; property string hotkey: "Close Project"; text: qsTrId("main.close_project_mnemonic"); icon.source: "qrc:/chimera/menu/CloseProject.png"; enabled: root.session.closeEnabled; onTriggered: root.leaveProject(() => root.session.run("close")) }
                MenuDivider {}
                Action {
                    property string hotkey: "Encode Video"
                    text: qsTrId("main.encode_video"); icon.source: "qrc:/chimera/menu/EncodeVideo.png"
                    onTriggered: root.encodeVideo()
                }
                Menu {
                    delegate: keyedMenuItem
                    title: qsTrId("main.screenshot_mnemonic"); icon.source: "qrc:/chimera/menu/Screenshot.png"
                    enabled: root.session.isOpen
                    Action { property string hotkey: "Screenshot"; text: qsTrId("main.screenshot"); icon.source: "qrc:/chimera/menu/Screenshot.png"; onTriggered: root.hotkeys.run("run", ["Screenshot"]) }
                    Action {
                        text: qsTrId("main.screenshot_as"); icon.source: "qrc:/chimera/menu/ScreenshotAs.png"
                        onTriggered: {
                            const suggested = root.pictureFiles.screenshotSuggestion(root.session.gameName)
                            screenshotAsDialog.currentFolder = suggested.toString().substring(0, suggested.toString().lastIndexOf("/"))
                            screenshotAsDialog.selectedFile = suggested
                            screenshotAsDialog.open()
                        }
                    }
                    Action { property string hotkey: "Screen Raw to Clipboard"; text: qsTrId("main.screenshot_raw_clipboard"); onTriggered: root.hotkeys.run("run", ["Screen Raw to Clipboard"]) }
                    Action { property string hotkey: "Screen Client to Clipboard"; text: qsTrId("main.screenshot_client_clipboard"); onTriggered: root.hotkeys.run("run", ["Screen Client to Clipboard"]) }
                    MenuDivider {}
                    // the counters and messages over the picture in the screenshot too
                    Action { text: qsTrId("main.capture_osd"); checkable: true; checked: root.session.captureOsd; onTriggered: root.session.captureOsd = checked }
                }
                Action { property string hotkey: "Exit Program"; text: qsTrId("main.exit"); shortcut: "Alt+F4"; onTriggered: root.quitProgram() }
            }
            Menu {
                readonly property bool shown: root.session.isOpen
                delegate: keyedMenuItem
                title: qsTrId("main.system")
                Action {
                    property string hotkey: "Pause"
                    text: qsTrId("main.pause"); icon.source: "qrc:/chimera/menu/Pause.png"; checkable: true; checked: !root.session.running
                    onTriggered: root.session.run(root.session.running ? "pause" : "play")
                }
                MenuDivider {}
                Action { property string hotkey: "Reboot Core"; text: qsTrId("main.reboot_core"); icon.source: "qrc:/chimera/menu/RebootCore.png"; enabled: root.session.restartEnabled; onTriggered: root.session.run("restart") }
                MenuDivider {}
                Action { text: qsTrId("main.loaded_core").arg(root.session.coreName).arg(root.session.systemId); enabled: false }
            }
            Menu {
                delegate: keyedMenuItem
                title: qsTrId("main.view")
                // the picture N times the machine's own, the window grown or shrunk to it
                Menu {
                    delegate: MenuEntry {}
                    id: windowSizeMenu
                    title: qsTrId("main.window_size"); icon.source: "qrc:/chimera/menu/WindowSize.png"
                    enabled: picture.pictureWidth > 0
                    // the sizes this screen cannot hold say so and cannot be picked; the check is on the size the window is
                    property int fits: 10
                    onAboutToShow: windowSizeMenu.fits = root.largestFit()
                    Instantiator {
                        model: 10
                        delegate: MenuItem {
                            required property int index
                            readonly property bool tooBig: index + 1 > windowSizeMenu.fits
                            text: (index + 1 >= 10 ? "1" : "") + "&" + ((index + 1) % 10) + "x" + (tooBig ? qsTrId("main.larger_than_this_screen") : "")
                            enabled: !tooBig
                            checkable: true
                            checked: root.windowScale === index + 1 && root.display.resizeWithPicture
                            onTriggered: {
                                checked = Qt.binding(() => root.windowScale === index + 1 && root.display.resizeWithPicture)
                                root.sizeWindow(index + 1)
                            }
                        }
                        onObjectAdded: (index, object) => windowSizeMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => windowSizeMenu.removeItem(object)
                    }
                    MenuDivider {}
                    // Chimera's: the window keeps its size when the picture's changes
                    MenuItem {
                        text: qsTrId("main.static_size")
                        checkable: true
                        checked: !root.display.resizeWithPicture
                        onTriggered: {
                            root.display.resizeWithPicture = !checked
                            checked = Qt.binding(() => !root.display.resizeWithPicture)
                            root.frameBufferResized(false)
                        }
                    }
                }
                Action { property string hotkey: "Full Screen"; text: qsTrId("main.switch_to_fullscreen"); icon.source: "qrc:/chimera/menu/Fullscreen.png"; onTriggered: root.visibility = root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen }
                Action { property string hotkey: "Display FPS"; text: qsTrId("main.display_fps"); checkable: true; checked: root.session.showFps; onTriggered: root.session.showFps = checked }
                Action { text: qsTrId("main.display_average_fps"); checkable: true; checked: root.session.showAverageFps; onTriggered: root.session.showAverageFps = checked }
                Action { text: qsTrId("main.reset_average_fps"); enabled: root.session.showAverageFps; onTriggered: root.session.run("resetAverageFps") }
                Action { property string hotkey: "Frame Counter"; text: qsTrId("main.display_frame_count"); checkable: true; checked: root.session.showFrameCounter; onTriggered: root.session.showFrameCounter = checked }
                Action { property string hotkey: "Lag Counter"; text: qsTrId("main.display_lag_frame_count"); checkable: true; checked: root.session.showLagCounter; onTriggered: root.session.showLagCounter = checked }
                Action { property string hotkey: "Input Display"; text: qsTrId("main.display_input"); checkable: true; checked: root.session.showInput; onTriggered: root.session.showInput = checked }
                Action { text: qsTrId("main.display_rerecord_count"); checkable: true; checked: root.session.showRerecords; onTriggered: root.session.showRerecords = checked }
                Action { text: qsTrId("main.display_subtitles"); checkable: true; checked: root.projectDetails.showSubtitles; onTriggered: root.projectDetails.showSubtitles = checked }
                MenuDivider {}
                Action { text: qsTrId("main.display_status_bar"); checkable: true; checked: root.session.showStatusBar; onTriggered: root.session.showStatusBar = checked }
                Action { text: qsTrId("main.display_messages"); checkable: true; checked: root.session.showMessages; onTriggered: root.session.showMessages = checked }
                Action { text: qsTrId("main.open_log_window"); icon.source: "qrc:/chimera/menu/LogWindow.png"; onTriggered: { root.bring(logWindow) } }
            }
            Menu {
                delegate: keyedMenuItem
                title: qsTrId("main.config_mnemonic")
                Action { text: qsTrId("main.controllers"); icon.source: "qrc:/chimera/menu/Controllers.png"; onTriggered: { root.bring(controllerWindow) } }
                Action { text: qsTrId("main.hotkeys"); icon.source: "qrc:/chimera/menu/Hotkeys.png"; onTriggered: { root.bring(hotkeyWindow) } }
                Action { text: qsTrId("main.display"); icon.source: "qrc:/chimera/menu/Display.png"; onTriggered: { root.bring(displayWindow) } }
                Action { text: qsTrId("main.sound"); icon.source: "qrc:/chimera/menu/Sound.png"; onTriggered: { root.bring(soundWindow) } }
                Action { text: qsTrId("main.paths"); icon.source: "qrc:/chimera/menu/Paths.png"; onTriggered: { root.bring(pathsWindow) } }
                Action { text: qsTrId("main.firmware"); icon.source: "qrc:/chimera/menu/Firmware.png"; onTriggered: { root.bring(firmwareWindow) } }
                Action { text: qsTrId("main.data_directory"); icon.source: "qrc:/chimera/menu/DataDirectory.png"; onTriggered: { root.bring(dataFolderWindow) } }
                Action { text: qsTrId("main.messages"); icon.source: "qrc:/chimera/menu/Messages.png"; onTriggered: { root.bring(messagesWindow) } }
                Action { text: qsTrId("main.theme"); icon.source: "qrc:/chimera/menu/ThemePalette.png"; onTriggered: { root.bring(themesWindow) } }
                Action { text: qsTrId("main.autofire"); icon.source: "qrc:/chimera/menu/Autofire.png"; onTriggered: autofireDialog.open() }
                Action { text: qsTrId("main.customize_mnemonic"); icon.source: "qrc:/chimera/menu/Customize.png"; onTriggered: { root.bring(customizeWindow) } }
                // the window's words in another language, at once (issue 376); its English name beside a
                // translated one, so a person who picked a language they cannot read finds the way back
                Menu {
                    delegate: keyedMenuItem
                    id: languageMenu
                    title: qsTrId("main.language_menu"); icon.source: "qrc:/chimera/menu/Language.png"
                    Instantiator {
                        model: root.customize.languages.split("\n")
                        delegate: MenuItem {
                            required property string modelData
                            readonly property string code: modelData.split("\t")[0]
                            text: modelData.split("\t")[1]
                            checkable: true
                            checked: root.customize.language === code
                            onTriggered: root.customize.language = code
                        }
                        onObjectAdded: (index, object) => languageMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => languageMenu.removeItem(object)
                    }
                }
                MenuDivider {}
                Menu {
                    delegate: keyedMenuItem
                    title: qsTrId("main.speed_skip"); icon.source: "qrc:/chimera/menu/SpeedSkip.png"
                    // one of the three, or none: each ticked turns the others off, as Chimera's
                    Action {
                        text: qsTrId("main.clock_throttle"); checkable: true; checked: root.session.throttle === "clock"
                        onTriggered: root.session.throttle = root.session.throttle === "clock" ? "" : "clock"
                    }
                    Action {
                        text: qsTrId("main.audio_throttle"); checkable: true; checked: root.session.throttle === "audio"; enabled: root.session.soundOn
                        onTriggered: root.session.throttle = root.session.throttle === "audio" ? "" : "audio"
                    }
                    Action {
                        text: qsTrId("main.vsync_throttle"); checkable: true; checked: root.session.throttle === "vsync"
                        onTriggered: {
                            root.session.throttle = root.session.throttle === "vsync" ? "" : "vsync"
                            // the throttle needs the window at the screen's refresh
                            if (root.session.throttle === "vsync" && !root.session.vsyncEnabled) {
                                root.session.vsyncEnabled = true
                                root.note(qsTrId("main.display_vsync_is_set_to_on"))
                            }
                        }
                    }
                    MenuDivider {}
                    Action {
                        text: qsTrId("main.vsync_enabled"); checkable: true; checked: root.session.vsyncEnabled
                        onTriggered: {
                            root.session.vsyncEnabled = checked
                            root.note(qsTrId("main.display_vsync_is_set_to_from").arg(checked ? qsTrId("main.on") : qsTrId("main.off")))
                        }
                    }
                    MenuDivider {}
                    Action {
                        property string hotkey: "Toggle Throttle"
                        text: qsTrId("main.unthrottled"); checkable: true; checked: root.session.unthrottled
                        onTriggered: root.session.unthrottled = checked
                    }
                    // the frame skip's one setting left (owner, 2026-10-24)
                    Menu {
                        delegate: MenuEntry {}
                        id: fastDrawMenu
                        title: qsTrId("main.turbo_unthrottled_draws")
                        Instantiator {
                            model: 10
                            delegate: MenuItem {
                                required property int index
                                text: index === 0 ? qsTrId("main.every_frame") : qsTrId("main.1_in").arg(index + 1)
                                checkable: true
                                checked: root.session.fastDrawOneIn === index + 1
                                onTriggered: root.session.fastDrawOneIn = index + 1
                            }
                            onObjectAdded: (index, object) => fastDrawMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => fastDrawMenu.removeItem(object)
                        }
                    }
                    MenuDivider {}
                    Action { text: qsTrId("main.speed_50"); enabled: root.session.throttle === "clock"; checkable: true; checked: root.session.speedPercent === 50; onTriggered: root.session.speedPercent = 50 }
                    Action { text: qsTrId("main.speed_75"); enabled: root.session.throttle === "clock"; checkable: true; checked: root.session.speedPercent === 75; onTriggered: root.session.speedPercent = 75 }
                    Action { text: qsTrId("main.speed_100"); enabled: root.session.throttle === "clock"; checkable: true; checked: root.session.speedPercent === 100; onTriggered: root.session.speedPercent = 100 }
                    Action { text: qsTrId("main.speed_150"); enabled: root.session.throttle === "clock"; checkable: true; checked: root.session.speedPercent === 150; onTriggered: root.session.speedPercent = 150 }
                    Action { text: qsTrId("main.speed_200"); enabled: root.session.throttle === "clock"; checkable: true; checked: root.session.speedPercent === 200; onTriggered: root.session.speedPercent = 200 }
                    Action { text: qsTrId("main.speed_400"); enabled: root.session.throttle === "clock"; checkable: true; checked: root.session.speedPercent === 400; onTriggered: root.session.speedPercent = 400 }
                }
                // for a key that is a hotkey and a button
                Menu {
                    delegate: keyedMenuItem
                    title: qsTrId("main.key_priority"); icon.source: "qrc:/chimera/menu/KeyPriority.png"
                    Action { text: qsTrId("main.both_hotkeys_and_controllers"); checkable: true; checked: root.hotkeys.keyPriority === 0; onTriggered: root.hotkeys.keyPriority = 0 }
                    Action { text: qsTrId("main.input_overrides_hotkeys"); checkable: true; checked: root.hotkeys.keyPriority === 1; onTriggered: root.hotkeys.keyPriority = 1 }
                    Action { text: qsTrId("main.hotkeys_override_input"); checkable: true; checked: root.hotkeys.keyPriority === 2; onTriggered: root.hotkeys.keyPriority = 2 }
                }
                MenuDivider {}
                Action { text: qsTrId("main.save_config"); icon.source: "qrc:/chimera/menu/SaveConfig.png"; onTriggered: root.configFiles.run("save") }
                Action { text: qsTrId("main.save_config_as"); icon.source: "qrc:/chimera/menu/SaveConfigAs.png"; onTriggered: saveConfigDialog.open() }
                Action { text: qsTrId("main.load_config"); icon.source: "qrc:/chimera/menu/LoadConfig.png"; onTriggered: root.configFiles.run("load") }
                Action { text: qsTrId("main.load_config_from"); icon.source: "qrc:/chimera/menu/LoadConfigFrom.png"; onTriggered: loadConfigDialog.open() }
            }
            Menu {
                delegate: keyedMenuItem
                title: qsTrId("main.tools")
                Action { text: qsTrId("main.core_manager"); icon.source: "qrc:/chimera/menu/CoreManager.png"; onTriggered: { root.bring(coresWindow) } }
                Action { text: qsTrId("main.cache_manager"); icon.source: "qrc:/chimera/menu/CacheManager.png"; onTriggered: { root.bring(cacheManagerWindow) } }
                Action { text: qsTrId("main.reproducible_media_maker"); icon.source: "qrc:/chimera/menu/MediaMaker.png"; onTriggered: { root.bring(mediaMakerWindow) } }
                Action { text: qsTrId("main.pre_compiled_modules"); icon.source: "qrc:/chimera/menu/PrecompiledModules.png"; onTriggered: { root.bring(precompiledWindow) } }
                // the server's sparkles, with a green dot while it is started: the item is not ticked, as Chimera's
                Action {
                    text: qsTrId("main.mcp_server")
                    icon.source: root.mcp.started ? "qrc:/chimera/menu/McpServerStarted.png" : "qrc:/chimera/menu/McpServer.png"
                    onTriggered: { root.bring(mcpServerWindow) }
                }
                MenuDivider {}
                Action { id: ramWatchAction; property string hotkey: "RAM Watch"; text: qsTrId("main.ram_watch"); icon.source: "qrc:/chimera/menu/RamWatch.png"; onTriggered: { root.bring(ramWatchWindow) } }
                Action { id: ramSearchAction; property string hotkey: "RAM Search"; text: qsTrId("main.ram_search"); icon.source: "qrc:/chimera/menu/RamSearch.png"; onTriggered: { root.bring(ramSearchWindow) } }
                Action { id: luaConsoleAction; property string hotkey: "Lua Console"; text: qsTrId("main.lua_console"); icon.source: "qrc:/chimera/menu/LuaConsole.png"; onTriggered: { root.bring(luaConsoleWindow) } }
                Action { id: hexEditorAction; property string hotkey: "Hex Editor"; text: qsTrId("main.hex_editor"); icon.source: "qrc:/chimera/menu/HexEditor.png"; onTriggered: { root.bring(hexEditorWindow) } }
                Action { text: qsTrId("main.macro_tool"); icon.source: "qrc:/chimera/menu/MacroTool.png"; enabled: root.session.isOpen; onTriggered: { root.bring(macroToolWindow) } }
            }
            // Chimera's Emulator menu, Game for a game core: only what the core
            // open offers, and gone when it offers nothing
            Menu {
                readonly property bool shown: root.session.isOpen && (root.session.hasGameTimer || root.session.hasSaveData
                    || root.session.hasMovieImport || root.session.hasTrace || root.session.hasRegisters || root.session.surfaces !== "[]")
                delegate: keyedMenuItem
                title: root.session.isGameCore ? qsTrId("main.game") : qsTrId("main.emulator")
                Action {
                    property string hotkey: "Game Time"
                    text: qsTrId("main.display_game_time"); enabled: root.session.hasGameTimer
                    checkable: true; checked: root.session.showGameTime; onTriggered: root.session.showGameTime = checked
                }
                Action {
                    text: qsTrId("main.export_save_data_dialog"); enabled: root.session.hasSaveData
                    onTriggered: {
                        const names = root.session.run("saveDataFiles")
                        if (names === undefined) return
                        if (names === "") { root.note(qsTrId("main.this_machine_holds_no_save_data")); return }
                        const list = names.split("\n")
                        saveDataDialog.files = list.length
                        // one file under its own name; more in a zip named for the game
                        saveDataDialog.selectedFile = "file:///" + (list.length === 1 ? list[0].replace(/^.*[\\/]/, "")
                                                                                       : qsTrId("main.save_data_file").arg(root.session.gameName))
                        saveDataDialog.open()
                    }
                }
                Action {
                    text: qsTrId("main.import_movie"); enabled: root.session.hasMovieImport
                    // Chimera's: the core's import form first, then the wizard on its answers
                    onTriggered: { root.newProject.run("chooseCore", [root.session.coreName]); root.bring(movieImportWindow) }
                }
                MenuDivider {}
                // the tools the core offers, by Chimera's names, in its order
                Action { text: qsTrId("main.debugger"); icon.source: "qrc:/chimera/menu/Debugger.png"; enabled: root.session.hasRegisters; onTriggered: { root.bring(debuggerWindow) } }
                Action { text: qsTrId("main.surface_viewer"); enabled: root.session.surfaces !== "[]"; onTriggered: { root.bring(surfaceViewerWindow) } }
                Action { id: traceLoggerAction; property string hotkey: "Trace Logger"; text: qsTrId("main.trace_logger"); enabled: root.session.hasTrace; onTriggered: { root.bring(traceLoggerWindow) } }
            }
            Menu {
                delegate: keyedMenuItem
                title: qsTrId("main.help")
                Action { text: qsTrId("main.open_toolassisted_run_in_browser"); icon.source: "qrc:/chimera/menu/OnlineHelp.png"; onTriggered: Qt.openUrlExternally(root.help.siteUrl) }
                Action {
                    text: qsTrId("main.copy_version_info"); icon.source: "qrc:/chimera/menu/CopyVersionInfo.png"
                    onTriggered: { root.clipboard.setText(root.help.run("versionReport")); root.note(qsTrId("main.version_info_copied")) }
                }
                Action { text: qsTrId("main.report_an_issue"); icon.source: "qrc:/chimera/menu/ReportIssue.png"; onTriggered: { root.bring(reportIssueWindow) } }
                // beside the report it goes with (owner, 2026-10-27); off until asked, every run.
                // A log page with a bug, and the green dot MCP Server's icon has while the log is kept
                Action {
                    text: qsTrId("main.export_core_log_dialog")
                    icon.source: root.session.coreLogPath !== "" ? "qrc:/chimera/menu/CoreLogOn.png" : "qrc:/chimera/menu/CoreLog.png"
                    onTriggered: {
                        if (root.session.coreLogPath !== "") {
                            coreLogOffDialog.size = root.session.run("coreLogSize")
                            coreLogOffDialog.open()
                        } else {
                            coreLogFileDialog.selectedFile = "file:///" + root.session.run("coreLogName")
                            coreLogFileDialog.open()
                        }
                    }
                }
                Action { text: qsTrId("main.about"); icon.source: "qrc:/chimera/menu/About.png"; onTriggered: aboutWindow.show() }
            }
        }
        // at the right of the menus while TAStudio is hidden, as Chimera's
        ToolButton {
            objectName: "showTAStudio"
            text: qsTrId("main.show_tastudio")
            flat: true
            visible: tastudioWindow.hiddenByPerson && root.session.hasProject
            onClicked: tastudioWindow.engage()
        }
    }

    // Chimera's context menu on the picture: the project's items while one is
    // open, Config while full screen, the screenshot and closing while a game runs, and the menu
    // bar back while it is hidden. Its Restart, Stop and Save items do here what their hotkeys do.
    readonly property bool fullScreen: root.visibility === Window.FullScreen
    property bool cursorHidden: false
    property point cursorAt: Qt.point(-100, -100)
    Shortcut {
        // the keyboard's menu key opens it too
        sequence: "Menu"
        enabled: root.customize.showContextMenu
        onActivated: pictureMenu.popup(pictureArea, 0, 0)
    }
    Menu {
        delegate: MenuEntry {}
        id: pictureMenu
        objectName: "pictureMenu"
        // Config, after the project's items, only while full screen
        property bool configIn: false
        onAboutToShow: {
            if (root.fullScreen !== pictureMenu.configIn) {
                if (root.fullScreen) pictureMenu.insertMenu(10, pictureConfigMenu)
                else pictureMenu.removeMenu(pictureConfigMenu)
                pictureMenu.configIn = root.fullScreen
            }
        }
        component Shown: MenuItem { height: visible ? implicitHeight : 0 }
        component ShownSeparator: MenuDivider { height: visible ? implicitHeight : 0 }
        Shown {
            text: qsTrId("main.restart_movie"); icon.source: "qrc:/chimera/menu/RestartMovie.png"; visible: root.session.hasProject
            onTriggered: root.hotkeys.run("run", ["Play from beginning"])
        }
        Shown {
            text: qsTrId("main.stop_movie"); icon.source: "qrc:/chimera/menu/StopMovie.png"; visible: root.session.hasProject
            onTriggered: root.hotkeys.run("run", ["Stop Movie"])
        }
        Shown {
            text: qsTrId("main.backup_movie"); visible: root.session.hasProject
            onTriggered: { root.session.run("saveBackup"); root.note(qsTrId("main.backup_movie_saved")) }
        }
        Shown {
            text: qsTrId("main.stop_movie_without_saving"); icon.source: "qrc:/chimera/menu/StopMovie.png"
            visible: root.session.hasProject && root.session.unsaved
            onTriggered: root.session.run("stopMovie")
        }
        // the window has no read-only mode: the project is always edited, so Edit, as Chimera's says then
        Shown { text: qsTrId("main.edit_subtitles"); visible: root.session.hasProject; onTriggered: tastudioWindow.showDetails("subtitles") }
        Shown { text: qsTrId("main.add_subtitle"); visible: root.session.isOpen && root.session.hasProject; onTriggered: subtitleMaker.openOn(root.session.frame) }
        Shown { text: qsTrId("main.edit_comments"); visible: root.session.hasProject; onTriggered: tastudioWindow.showDetails("comments") }
        Shown {
            text: qsTrId("main.save_movie"); icon.source: "qrc:/chimera/menu/SaveMovie.png"; visible: root.session.hasProject
            onTriggered: root.hotkeys.run("run", ["Save Movie"])
        }
        Shown {
            text: qsTrId("main.save_movie_as"); icon.source: "qrc:/chimera/menu/SaveMovie.png"; visible: root.session.hasProject
            onTriggered: root.askSaveAs()
        }
        ShownSeparator { visible: root.session.isOpen }
        Shown { text: qsTrId("main.screenshot"); icon.source: "qrc:/chimera/menu/Screenshot.png"; visible: root.session.isOpen; onTriggered: root.hotkeys.run("run", ["Screenshot"]) }
        Shown { text: qsTrId("main.close_project"); icon.source: "qrc:/chimera/menu/CloseProject.png"; visible: root.session.isOpen; onTriggered: closeAction.trigger() }
        ShownSeparator { visible: root.fullScreen || !root.menuBar.visible }
        Shown {
            text: root.menuBar.visible ? qsTrId("main.hide_menu") : qsTrId("main.show_menu")
            visible: root.fullScreen || !root.menuBar.visible
            onTriggered: root.menuBar.visible = !root.menuBar.visible
        }
    }
    Menu {
        delegate: MenuEntry {}
        id: pictureConfigMenu
        title: qsTrId("main.config")
        // Chimera shows it only full screen, where the menu bar is gone
        Action { text: qsTrId("main.controllers"); icon.source: "qrc:/chimera/menu/Controllers.png"; onTriggered: { root.bring(controllerWindow) } }
        Action { text: qsTrId("main.hotkeys"); icon.source: "qrc:/chimera/menu/Hotkeys.png"; onTriggered: { root.bring(hotkeyWindow) } }
        Action { text: qsTrId("main.display"); icon.source: "qrc:/chimera/menu/Display.png"; onTriggered: { root.bring(displayWindow) } }
        Action { text: qsTrId("main.sound"); icon.source: "qrc:/chimera/menu/Sound.png"; onTriggered: { root.bring(soundWindow) } }
        Action { text: qsTrId("main.paths"); icon.source: "qrc:/chimera/menu/Paths.png"; onTriggered: { root.bring(pathsWindow) } }
        Action { text: qsTrId("main.messages"); icon.source: "qrc:/chimera/menu/Messages.png"; onTriggered: { root.bring(messagesWindow) } }
        Action { text: qsTrId("main.autofire"); icon.source: "qrc:/chimera/menu/Autofire.png"; onTriggered: autofireDialog.open() }
        Action { text: qsTrId("main.customize"); icon.source: "qrc:/chimera/menu/Customize.png"; onTriggered: { root.bring(customizeWindow) } }
        MenuDivider {}
        Action { text: qsTrId("main.save_config"); icon.source: "qrc:/chimera/menu/SaveConfig.png"; onTriggered: root.configFiles.run("save") }
        Action { text: qsTrId("main.load_config"); icon.source: "qrc:/chimera/menu/LoadConfig.png"; onTriggered: root.configFiles.run("load") }
    }

    // Chimera's Subtitle Maker, for Add Subtitle: the frame the machine is on, or the subtitle already on it
    Dialog {
        id: subtitleMaker
        title: qsTrId("main.subtitle_maker")
        anchors.centerIn: parent
        modal: true
        width: 435
        property int frame: 0
        property int index: -1
        property string color: "#FFFFFFFF"
        function openOn(f) {
            subtitleMaker.frame = f
            const found = root.projectDetails.run("subtitleOn", [f])
            const sub = found ? JSON.parse(found) : { index: -1, x: 0, y: 0, duration: 120, color: "#FFFFFFFF", message: "" }
            subtitleMaker.index = sub.index
            subX.value = sub.x
            subY.value = sub.y
            subDuration.value = sub.duration
            subtitleMaker.color = sub.color
            subMessage.text = sub.message
            open()
            subMessage.forceActiveFocus()
        }
        onAccepted: {
            // as Chimera's: the one on this frame is replaced
            if (subtitleMaker.index >= 0) root.projectDetails.run("removeSubtitle", [subtitleMaker.index])
            root.projectDetails.run("addSubtitle", [subtitleMaker.frame, subX.value, subY.value, subDuration.value, subtitleMaker.color, subMessage.text])
        }
        ColorDialog {
            id: subColorDialog
            selectedColor: "#" + subtitleMaker.color.substring(3)
            onAccepted: subtitleMaker.color = "#FF" + selectedColor.toString().substring(1).toUpperCase()
        }
        GridLayout {
            anchors.fill: parent
            columns: 4
            Label { text: qsTrId("common.frame") }
            SpinBox { from: 0; to: 2147483647; value: subtitleMaker.frame; enabled: false; Layout.columnSpan: 3; Layout.preferredWidth: 120 }
            Label { text: qsTrId("main.message"); Layout.columnSpan: 4 }
            TextField { id: subMessage; Layout.columnSpan: 4; Layout.fillWidth: true }
            SpinBox { id: subX; from: 0; to: 320; editable: true; Layout.preferredWidth: 90 }
            Label { text: qsTrId("main.x_position") }
            SpinBox { id: subDuration; from: 0; to: 9999; editable: true; Layout.preferredWidth: 90 }
            Label { text: qsTrId("main.duration") }
            SpinBox { id: subY; from: 0; to: 240; editable: true; Layout.preferredWidth: 90 }
            Label { text: qsTrId("main.y_position") }
            Rectangle {
                Layout.preferredWidth: 56
                Layout.preferredHeight: 19
                color: "#" + subtitleMaker.color.substring(3)
                border.color: palette.mid
                TapHandler { onDoubleTapped: subColorDialog.open() }
            }
            Label { text: qsTrId("main.color") }
        }
        footer: DialogButtonBox {
            Button { text: qsTrId("common.save"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: qsTrId("common.cancel_mnemonic"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
    }

    Dialog {
        id: autofireDialog
        title: qsTrId("main.autofire_configuration")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: { fireOn.value = root.session.autofireOn; fireOff.value = root.session.autofireOff; fireLag.checked = root.session.autofireSkipsLag }
        // as Chimera's: "Pattern: N on, N off", and whether lag frames are taken into account (they do not move the pattern)
        ColumnLayout {
            RowLayout {
                Label { text: qsTrId("main.pattern") }
                SpinBox { id: fireOn; from: 1; to: 512; editable: true; Layout.preferredWidth: 90 }
                Label { text: qsTrId("main.on_2") }
                SpinBox { id: fireOff; from: 1; to: 512; editable: true; Layout.preferredWidth: 90 }
                Label { text: qsTrId("main.off") }
            }
            CheckBox { id: fireLag; text: qsTrId("main.take_lag_frames_into_account") }
        }
        onAccepted: { root.session.autofireOn = fireOn.value; root.session.autofireOff = fireOff.value; root.session.autofireSkipsLag = fireLag.checked }
    }
    // Chimera's Go to Frame: the machine paused, a frame index typed
    Dialog {
        id: seekDialog
        title: qsTrId("main.go_to_frame")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: { root.session.run("pause"); seekTo.text = ""; seekTo.forceActiveFocus() }
        ColumnLayout {
            Label { text: qsTrId("main.jump_seek_to_frame_index") }
            TextField {
                id: seekTo
                validator: IntValidator { bottom: 0 }
                Layout.preferredWidth: 200
                onAccepted: seekDialog.accept()
            }
        }
        onAccepted: if (seekTo.text !== "") root.session.run("seek", [parseInt(seekTo.text)])
    }
    // RAM Search's and RAM Watch's View in Hex Editor: a hidden domain (a
    // game's own variables, say) is not one the Hex Editor shows
    function viewInHexEditor(domain, address) {
        if (("," + root.hexEditor.domains + ",").indexOf("," + domain + ",") < 0) {
            root.note(qsTrId("main.the_hex_editor_does_not_show").arg(domain))
            return
        }
        root.hexEditor.domain = domain
        root.hexEditor.run("goTo", [address])
        hexEditorWindow.show()
        hexEditorWindow.raise()
    }
    RamSearch {
        id: ramSearchWindow; vm: root.ramSearch; folderOf: root.folderOf; visible: false
        clipboard: root.clipboard; customize: root.customize; themeColor: root.osdColor
        toolWindows: root.toolWindows; mainWindow: root
        showRamWatch: () => { root.bring(ramWatchWindow) }
        viewInHexEditor: root.viewInHexEditor
    }
    RamWatch {
        id: ramWatchWindow; vm: root.ramWatch; folderOf: root.folderOf; visible: false
        clipboard: root.clipboard; customize: root.customize; themeColor: root.osdColor
        toolWindows: root.toolWindows; mainWindow: root; gameProperties: root.gameProperties
        viewInHexEditor: root.viewInHexEditor
    }
    NewProject {
        id: newProjectWindow; vm: root.newProject; askFirst: (then) => root.leaveProject(then, newProjectWindow); mainWindow: root; themeColor: root.osdColor; visible: false
        folderOf: root.folderOf; gameChosen: (file) => root.paths.run("gameChosen", [file])
    }
    // New Project pauses the machine while it is open, as Chimera's did, and
    // lets it run again when it closes without making a project (issue 392)
    property bool runAfterNewProject: false
    Connections {
        target: newProjectWindow
        function onVisibleChanged() {
            if (newProjectWindow.visible) {
                root.runAfterNewProject = root.session.running
                if (root.session.running) root.session.run("pause")
            } else if (root.runAfterNewProject) {
                root.runAfterNewProject = false
                if (root.session.isOpen && !newProjectWindow.madeProject) root.session.run("play")
            }
        }
    }
    MovieImport {
        id: movieImportWindow
        vm: root.newProject
        mainWindow: root
        themeColor: root.osdColor
        visible: false
        // what the core noted, as Chimera says it, before the wizard
        onTaken: {
            if (root.newProject.importSummary !== "") importNotesDialog.open()
            else { root.bring(newProjectWindow) }
        }
    }
    MessageDialog {
        id: importNotesDialog
        title: movieImportWindow.title
        text: root.newProject.importSummary
        buttons: MessageDialog.Ok
        onAccepted: { root.bring(newProjectWindow) }
        onRejected: { root.bring(newProjectWindow) }
    }
    // the project's own window, beside this one: open exactly when a project is
    TAStudio {
        id: tastudioWindow
        session: root.session; pianoRoll: root.pianoRoll; branches: root.branches; projectDetails: root.projectDetails
        clipboard: root.clipboard; customize: root.customize; keyedItem: keyedMenuItem; folderOf: root.folderOf; mainWindow: root
        keyNamer: root.keyNamer; toolWindows: root.toolWindows; hotkeyKeys: root.hotkeyKeys
        showControllers: () => { controllerWindow.over = tastudioWindow; root.bring(controllerWindow) }
        visible: false
    }
    Connections {
        target: root.session
        function onHasProjectChanged() {
            if (root.session.hasProject) tastudioWindow.engage()
            else tastudioWindow.close()
        }
    }
    // files dropped on the window, each opened by its kind (openFiles)
    DropArea {
        anchors.fill: parent
        onDropped: (drop) => {
            if (!drop.hasUrls) return
            root.openFiles(drop.urls)
            drop.accept()
        }
    }
    HexEditor {
        id: hexEditorWindow; vm: root.hexEditor; folderOf: root.folderOf; visible: false
        clipboard: root.clipboard; customize: root.customize; themeColor: root.osdColor
        toolWindows: root.toolWindows; mainWindow: root
        showRamWatch: () => { root.bring(ramWatchWindow) }
    }
    Themes { id: themesWindow; vm: root.themes; colors: root.themeColors; mainWindow: root; visible: false }
    // Chimera's Controller Config and Configure Hotkeys, each a window of its own
    ControllerConfig {
        id: controllerWindow; controls: root.controls; keyNamer: root.keyNamer; session: root.session
        mainWindow: root; themeColor: root.osdColor; visible: false
    }
    HotkeyConfig {
        id: hotkeyWindow; hotkeys: root.hotkeys; keyNamer: root.keyNamer; session: root.session
        mainWindow: root; themeColor: root.osdColor; visible: false
    }
    LuaConsole {
        id: luaConsoleWindow; vm: root.luaConsole; folderOf: root.folderOf; visible: false
        fileUrl: (path) => root.pictureFiles.folderUrl(path); localPath: (url) => root.pictureFiles.localPath(url)
        clipboard: root.clipboard; customize: root.customize; themeColor: root.osdColor
        toolWindows: root.toolWindows; mainWindow: root
    }
    Cores { id: coresWindow; vm: root.cores; visible: false }

    FileDialog {
        id: projectDialog
        title: qsTrId("main.choose_a_project")
        currentFolder: root.folderOf("Movies")
        nameFilters: [qsTrId("main.chimera_projects_chimeraproject"), qsTrId("common.all_files")]
        // the project's core is found among the installed ones
        onAccepted: root.openProject(selectedFile)
    }
    // the windows Lua scripts make with the forms library
    LuaForms { vm: root.forms; mainWindow: root; themeColors: root.themeColors }
    // Chimera's File > Encode Video: a window of its own, modeless
    EncodeVideo {
        id: encodeWindow
        vm: root.encoder; session: root.session; pianoRoll: root.pianoRoll; paths: root.paths; files: root.pictureFiles
        picture: picture
        mainWindow: root
        themeColor: root.osdColor
        visible: false
    }
    // Chimera's: a video is an encode of a run, so there is none without a project
    function encodeVideo() {
        if (!root.session.hasProject) { encodeRefused.open(); return }
        encodeWindow.show()
        encodeWindow.raise()
    }
    MessageDialog {
        id: encodeRefused
        text: qsTrId("main.open_a_project_first_a_video")
        buttons: MessageDialog.Ok
    }
    // a project that does not open says why in a box, as Chimera's did: a
    // message over the picture went by unread, and the open seemed to do nothing (issue 422)
    MessageDialog {
        id: openFailedDialog
        title: qsTrId("main.cannot_open_the_project")
        buttons: MessageDialog.Ok
    }
    Connections {
        target: root.session
        function onOpenFailuresChanged() {
            openFailedDialog.text = root.session.status
            openFailedDialog.open()
        }
    }
    Connections {
        target: root.encoder
        function onPhaseChanged() {
            if (root.encoder.phase === "encoding") root.note(qsTrId("main.encoding_video"))
            else if (root.encoder.phase === "done") root.note(qsTrId("main.video_encoded"))
            else if (root.encoder.phase === "stopped") root.note(qsTrId("main.encoding_stopped"))
            else if (root.encoder.phase === "failed") root.note(qsTrId("main.encoding_failed"))
        }
    }
    FileDialog {
        id: saveConfigDialog
        title: qsTrId("main.save_the_settings_as")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTrId("main.settings_json")]
        onAccepted: root.configFiles.run("saveAs", [selectedFile])
    }
    FileDialog {
        id: loadConfigDialog
        title: qsTrId("main.load_the_settings_from")
        nameFilters: [qsTrId("main.settings_json"), qsTrId("common.all_files")]
        onAccepted: root.configFiles.run("loadFrom", [selectedFile])
    }
    FileDialog {
        id: screenshotAsDialog
        title: qsTrId("main.save_the_picture_as")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: [qsTrId("main.png_pictures_png"), qsTrId("main.jpeg_pictures_jpg")]
        // Capture OSD: the picture area as drawn, with what is over it
        onAccepted: {
            if (root.session.captureOsd) pictureArea.grabToImage(grab => root.pictureFiles.saveGrab(selectedFile, grab.image))
            else root.pictureFiles.save(selectedFile)
        }
    }
    FileDialog {
        id: saveAsDialog
        title: qsTrId("main.save_the_project_as")
        // its folder and file name are set as it opens (askSaveAs)
        fileMode: FileDialog.SaveFile
        defaultSuffix: "chimeraProject"
        nameFilters: [qsTrId("main.chimera_projects_chimeraproject")]
        onAccepted: root.session.run("saveProject", [selectedFile])
        // a question that asked for this save is answered Cancel
        onRejected: root.afterSave = null
        // over another window only for the one question (leaveProject)
        onVisibleChanged: if (!visible) parentWindow = root
    }
    FileDialog {
        id: coreLogFileDialog
        title: qsTrId("main.export_core_log")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "txt"
        nameFilters: [qsTrId("main.text_files_txt")]
        onAccepted: {
            if (root.session.run("startCoreLog", [selectedFile]) === undefined) return
            if (root.session.isOpen) coreLogRebootDialog.open()
            else coreLogInfoDialog.open()
        }
    }
    Dialog {
        id: coreLogRebootDialog
        title: qsTrId("main.export_core_log")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            text: qsTrId("main.the_core_log_is_on_reboot")
            wrapMode: Text.WordWrap
            width: 420
        }
        onAccepted: root.session.run("restart")
    }
    Dialog {
        id: coreLogInfoDialog
        title: qsTrId("main.export_core_log")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok
        Label {
            text: qsTrId("main.the_core_log_is_on_load")
            wrapMode: Text.WordWrap
            width: 420
        }
    }
    Dialog {
        id: coreLogOffDialog
        property var size: 0
        title: qsTrId("main.export_core_log")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            text: qsTrId("main.the_core_log_is_on_and").arg(root.session.coreLogPath).arg(Math.round(coreLogOffDialog.size / 1024))
            wrapMode: Text.WordWrap
            width: 420
        }
        onAccepted: root.session.run("stopCoreLog")
    }
    FileDialog {
        id: saveDataDialog
        property int files: 1
        title: qsTrId("main.export_save_data")
        fileMode: FileDialog.SaveFile
        // one file is saved as the core names it; several go in a zip
        defaultSuffix: files === 1 ? "" : "zip"
        nameFilters: files === 1 ? [qsTrId("common.all_files")] : [qsTrId("main.zip_archives_zip")]
        onAccepted: root.session.run("exportSaveData", [selectedFile])
    }
    Dialog {
        id: askSaveDialog
        objectName: "askSaveDialog"
        title: qsTrId("common.closing_with_unsaved_changes")
        anchors.centerIn: parent
        modal: true
        // over another window only for the one question (leaveProject)
        onClosed: parent = root.contentItem
        Label { text: qsTrId("main.save_chimera_project") }
        footer: DialogButtonBox {
            Button {
                text: qsTrId("main.yes")
                onClicked: {
                    askSaveDialog.close()
                    root.afterSave = root.afterAsk
                    if (root.session.projectPath === "") root.askSaveAs()
                    else root.session.run("saveProject")
                }
            }
            Button {
                text: qsTrId("main.no")
                onClicked: { askSaveDialog.close(); const then = root.afterAsk; root.afterAsk = null; then() }
            }
            Button {
                text: qsTrId("common.cancel")
                onClicked: { askSaveDialog.close(); root.afterAsk = null }
            }
        }
    }
    // the core stopped: emulation paused, the
    // work kept, and four ways on; closed without a choice, nothing is done
    Connections {
        target: root.session
        function onStoppedAtChanged() { if (root.session.stoppedAt >= 0) coreStoppedDialog.open(); else coreStoppedDialog.close() }
    }
    Dialog {
        id: coreStoppedDialog
        objectName: "coreStoppedDialog"
        title: qsTrId("main.the_core_stopped")
        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.CloseOnEscape
        onRejected: if (root.session.stoppedAt >= 0) root.session.run("stoppedChoice", [""])
        function choose(choice) { coreStoppedDialog.close(); root.session.run("stoppedChoice", [choice]) }
        ColumnLayout {
            spacing: 8
            Label { text: qsTrId("main.the_core_stopped_at_frame_and").arg(root.session.stoppedAt) }
            Label { text: root.session.stoppedReason; wrapMode: Text.Wrap; Layout.maximumWidth: 480; font.italic: true }
            Label {
                visible: root.session.stoppedFreezesOff > 0
                wrapMode: Text.Wrap; Layout.maximumWidth: 480
                text: (root.session.stoppedFreezesOff === 1 ? qsTrId("main.1_frozen_address_was_switched_off")
                       : qsTrId("main.frozen_addresses_were_switched_off").arg(root.session.stoppedFreezesOff))
                      + qsTrId("main.writing_to_the_wrong_address_can")
            }
            Label { text: qsTrId("main.your_inputs_branches_and_markers_are") }
            Button {
                Layout.fillWidth: true
                text: root.session.stoppedSafePoint >= 0 ? qsTrId("main.go_back_to_frame_the_latest").arg(root.session.stoppedSafePoint)
                                                         : qsTrId("main.go_back_to_the_latest_safe")
                enabled: root.session.stoppedSafePoint >= 0
                onClicked: coreStoppedDialog.choose("back")
            }
            Button {
                Layout.fillWidth: true
                text: qsTrId("main.restart_the_machine_and_run_again")
                enabled: root.session.stoppedCanRestart
                onClicked: coreStoppedDialog.choose("restart")
            }
            Button {
                Layout.fillWidth: true
                text: qsTrId("main.save_the_project_s_inputs_without")
                enabled: root.session.hasProject
                onClicked: {
                    coreStoppedDialog.choose("")
                    // the machine died: the session keeps no state history of it
                    const quit = () => { root.leaving = true; Qt.quit() }
                    if (!root.session.unsaved && root.session.projectPath !== "") { quit(); return }
                    root.afterSave = quit
                    if (root.session.projectPath === "") root.askSaveAs()
                    else root.session.run("saveProject")
                }
            }
            Button {
                Layout.fillWidth: true
                text: qsTrId("main.close_chimera_without_saving")
                onClicked: closeKeepingWorkDialog.open()
            }
        }
    }
    Dialog {
        id: closeKeepingWorkDialog
        objectName: "closeKeepingWorkDialog"
        title: qsTrId("main.close_without_saving")
        anchors.centerIn: parent
        modal: true
        Label {
            text: qsTrId("main.close_chimera_without_saving_the_project")
            wrapMode: Text.Wrap; width: 480
        }
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: {
            coreStoppedDialog.choose("keepWork")
            root.leaving = true
            Qt.quit()
        }
    }
    Dialog {
        id: cheatsDialog
        objectName: "cheatsDialog"
        title: qsTrId("main.cheats_warning")
        anchors.centerIn: parent
        modal: true
        Label { text: qsTrId("main.continue_playback_with_cheats_enabled_ch") }
        footer: DialogButtonBox {
            Button { text: qsTrId("main.yes"); onClicked: { cheatsDialog.close(); const then = root.afterCheats; root.afterCheats = null; then() } }
            Button {
                text: qsTrId("main.no")
                onClicked: { cheatsDialog.close(); root.session.run("disableAllFreezes"); const then = root.afterCheats; root.afterCheats = null; then() }
            }
            Button { text: qsTrId("common.cancel"); onClicked: { cheatsDialog.close(); root.afterCheats = null } }
        }
    }
    Rectangle {
        id: pictureArea
        objectName: "pictureArea"
        anchors.fill: parent
        color: "black"
        // the pointer over the picture, in the machine's pixels, for the machine and Lua's input.getmouse
        PicturePointer {
            anchors.fill: parent
            onNoted: (at, buttons) => pictureArea.notePointer(at, buttons)
        }
        HoverHandler {
            // full screen, the pointer hidden after two seconds still, as Chimera's (moving it more than 5 pixels shows it)
            cursorShape: root.cursorHidden || root.capturing ? Qt.BlankCursor : Qt.ArrowCursor
            onPointChanged: {
                const p = point.position
                if (Math.abs(p.x - root.cursorAt.x) > 5 || Math.abs(p.y - root.cursorAt.y) > 5) {
                    root.cursorAt = p
                    root.cursorHidden = false
                    mouseIdle.restart()
                }
            }
        }
        Timer {
            id: mouseIdle
            interval: 2000
            running: true
            repeat: true
            onTriggered: if (root.visibility === Window.FullScreen && root.display.autohideMouseFullscreen) root.cursorHidden = true
        }
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (event) => root.hostInput.mouseWheel = root.hostInput.mouseWheel + event.angleDelta.y
        }
        // a right click: the window's menu, as Chimera's, unless the right button holds one of the
        // machine's buttons (Shift held opens it anyway)
        TapHandler {
            acceptedButtons: Qt.RightButton
            grabPermissions: PointerHandler.ApprovesTakeOverByAnything
            onTapped: (eventPoint) => {
                if (!root.customize.showContextMenu) return
                if (!root.keyNamer.shiftHeld() && root.controls.run("hasBinding", ["WMouse R"]) === true) return
                pictureMenu.popup()
            }
        }
        // a double click: full screen and back, as Chimera's, when the display settings allow it and the
        // left button holds none of the machine's buttons (Shift held does it anyway)
        TapHandler {
            acceptedButtons: Qt.LeftButton
            grabPermissions: PointerHandler.ApprovesTakeOverByAnything
            onDoubleTapped: {
                const shift = root.keyNamer.shiftHeld()
                if (!root.display.allowDoubleClickFullscreen && !shift) return
                if (!shift && root.controls.run("hasBinding", ["WMouse L"]) === true) return
                root.visibility = root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
            }
        }
        // where the pointer last was, for how far it moved (an analog control bound to RMouse X or Y)
        property point lastAt: Qt.point(NaN, NaN)
        function notePointer(at, buttons) {
            if (root.session.mouseBound && !isNaN(pictureArea.lastAt.x) && (at.x !== pictureArea.lastAt.x || at.y !== pictureArea.lastAt.y))
                root.session.run("pointerMoved", [Math.round(at.x - pictureArea.lastAt.x), Math.round(at.y - pictureArea.lastAt.y)])
            pictureArea.lastAt = at
            const shown = picture.shownRect, src = picture.sourceRect
            if (shown.width > 0 && shown.height > 0) {
                root.hostInput.mouseX = Math.floor(src.x + (at.x - shown.x) * src.width / shown.width)
                root.hostInput.mouseY = Math.floor(src.y + (at.y - shown.y) * src.height / shown.height)
                // for an analog control bound to the pointer: Chimera's -10000 at the picture's left or top to 10000 at the other side
                root.session.run("pointer", [Math.round((at.x - shown.x) / shown.width * 20000 - 10000),
                                             Math.round((at.y - shown.y) / shown.height * 20000 - 10000)])
            }
            if (buttons < 0) return
            const mask = (buttons & Qt.LeftButton ? 1 : 0) | (buttons & Qt.MiddleButton ? 2 : 0) | (buttons & Qt.RightButton ? 4 : 0)
                       | (buttons & Qt.BackButton ? 8 : 0) | (buttons & Qt.ForwardButton ? 16 : 0)
            // the pointer's buttons over the picture hold what they are bound to, as keys do (WMouse L, M, R, 1, 2)
            const names = ["WMouse L", "WMouse M", "WMouse R", "WMouse 1", "WMouse 2"]
            const was = root.hostInput.mouseButtons
            for (let b = 0; b < 5; b++)
                if (((mask ^ was) >> b) & 1) root.buttonRouter.press(names[b], ((mask >> b) & 1) === 1)
            root.hostInput.mouseButtons = mask
        }
        // nothing open: Chimera's mark, faint, blinking now and then, its eye
        // following the pointer across the window
        PictureItem {
            id: idleEyePicture
            objectName: "idleEye"
            anchors.fill: parent
            z: 1
            visible: !root.session.isOpen
            source: root.idleEye.picture("eye")
            smooth: true
            HoverHandler { id: idlePointer }
            Timer {
                interval: 16
                repeat: true
                running: idleEyePicture.visible
                property real gaze: 0
                onTriggered: {
                    // where the pointer is across the picture; gone off it, the last place holds
                    if (idlePointer.hovered && idleEyePicture.width > 0) gaze = idlePointer.point.position.x / idleEyePicture.width * 2 - 1
                    root.idleEye.run("tick", [gaze])
                }
            }
        }
        PictureItem {
            id: picture
            objectName: "picture"
            anchors.fill: parent
            source: root.session.picture("screen")
            // Config > Display: its shape, bars or stretched, whole steps, crop and filter
            smooth: root.display.finalFilter === 1
            fit: ({ "aspectMode": root.display.aspectMode, "customWidth": root.display.customWidth,
                    "customHeight": root.display.customHeight, "ratioX": root.display.ratioX, "ratioY": root.display.ratioY,
                    "letterbox": root.display.letterbox ? 1 : 0, "integerScale": root.display.integerScale ? 1 : 0,
                    "cropLeft": root.display.cropLeft, "cropTop": root.display.cropTop,
                    "cropRight": root.display.cropRight, "cropBottom": root.display.cropBottom,
                    "prescale": root.display.prescale, "autoPrescale": root.display.autoPrescale ? 1 : 0,
                    // a script's extra paddings (client.SetGameExtraPadding, SetClientExtraPadding)
                    "padLeft": root.display.padLeft, "padTop": root.display.padTop, "padRight": root.display.padRight,
                    "padBottom": root.display.padBottom, "clientLeft": root.display.clientLeft, "clientTop": root.display.clientTop,
                    "clientRight": root.display.clientRight, "clientBottom": root.display.clientBottom })
            // told to the display for scripts: the area, the picture in it, the part of the machine's shown
            onPlacementChanged: root.display.placement = [Math.round(width), Math.round(height), Math.round(shownRect.x),
                Math.round(shownRect.y), Math.round(shownRect.width), Math.round(shownRect.height), Math.round(sourceRect.x),
                Math.round(sourceRect.y), Math.round(sourceRect.width), Math.round(sourceRect.height)].join(" ")
            onWidthChanged: placementChanged()
            onHeightChanged: placementChanged()
        }
        // what scripts draw with Chimera's gui library: in the machine's
        // pixels, over the picture where it is drawn; gui.text in the window's
        Canvas {
            id: luaCanvas
            objectName: "luaCanvas"
            anchors.fill: parent
            readonly property var shapes: JSON.parse(root.guiOverlay.shapes)
            onShapesChanged: requestPaint()
            // a script's gui.text follows the counters' size
            readonly property real textScale: root.osdScale
            onTextScaleChanged: requestPaint()
            onImageLoaded: requestPaint()
            Connections { target: picture; function onPlacementChanged() { luaCanvas.requestPaint() } }
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.clearRect(0, 0, width, height)
                const shown = picture.shownRect, src = picture.sourceRect
                if (luaCanvas.shapes.length === 0) return
                const sx = src.width > 0 ? shown.width / src.width : 1, sy = src.height > 0 ? shown.height / src.height : 1
                for (const s of luaCanvas.shapes) {
                    if (s.k === "screentext") {
                        // a script's gui.text, sized and placed as the counters are, a line a row
                        const k = root.osdScale
                        ctx.save()
                        ctx.font = "%1px monospace".arg(Math.round(12 * k))
                        ctx.textBaseline = "top"
                        const lines = String(s.text).replace(/\r/g, "").split("\n")
                        const w = Math.max(...lines.map(t => ctx.measureText(t).width)), lh = 14 * k
                        const right = s.anchor === "topright" || s.anchor === "bottomright" || s.anchor === "1" || s.anchor === "3"
                        const bottom = s.anchor === "bottomleft" || s.anchor === "bottomright" || s.anchor === "2" || s.anchor === "3"
                        const x = right ? width - s.x * k - w : s.x * k
                        const y = bottom ? height - (s.y + 12) * k - (lines.length - 1) * lh : s.y * k
                        ctx.fillStyle = s.color
                        lines.forEach((t, i) => ctx.fillText(t, x, y + i * lh))
                        ctx.restore()
                        continue
                    }
                    // the client surface: the picture area's own pixels
                    if (s.surface === "client") {
                        ctx.save()
                        Shapes.draw(ctx, luaCanvas, s, 1, 1, Shapes.urlOf)
                        ctx.restore()
                        continue
                    }
                    // the surface's pixels, from its top left: the picture's, cropped and padded, as Chimera's
                    const surface = picture.surfaceRect
                    ctx.save()
                    ctx.translate(surface.x, surface.y)
                    ctx.scale(sx, sy)
                    Shapes.draw(ctx, luaCanvas, s, sx, sy, Shapes.urlOf)
                    ctx.restore()
                }
            }
        }
        Connections { target: root.guiOverlay; function onMessageChanged() { root.note(root.guiOverlay.message) } }
        // Display Nothing: nothing is drawn here, the counters neither, as Chimera's
        Rectangle { anchors.fill: parent; z: 100; color: "black"; visible: root.session.displayNothing }
        // Chimera's counters over the picture, each where Config > Messages
        // puts it (at first Chimera's places: the speed at the top, the
        // frame counter 14 down, the input 28, the lag counter 42)
        component OsdText: Text {
            property string place: ""
            style: Text.Outline
            styleColor: "black"
            font.family: "monospace"
            font.pixelSize: 12 * root.osdScale
            x: place === "" ? 0 : root.osdX(place, width)
            y: place === "" ? 0 : root.osdY(place, height)
        }
        // the speed and the average speed share the speed's line, as Chimera's (issue 312)
        OsdText {
            objectName: "osdFps"
            place: "Fps"
            visible: root.session.isOpen && text !== ""
            text: [root.session.showFps ? root.session.fpsText : "",
                   root.session.showAverageFps ? root.session.averageFpsText : ""].filter(t => t !== "").join("   ")
            color: root.osdMessageColor
        }
        // a game's own timer, at the top right 14 down, as Chimera places it
        OsdText {
            objectName: "osdGameTime"
            place: "Game Time"
            visible: root.session.isOpen && root.session.showGameTime && text !== ""
            text: root.session.gameTimeText
            color: root.osdMessageColor
        }
        OsdText {
            objectName: "osdRerecords"
            place: "Rerecords"
            visible: root.session.isOpen && root.session.showRerecords
            text: root.session.rerecords
            color: root.osdMessageColor
        }
        // messages a moment each, the newest at their corner and the older
        // ones away from it, as Chimera stacks them (or only the newest)
        Column {
            objectName: "osdMessages"
            x: root.osdX("Messages", width)
            y: root.osdY("Messages", height)
            visible: root.session.showMessages
            Repeater {
                model: {
                    const n = onScreenMessages.count
                    const all = []
                    for (let i = 0; i < n; i++) all.push(onScreenMessages.get(i).message)
                    const shown = root.messages.stackMessages ? all : all.slice(-1)
                    const p = root.osdPositions["Messages"]
                    return p && p.anchor < 2 ? shown.reverse() : shown
                }
                delegate: OsdText {
                    required property string modelData
                    x: 0
                    y: 0
                    // a long message wraps at the picture's width (issue 378)
                    width: Math.min(implicitWidth, root.osdRoom("Messages", pictureArea.width))
                    wrapMode: Text.Wrap
                    text: modelData
                    color: root.osdMessageColor
                }
            }
        }
        OsdText {
            objectName: "osdFrameCounter"
            place: "Frame Counter"
            visible: root.session.isOpen && root.session.showFrameCounter
            text: root.session.frameCounterText
            // a lag frame's number is the alert's colour
            color: root.session.lagFrame ? root.osdAlertColor : root.osdMessageColor
        }
        OsdText {
            objectName: "osdInput"
            place: "Input Display"
            visible: root.session.isOpen && root.session.showInput
            text: root.session.inputText
            color: root.osdMovieInputColor
        }
        OsdText {
            objectName: "osdLagCounter"
            place: "Lag Counter"
            visible: root.session.isOpen && root.session.showLagCounter
            text: root.session.lagCount
            color: root.osdAlertColor
        }
        // while autohold or autofire is armed, what they hold, at the top right as Chimera has it
        OsdText {
            objectName: "osdSticky"
            place: "Autohold"
            visible: root.session.isOpen && root.session.stickyArmed
            text: root.session.stickyText
            color: root.osdColor("OsdAutoHold", "#FFFFFF")
        }
        // the project's subtitles on this frame, where and in the colour each says
        Repeater {
            model: root.session.isOpen ? JSON.parse(root.projectDetails.onScreen) : []
            delegate: OsdText {
                required property var modelData
                x: modelData.x
                y: modelData.y
                text: modelData.text
                color: modelData.color
            }
        }
        // RAM Watch's on-screen watches, a line every 14, where Config >
        // Messages puts them (Chimera's place: the top left, 70 down)
        Column {
            objectName: "onScreenWatches"
            x: root.osdX("Watches", width)
            y: root.osdY("Watches", height)
            visible: root.session.isOpen
            Repeater {
                model: root.ramWatch.onScreenText === "" ? [] : root.ramWatch.onScreenText.split("\n")
                delegate: Text {
                    required property string modelData
                    text: modelData
                    height: 14 * root.osdScale
                    color: "white"
                    style: Text.Outline
                    styleColor: "black"
                    font.family: "monospace"
                    font.pixelSize: 12 * root.osdScale
                }
            }
        }
    }

    // every message the window shows, for the log window; and the ones over the picture now
    ListModel { id: messageLog }
    ListModel { id: onScreenMessages }
    // View > Window Size, as Chimera does: the picture times its system's scale (2 at first,
    // remembered), a smaller one while the window would not fit the screen; the window follows the picture's
    // size unless Static Size is ticked, and a size asked for is taken whatever Static Size says
    property int windowScale: 2
    property bool resizePending: false
    // what the window has besides the picture: its menu bar and status bar (the picture takes the rest),
    // measured from them, as the picture's own size may not have caught up with a size just given
    function chromeHeight() {
        return (root.menuBar && root.menuBar.visible ? root.menuBar.height : 0) + (root.header && root.header.visible ? root.header.height : 0)
            + (root.footer && root.footer.visible ? root.footer.height : 0)
    }
    // the largest scale this screen holds the window at (10 when it cannot be said)
    function largestFit() {
        const pw = picture.pictureWidth, ph = picture.pictureHeight
        if (pw <= 0 || ph <= 0 || root.visibility !== Window.Windowed) return 10
        const bw = root.display.clientLeft + root.display.clientRight, bh = root.chromeHeight() + root.display.clientTop + root.display.clientBottom
        let fits = 0
        for (let z = 1; z <= 10; z++) {
            if (pw * z + bw >= Screen.desktopAvailableWidth || ph * z + bh >= Screen.desktopAvailableHeight) break
            fits = z
        }
        return fits
    }
    function frameBufferResized(force) {
        if (root.visibility !== Window.Windowed) { root.resizePending = true; return }
        if (!root.display.resizeWithPicture && !force) return
        const pw = picture.pictureWidth, ph = picture.pictureHeight
        if (pw <= 0 || ph <= 0) return
        let zoom = root.display.run("scaleFor", [root.session.systemId]) || 2
        const bw = root.display.clientLeft + root.display.clientRight, bh = root.chromeHeight() + root.display.clientTop + root.display.clientBottom
        for (; zoom > 1; zoom--)
            if (pw * zoom + bw < Screen.desktopAvailableWidth && ph * zoom + bh < Screen.desktopAvailableHeight) break
        root.windowScale = zoom
        root.width = Math.round(pw * zoom + bw)
        root.height = Math.round(ph * zoom + bh)
        // too large for the screen even so: its top left kept on it
        if (root.x + root.width > Screen.desktopAvailableWidth) root.x = Math.max(Screen.desktopAvailableWidth - root.width, 0)
        if (root.y + root.height > Screen.desktopAvailableHeight) root.y = Math.max(Screen.desktopAvailableHeight - root.height, 0)
    }
    function sizeWindow(n) {
        root.display.run("setScaleFor", [root.session.systemId, n])
        root.frameBufferResized(true)
    }
    Connections {
        target: picture
        function onPictureWidthChanged() { root.frameBufferResized(false) }
        function onPictureHeightChanged() { root.frameBufferResized(false) }
    }
    // a script's client padding takes room of its own, as Chimera's window grows by it
    Connections {
        target: root.display
        function onClientLeftChanged() { root.frameBufferResized(false) }
        function onClientTopChanged() { root.frameBufferResized(false) }
        function onClientRightChanged() { root.frameBufferResized(false) }
        function onClientBottomChanged() { root.frameBufferResized(false) }
    }
    // a message over the picture, for the messages' time or as long as asked
    function note(text, seconds) {
        if (text === "") return
        messageLog.append({ "time": Qt.formatTime(new Date(), "hh:mm:ss"), "message": text })
        if (messageLog.count > 5000) messageLog.remove(0)
        if (!root.session.showMessages) return
        onScreenMessages.append({ "message": text, "until": Date.now() + (seconds || root.messages.messageSeconds) * 1000 })
        if (onScreenMessages.count > 5) onScreenMessages.remove(0)
    }
    Connections { target: root.session; function onStatusChanged() { root.note(root.session.status) } }
    Connections { target: root.hotkeys; function onStatusChanged() { root.note(root.hotkeys.status) } }
    Connections { target: root.branches; function onStatusChanged() { root.note(root.branches.status) } }
    Connections { target: root.projectDetails; function onStatusChanged() { root.note(root.projectDetails.status) } }
    Timer {
        interval: 250
        repeat: true
        running: onScreenMessages.count > 0
        onTriggered: { while (onScreenMessages.count > 0 && onScreenMessages.get(0).until < Date.now()) onScreenMessages.remove(0) }
    }
    LogWindow {
        id: logWindow; log: messageLog; clipboard: root.clipboard; visible: false
        customize: root.customize; toolWindows: root.toolWindows; mainWindow: root
    }
    MediaMaker { id: mediaMakerWindow; vm: root.mediaMaker; mainWindow: root; visible: false }
    CacheManager { id: cacheManagerWindow; vm: root.cacheManager; themeColor: root.osdColor; visible: false; onBudgetsAsked: (projectId, projectLabel) => {
            greenzoneBudgetsWindow.projectId = projectId
            greenzoneBudgetsWindow.projectLabelAsked = projectLabel
            greenzoneBudgetsWindow.show()
            greenzoneBudgetsWindow.raise()
        }
    }
    GreenzoneBudgets { id: greenzoneBudgetsWindow; session: root.session; caches: root.cacheManager; mainWindow: root; themeColor: root.osdColor; visible: false }
    Customize { id: customizeWindow; vm: root.customize; session: root.session; hotkeys: root.hotkeys; controls: root.controls; mainWindow: root; visible: false }
    MessagesConfig { id: messagesWindow; vm: root.messages; mainWindow: root; themeColor: root.osdColor; visible: false }
    SoundConfig { id: soundWindow; session: root.session; mainWindow: root; visible: false }
    DisplayConfig { id: displayWindow; vm: root.display; session: root.session; idleEye: root.idleEye; mainWindow: root; visible: false }
    ProjectFiles { session: root.session; mainWindow: root; themeColor: root.osdColor }
    // Work a crash left behind, offered before the project opens, as
    // Chimera's "Recover unsaved work?" asks
    Dialog {
        id: recoveryDialog
        objectName: "recoveryDialog"
        title: qsTrId("main.recover_unsaved_work")
        anchors.centerIn: parent
        // sized when it opens: a window resized while it is closed leaves it alone
        width: 560
        onAboutToShow: recoveryDialog.width = Math.min(parent.width - 32, 560)
        modal: true
        closePolicy: Popup.NoAutoClose
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            width: recoveryDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTrId("main.chimera_did_not_close_normally_the")
                + (root.session.recoveredCrash === "" ? ""
                   : qsTrId("main.it_was_ended_by_what_windows").arg(root.session.recoveredCrash).arg(root.session.recoveredCrashNote))
                + qsTrId("main.the_work_in_progress_up_to")
                .arg(root.session.recoveredUpTo).arg(root.session.recoveredCopy)
        }
        onAccepted: root.session.run("recoverWork", [true])
        onRejected: root.session.run("recoverWork", [false])
    }
    Connections {
        target: root.session
        function onRecoveredCopyChanged() { if (root.session.recoveredCopy !== "") recoveryDialog.open() }
    }
    ReportIssue { id: reportIssueWindow; vm: root.help; clipboard: root.clipboard; pictureFiles: root.pictureFiles; folderOf: root.folderOf; gameRunning: root.session.isOpen; mainWindow: root; visible: false }
    PathsConfig { id: pathsWindow; vm: root.paths; session: root.session; mainWindow: root; themeColor: root.osdColor; visible: false }
    FirmwareConfig { id: firmwareWindow; vm: root.firmware; folderOf: root.folderOf; themeColor: root.osdColor; visible: false }
    McpServer { id: mcpServerWindow; vm: root.mcp; clipboard: root.clipboard; mainWindow: root; visible: false }
    ProjectSettings { id: projectSettingsWindow; vm: root.settings; themeColor: root.osdColor; mainWindow: root; visible: false }
    DataFolderConfig { id: dataFolderWindow; vm: root.dataFolder; themeColor: root.osdColor; mainWindow: root; visible: false }
    PrecompiledModules { id: precompiledWindow; vm: root.precompiled; themeColor: root.osdColor; visible: false }
    TraceLogger {
        id: traceLoggerWindow; vm: root.traceLogger; session: root.session; visible: false
        folderOf: root.folderOf; customize: root.customize; toolWindows: root.toolWindows; mainWindow: root; themeColor: root.osdColor
    }
    Debugger {
        id: debuggerWindow; vm: root.debuggerTool; session: root.session; visible: false
        customize: root.customize; toolWindows: root.toolWindows; mainWindow: root; themeColor: root.osdColor
    }
    SurfaceViewer {
        id: surfaceViewerWindow; vm: root.surfaceViewer; session: root.session; visible: false
        clipboard: root.clipboard; localPath: (url) => root.pictureFiles.localPath(url); themeColor: root.osdColor
        customize: root.customize; toolWindows: root.toolWindows; mainWindow: root
    }
    MacroTool {
        id: macroToolWindow; vm: root.macroTool; pianoRoll: root.pianoRoll; folderOf: root.folderOf; visible: false
        customize: root.customize; toolWindows: root.toolWindows; mainWindow: root; themeColor: root.osdColor
    }
    // Help > About, as Chimera's About box (issue #193): the logo, the name,
    // the commit the build was made from (a link to that commit) with the
    // commit's date and time, where the code is, where the project is, and
    // OK; one column, every line centred, at Chimera's sizes
    ApplicationWindow {
        id: aboutWindow
        objectName: "aboutWindow"
        title: qsTrId("main.about_chimera")
        width: 320; height: 234
        minimumWidth: 320; maximumWidth: 320
        minimumHeight: 234; maximumHeight: 234
        flags: Qt.Dialog | Qt.MSWindowsFixedSizeDialogHint
        modality: Qt.ApplicationModal
        transientParent: root
        Image {
            x: (parent.width - width) / 2; y: 14
            width: 64; height: 64
            source: "qrc:/chimera/chimera.png"
            fillMode: Image.PreserveAspectFit
            smooth: true
        }
        Label {
            x: (parent.width - width) / 2; y: 88
            text: qsTrId("main.chimera")
            font.bold: true
            font.pointSize: 12
        }
        // only the commit is the link; its date reads as text beside it
        Label {
            x: (parent.width - width) / 2; y: 120
            textFormat: Text.RichText
            text: root.help.commit !== ""
                ? root.help.buildName.replace(root.help.commit, "<a href=\"" + root.help.commitUrl + "\">" + root.help.commit + "</a>")
                : "<a href=\"" + root.help.homePage + "\">" + root.help.buildName + "</a>"
            onLinkActivated: (link) => Qt.openUrlExternally(link)
        }
        Label {
            x: (parent.width - width) / 2; y: 144
            textFormat: Text.RichText
            text: "<a href=\"" + root.help.homePage + "\">" + root.help.homePage.replace("https://", "") + "</a>"
            onLinkActivated: (link) => Qt.openUrlExternally(link)
        }
        Label {
            x: (parent.width - width) / 2; y: 166
            textFormat: Text.RichText
            text: "<a href=\"" + root.help.siteUrl + "\">" + root.help.siteUrl.replace(/^https?:\/\//, "") + "</a>"
            onLinkActivated: (link) => Qt.openUrlExternally(link)
        }
        Button {
            x: (parent.width - width) / 2; y: 198
            width: 75; height: 23
            text: qsTrId("common.ok_mnemonic")
            focus: true
            onClicked: aboutWindow.close()
        }
        Shortcut { sequences: ["Escape", "Return", "Enter"]; onActivated: aboutWindow.close() }
    }

    // Chimera's status bar: icons for what the machine is doing, then the core's name; what is said
    // goes over the picture as Chimera's messages do. Shown whenever Display Status Bar says so.
    footer: Rectangle {
        id: statusBar
        objectName: "statusBar"
        visible: !root.chromeless && (root.visibility === Window.FullScreen ? root.display.statusBarFullscreen : root.session.showStatusBar)
        implicitHeight: 22
        color: palette.window
        Rectangle { width: parent.width; height: 1; color: palette.mid }
        component StatusIcon: Image {
            property string tip: ""
            signal clicked()
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            fillMode: Image.PreserveAspectFit
            HoverHandler { id: iconHover }
            TapHandler { onTapped: parent.clicked() }
            ToolTip.visible: iconHover.hovered && tip !== ""
            ToolTip.text: tip
            ToolTip.delay: 500
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6
            spacing: 4
            // the movie: played back, or recorded
            StatusIcon {
                objectName: "movieStatus"
                visible: root.session.hasProject && root.session.isOpen
                source: root.session.recordingMode ? "qrc:/chimera/status/Record.png" : "qrc:/chimera/status/Play.png"
                tip: root.session.recordingMode ? qsTrId("main.movie_is_in_record_mode") : qsTrId("main.movie_is_in_playback_mode")
            }
            // paused, or playing to a frame; a click pauses or plays
            StatusIcon {
                objectName: "pauseStatus"
                readonly property bool seeking: root.session.seekingTo >= 0
                visible: root.session.isOpen && (!root.session.running || seeking)
                source: seeking ? "qrc:/chimera/status/Seeking.png" : "qrc:/chimera/status/Pause.png"
                tip: seeking ? qsTrId("main.emulator_is_playing_to_frame_click").arg(root.session.seekingTo) : qsTrId("main.emulator_paused")
                onClicked: root.session.run(root.session.running ? "pause" : "play")
            }
            // a video being made
            StatusIcon {
                visible: root.encoder.phase === "encoding"
                source: "qrc:/chimera/status/Capture.png"
                tip: qsTrId("main.a_v_capture_in_progress")
            }
            // a light for each drive, with what is in it
            Repeater {
                model: root.session.isOpen ? JSON.parse(root.session.drives) : []
                delegate: RowLayout {
                    required property var modelData
                    spacing: 2
                    StatusIcon { source: modelData.on ? "qrc:/chimera/status/LightOn.png" : "qrc:/chimera/status/LightOff.png"; tip: modelData.long }
                    Label { visible: modelData.short !== ""; text: modelData.short }
                }
            }
            // some address is frozen
            StatusIcon {
                visible: root.session.frozenCount > 0
                source: "qrc:/chimera/status/Frozen.png"
                tip: qsTrId("main.addresses_are_currently_frozen")
            }
            // the sound: a click turns it on or off
            StatusIcon {
                objectName: "soundStatus"
                source: root.session.soundOn ? "qrc:/chimera/status/Sound.png" : "qrc:/chimera/status/Muted.png"
                tip: root.session.soundOn ? qsTrId("main.core_is_producing_audio_live_playback").arg(root.session.volume) : qsTrId("main.core_is_not_producing_audio")
                onClicked: root.session.soundOn = !root.session.soundOn
            }
            // the key priority: a click is the next one
            StatusIcon {
                objectName: "keyPriorityStatus"
                source: ["qrc:/chimera/status/KeysBoth.png", "qrc:/chimera/status/KeysButtons.png", "qrc:/chimera/status/KeysHotkeys.png"][root.hotkeys.keyPriority] || ""
                tip: [qsTrId("main.key_priority_allow_both_hotkeys_and"),
                      qsTrId("main.key_priority_controller_buttons_will_ove"),
                      qsTrId("main.key_priority_hotkeys_will_override_contr")][root.hotkeys.keyPriority] || ""
                onClicked: root.hotkeys.run("cycleKeyPriority")
            }
            // the core running
            Label {
                objectName: "coreNameStatus"
                visible: root.session.isOpen
                text: root.session.coreName
                HoverHandler { id: coreHover }
                ToolTip.visible: coreHover.hovered
                ToolTip.text: qsTrId("main.ported")
                ToolTip.delay: 500
            }
            Item { Layout.fillWidth: true }
        }
    }
    // what went wrong is said over the picture, as Chimera's messages say it
    Connections { target: root.session; function onErrorChanged() { if (root.session.error !== "") root.note(root.session.error) } }
    Connections { target: root.pianoRoll; function onErrorChanged() { if (root.pianoRoll.error !== "") root.note(root.pianoRoll.error) } }
}
