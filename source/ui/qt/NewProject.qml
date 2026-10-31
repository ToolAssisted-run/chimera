// Chimera's New Chimera Project, laid out as its wizard: a page at a time,
// each with its heading, and < Back, Next > (Create on the last page) and
// Cancel under them beside a line that says what keeps a page from going
// on. 1: the kind of core, the core, its system and its renderer (what a
// hardware renderer costs under it). 2: the game's files, a box a kind of
// file, skipped for a package that takes none. 3: the settings, with the
// machine's presets above them and what the core suggests for the game.
// 4: the firmware those decisions call for, skipped when they call for
// none. A project made from an imported movie asks where it is saved. A
// dialog of the main window, as Chimera's. Layout and bindings only, over
// the newProject view model.
import QtQuick
import QtQuick.Controls
import ChimeraDialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "newProjectWindow"
    required property QtObject vm
    // asked before the project made replaces the one open (the window's question about unsaved changes)
    property var askFirst: (then) => then()
    property Window mainWindow: null
    property var themeColor: (role, fallback) => fallback
    // a Config > Paths folder for a file dialog to open in: games in ROM's,
    // firmware in Firmware's, as Chimera's wizard (issue 402)
    property var folderOf: null
    // a game file picked: where the next game dialog opens, when Paths says so
    property var gameChosen: null
    title: qsTrId("new_project.new_chimera_project")
    width: 620
    height: 480
    minimumWidth: 520
    minimumHeight: 380
    modality: Qt.ApplicationModal
    transientParent: win.mainWindow

    // the page shown: 0 the core, 1 the files, 2 the settings, 3 the firmware
    property int page: 0
    // what the wizard last had to say, in the error colour as Chimera's
    property string said: ""
    property string slotFor: ""
    property int firmwarePicked: -1
    // a project was made the last time the window was open (the main window
    // lets a paused machine run again only when none was)
    property bool madeProject: false
    readonly property color errorColor: win.themeColor("AccentError", "#c62828")
    readonly property color goodColor: win.themeColor("AccentGood", "#2e7d32")

    onVisibleChanged: if (visible) {
        win.madeProject = false
        // the cores folder as it is now: a package may have been put in or taken out since (issue 423)
        win.vm.run("refresh")
        // the first core of the kind when none is chosen, as Chimera's opens
        if (win.vm.coreLine < 0 && win.vm.coreLines !== "") win.vm.run("chooseCoreLine", [0])
        win.showPage(0)
    }
    Connections {
        target: win.vm
        // a change made by the view model: QQmlPropertyMap sends valueChanged only for one made from QML
        function onStatusChanged() { win.said = win.vm.status }
        // made: the window's work is done, as Chimera's closed once Create had made the project
        function onMadeChanged() { win.madeProject = true; win.close() }
        // refused for its files: the files page, with why, as Chimera's went back to it
        function onFilesRefusedChanged() { if (win.vm.filesRefused) { win.showPage(1); win.said = win.vm.status } }
    }
    // a path as a file dialog takes it
    function urlOf(path) { return localFiles.folderUrl(path) }
    // a command whose refusal is said on the wizard's line
    function run(command, args) {
        win.vm.run(command, args || [])
        if (win.vm.error !== "") win.said = win.vm.error
    }
    // files into a slot, from here: each one added builds the slots again, so a
    // loop in the slot that took a drop would end with it after the first (issue 425)
    function addFiles(slotId, files) {
        for (const f of files) win.run("addFile", [slotId, f])
    }

    // the pages asked: the files when the package takes any, the firmware when the choices call for some
    function applies(p) { return p === 1 ? win.vm.filesAsked : p === 3 ? win.vm.firmwareAsked : p === 4 ? win.vm.compileAsked : true }
    function nextFrom(p) {
        for (let i = p + 1; i <= 4; i++) if (win.applies(i)) return i
        return -1
    }
    readonly property bool lastPage: win.nextFrom(win.page) < 0
    function showPage(p) {
        win.page = p
        win.said = ""
        win.firmwarePicked = -1
        // the firmware is looked for again when its page is shown
        if (p === 3) win.vm.run("firmwarePage")
    }
    function back() {
        for (let i = win.page - 1; i >= 0; i--)
            if (win.applies(i)) { win.showPage(i); return }
        win.showPage(0)
    }
    function advance() {
        if (win.page === 0) {
            if (win.vm.core === "") { win.said = qsTrId("new_project.pick_a_core_none_in_the"); return }
            if (win.applies(1)) { win.showPage(1); return }
        }
        if (win.page <= 1) {
            if (win.vm.filesComplaint !== "") { win.said = win.vm.filesComplaint; return }
            win.showPage(2)
            return
        }
        if (win.page === 3 && win.vm.firmwareMissing !== "") {
            win.said = qsTrId("new_project.is_required_and_not_provided").arg(win.vm.firmwareMissing)
            return
        }
        if (win.page === 4 && !win.vm.compileReady) {
            win.said = qsTrId("new_project.compile_this_game_s_code_first")
            return
        }
        const next = win.nextFrom(win.page)
        if (next < 0) win.create()
        else win.showPage(next)
    }
    function create() {
        // a project made from an imported movie is saved at once, where asked
        if (win.vm.imported) { importSaveDialog.open(); return }
        win.askFirst(() => {
            win.run("create")
            if (win.vm.error !== "" && win.vm.clockWrong) win.showPage(2)
        })
    }

    Shortcut { sequences: ["Return", "Enter"]; enabled: win.active && nextButton.enabled; onActivated: win.advance() }
    Shortcut { sequence: "Escape"; enabled: win.active && !win.vm.making; onActivated: win.close() }
    // open until the project is made, so that a refusal is said here
    onClosing: (close) => { if (win.vm.making) close.accepted = false }

    FileDialog {
        id: fileDialog
        title: qsTrId("new_project.choose_a_file")
        fileMode: FileDialog.OpenFiles
        currentFolder: win.folderOf ? win.folderOf("ROM") : ""
        onAccepted: {
            if (win.gameChosen && selectedFiles.length > 0) win.gameChosen(selectedFiles[0])
            win.addFiles(win.slotFor, selectedFiles)
        }
    }
    FileDialog {
        id: firmwareDialog
        property string need: ""
        currentFolder: win.folderOf ? win.folderOf("Firmware") : ""
        onAccepted: win.run("chooseFirmware", [firmwareDialog.need, selectedFile])
    }
    FolderDialog {
        id: scanDialog
        title: qsTrId("new_project.scan_a_folder_for_firmware")
        currentFolder: win.folderOf ? win.folderOf("Firmware") : ""
        // in the dialog itself, as a box of the chooser's (docs/file-picker.md)
        checkBoxText: qsTrId("new_project.include_sub_folders")
        checkBoxChecked: true
        onAccepted: win.run("scanFolder", [selectedFolder, checkBoxChecked])
    }
    FileDialog {
        id: importSaveDialog
        title: qsTrId("new_project.save_the_imported_project")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "chimeraProject"
        nameFilters: [qsTrId("new_project.tas_project_files_chimeraproject")]
        // beside the movie, named after it
        currentFolder: win.urlOf(win.vm.importMovie.replace(/[\\/][^\\/]*$/, ""))
        selectedFile: win.vm.importMovie === "" ? "" : win.urlOf(win.vm.importMovie.replace(/\.[^.\\/]*$/, "") + ".chimeraProject")
        onAccepted: win.askFirst(() => win.run("create", [selectedFile]))
    }

    component Heading: Label { Layout.fillWidth: true; Layout.bottomMargin: 10; elide: Text.ElideRight }
    component Caption: Label { Layout.preferredWidth: 96 }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        StackLayout {
            id: pages
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: win.page

            // ---- page 1: the machine ----
            ColumnLayout {
                spacing: 8
                Heading { text: qsTrId("new_project.please_select_the_core_an_emulator") }
                GridLayout {
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 8
                    Layout.fillWidth: true
                    Caption { text: qsTrId("new_project.kind"); Layout.row: 0; Layout.column: 0 }
                    ComboBox {
                        Layout.row: 0
                        Layout.column: 1
                        Layout.preferredWidth: 160
                        model: [qsTrId("common.emulators"), qsTrId("common.games")]
                        currentIndex: win.vm.kind === "games" ? 1 : 0
                        onActivated: (i) => {
                            win.run("chooseKind", [i === 1 ? "games" : "emulators"])
                            // another kind: its first core chosen
                            if (win.vm.coreLines !== "") win.run("chooseCoreLine", [0])
                        }
                    }
                    Caption { text: qsTrId("new_project.core"); Layout.row: 1; Layout.column: 0 }
                    ComboBox {
                        objectName: "coreBox"
                        Layout.row: 1
                        Layout.column: 1
                        Layout.fillWidth: true
                        model: win.vm.coreLines === "" ? [] : win.vm.coreLines.split("\n")
                        currentIndex: win.vm.coreLine
                        onActivated: (i) => win.run("chooseCoreLine", [i])
                    }
                    Caption { text: qsTrId("new_project.system"); Layout.row: 2; Layout.column: 0 }
                    // a package that is one machine still says which, in a box that is not a choice
                    ComboBox {
                        Layout.row: 2
                        Layout.column: 1
                        Layout.fillWidth: true
                        model: win.vm.machines !== "" ? win.vm.machines.split("\n") : win.vm.systems !== "" ? [win.vm.systems] : []
                        currentIndex: win.vm.machines !== "" ? win.vm.machine : 0
                        enabled: count > 1
                        onActivated: (i) => win.run("chooseMachine", [i])
                    }
                    Caption { text: qsTrId("new_project.renderer"); Layout.row: 3; Layout.column: 0 }
                    // one renderer is not a choice
                    ComboBox {
                        Layout.row: 3
                        Layout.column: 1
                        Layout.fillWidth: true
                        model: win.vm.renderers === "" ? [] : win.vm.renderers.split("\n")
                        currentIndex: win.vm.renderer
                        enabled: count > 1
                        onActivated: (i) => win.run("chooseRenderer", [i])
                    }
                    // what a hardware renderer costs; the room kept whether or not it is said
                    Label {
                        Layout.row: 4
                        Layout.column: 1
                        Layout.fillWidth: true
                        Layout.preferredHeight: 50
                        verticalAlignment: Text.AlignTop
                        text: win.vm.rendererCaveat
                        wrapMode: Text.WordWrap
                        color: palette.placeholderText
                    }
                }
                Item { Layout.fillHeight: true }
                Label {
                    readonly property string url: "https://github.com/ToolAssisted-run/chimera/issues"
                    Layout.fillWidth: true
                    textFormat: Text.StyledText
                    text: qsTrId("new_project.please_submit_any_issues_and_feature").arg("<a href=\"" + url + "\">" + url + "</a>")
                    linkColor: palette.link
                    color: palette.placeholderText
                    elide: Text.ElideRight
                    onLinkActivated: (link) => Qt.openUrlExternally(link)
                    HoverHandler { cursorShape: parent.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor }
                }
            }

            // ---- page 2: the files ----
            ColumnLayout {
                Heading { text: qsTrId("new_project.please_provide_the_game_roms_and") }
                ScrollView {
                    id: slotsView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: slotsView.availableWidth
                        spacing: 6
                        Repeater {
                            model: win.vm.rows("slots")
                            delegate: GroupBox {
                                id: slot
                                required property var model
                                required property string id
                                // the box's own title is the box's: the slot's is read from the row
                                readonly property string slotTitle: slot.model.title
                                required property string formats
                                required property string help
                                required property bool exposed
                                required property string files
                                required property string filter
                                required property int max
                                required property string cardinality
                                required property bool shown
                                property int picked: -1
                                // a slot only settings open, which this machine closes, is not on the form
                                visible: slot.shown
                                enabled: slot.exposed
                                Layout.fillWidth: true
                                Layout.rightMargin: 12
                                title: slot.slotTitle + "  (" + slot.cardinality + ")" + (slot.exposed ? "" : qsTrId("new_project.unavailable_with_the_current_files"))
                                ToolTip.visible: slotHover.hovered && ToolTip.text !== ""
                                ToolTip.delay: 600
                                ToolTip.text: slot.help !== "" ? slot.help : qsTrId("new_project.the_core_did_not_explain_this")
                                HoverHandler { id: slotHover }
                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 4
                                    Label {
                                        visible: slot.formats !== ""
                                        text: qsTrId("new_project.formats").arg(slot.formats)
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        // the list is where a file dragged onto this slot lands
                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 70
                                            color: palette.base
                                            border.color: slotDrop.containsDrag ? palette.highlight : palette.mid
                                            ListView {
                                                id: fileList
                                                anchors.fill: parent
                                                anchors.margins: 1
                                                clip: true
                                                model: slot.files === "" ? [] : slot.files.split("\n")
                                                ScrollBar.vertical: ScrollBar {}
                                                delegate: Rectangle {
                                                    required property string modelData
                                                    required property int index
                                                    width: fileList.width
                                                    height: fileName.implicitHeight + 4
                                                    color: index === slot.picked ? palette.highlight : "transparent"
                                                    Label {
                                                        id: fileName
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        leftPadding: 3
                                                        text: parent.modelData
                                                        color: parent.index === slot.picked ? palette.highlightedText : palette.text
                                                    }
                                                    TapHandler { onTapped: slot.picked = parent.index }
                                                }
                                            }
                                            DropArea {
                                                id: slotDrop
                                                anchors.fill: parent
                                                onDropped: (drop) => {
                                                    drop.accept()
                                                    win.addFiles(slot.id, drop.urls)
                                                }
                                            }
                                        }
                                        // the order is the machine's: the first file is in the drive at boot
                                        ColumnLayout {
                                            visible: slot.max !== 1
                                            Layout.alignment: Qt.AlignTop
                                            Button {
                                                text: "▲"
                                                Layout.preferredWidth: 32
                                                enabled: slot.picked > 0
                                                ToolTip.visible: hovered
                                                ToolTip.text: qsTrId("new_project.move_up_earlier_in_the_swap")
                                                onClicked: { win.run("moveFile", [slot.id, slot.picked, slot.picked - 1]); slot.picked-- }
                                            }
                                            Button {
                                                text: "▼"
                                                Layout.preferredWidth: 32
                                                enabled: slot.picked >= 0 && slot.picked < fileList.count - 1
                                                ToolTip.visible: hovered
                                                ToolTip.text: qsTrId("new_project.move_down_later_in_the_swap")
                                                onClicked: { win.run("moveFile", [slot.id, slot.picked, slot.picked + 1]); slot.picked++ }
                                            }
                                        }
                                        ColumnLayout {
                                            Layout.alignment: Qt.AlignTop
                                            Button {
                                                text: qsTrId("new_project.add")
                                                Layout.preferredWidth: 84
                                                onClicked: {
                                                    win.slotFor = slot.id
                                                    fileDialog.nameFilters = slot.filter === "*" ? [qsTrId("common.all_files")]
                                                        : [slot.slotTitle + " (" + slot.filter + ")", qsTrId("common.all_files")]
                                                    fileDialog.open()
                                                }
                                            }
                                            Button {
                                                text: qsTrId("common.remove")
                                                Layout.preferredWidth: 84
                                                enabled: slot.picked >= 0
                                                onClicked: { win.run("removeFile", [slot.id, slot.picked]); slot.picked = -1 }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ---- page 3: the settings ----
            ColumnLayout {
                Heading { text: qsTrId("new_project.please_specify_the_emulation_configurati") }
                // a machine somebody has got working, then adjusted: absent when there are none
                RowLayout {
                    Layout.fillWidth: true
                    visible: presetBox.count > 0
                    Label { text: qsTrId("common.preset") }
                    ComboBox {
                        id: presetBox
                        Layout.fillWidth: true
                        model: win.vm.rows("presets")
                        textRole: "label"
                        valueRole: "id"
                        // the list is given again after every edit: the choice stays
                        property string chosen: ""
                        onActivated: chosen = currentValue
                        Connections {
                            target: presetBox.model
                            function onModelReset() { Qt.callLater(presetBox.keep) }
                        }
                        function keep() {
                            const at = chosen === "" ? -1 : indexOfValue(chosen)
                            currentIndex = at >= 0 ? at : (count > 0 ? 0 : -1)
                        }
                        Component.onCompleted: keep()
                    }
                    Button { text: qsTrId("common.apply"); Layout.preferredWidth: 72; enabled: presetBox.currentIndex >= 0; onClicked: win.run("applyPreset", [presetBox.currentValue]) }
                }
                // what the core suggests for the game, selectable so its source can be copied
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 62
                    visible: win.vm.suggestion !== ""
                    TextArea { text: win.vm.suggestion; readOnly: true; wrapMode: TextEdit.Wrap; activeFocusOnTab: false }
                }
                SettingsList {
                    vm: win.vm
                    presetsHere: false
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
            }

            // ---- page 4: the firmware ----
            ColumnLayout {
                Heading { text: qsTrId("new_project.please_indicate_where_to_find_the") }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: palette.base
                    border.color: palette.mid
                    readonly property var widths: [190, 130, 110, 180]
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 1
                        spacing: 0
                        Row {
                            Repeater {
                                model: [qsTrId("new_project.firmware"), qsTrId("new_project.expected_file"), qsTrId("new_project.sha1"), qsTrId("common.status")]
                                delegate: Label {
                                    required property int index
                                    required property string modelData
                                    text: modelData
                                    width: parent.parent.parent.widths[index]
                                    leftPadding: 4; topPadding: 2; bottomPadding: 2
                                    elide: Text.ElideRight
                                    background: Rectangle { color: palette.button; border.color: palette.mid }
                                }
                            }
                        }
                        ListView {
                            id: firmwareList
                            objectName: "firmwareList"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: win.vm.rows("firmware")
                            ScrollBar.vertical: ScrollBar {}
                            delegate: Rectangle {
                                id: need
                                required property int index
                                required property string id
                                required property string name
                                required property string title
                                required property string expected
                                required property string sha1Shown
                                required property string state
                                required property bool satisfied
                                required property string description
                                required property string file
                                readonly property bool isPicked: need.index === win.firmwarePicked
                                width: firmwareList.width
                                height: 22
                                color: isPicked ? palette.highlight : "transparent"
                                // green against red says found or not, in line
                                readonly property color ink: isPicked ? palette.highlightedText : need.satisfied ? win.goodColor : win.errorColor
                                readonly property var widths: [190, 130, 110, 180]
                                Row {
                                    anchors.verticalCenter: parent.verticalCenter
                                    Label { text: need.title; color: need.ink; width: need.widths[0]; leftPadding: 4; elide: Text.ElideRight }
                                    Label { text: need.expected; color: need.ink; width: need.widths[1]; leftPadding: 4; elide: Text.ElideRight }
                                    Label { text: need.sha1Shown; color: need.ink; width: need.widths[2]; leftPadding: 4; elide: Text.ElideRight }
                                    Label { text: need.state; color: need.ink; width: need.widths[3]; leftPadding: 4; elide: Text.ElideRight }
                                }
                                ToolTip.visible: needHover.hovered && need.description !== ""
                                ToolTip.delay: 600
                                ToolTip.text: need.description
                                HoverHandler { id: needHover }
                                TapHandler {
                                    onTapped: win.firmwarePicked = need.index
                                    onDoubleTapped: { win.firmwarePicked = need.index; selectFirmware.clicked() }
                                }
                            }
                            Label {
                                visible: firmwareList.count === 0
                                text: qsTrId("new_project.these_decisions_need_no_firmware")
                                leftPadding: 4
                            }
                        }
                    }
                }
                RowLayout {
                    Button {
                        id: selectFirmware
                        text: qsTrId("new_project.select_file")
                        enabled: win.firmwarePicked >= 0
                        onClicked: {
                            const it = firmwareList.itemAtIndex(win.firmwarePicked)
                            if (!it) return
                            firmwareDialog.need = it.id
                            firmwareDialog.title = qsTrId("new_project.locate").arg(it.name)
                            firmwareDialog.open()
                        }
                    }
                    Button {
                        text: qsTrId("common.clear")
                        enabled: win.firmwarePicked >= 0 && firmwareList.itemAtIndex(win.firmwarePicked) !== null && firmwareList.itemAtIndex(win.firmwarePicked).file !== ""
                        onClicked: win.run("clearFirmware", [firmwareList.itemAtIndex(win.firmwarePicked).id])
                    }
                    Button { text: qsTrId("common.scan_folder"); onClicked: scanDialog.open() }
                    Item { Layout.fillWidth: true }
                }
            }

            // ---- page 5: the code the core compiles for this game (Chimera's, for RPCS3) ----
            ColumnLayout {
                Heading { text: qsTrId("new_project.this_game_s_code_must_be") }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTrId("new_project.this_core_translates_the_console_s")
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: palette.base
                    border.color: palette.mid
                    ListView {
                        id: moduleList
                        objectName: "compiledModules"
                        anchors.fill: parent
                        anchors.margins: 1
                        clip: true
                        model: win.vm.rows("compiled")
                        ScrollBar.vertical: ScrollBar {}
                        header: Row {
                            Repeater {
                                model: [qsTrId("new_project.compiled_module"), qsTrId("new_project.sha1"), qsTrId("new_project.state")]
                                delegate: Label {
                                    required property int index
                                    required property string modelData
                                    text: modelData
                                    width: [300, 180, 72][index]
                                    leftPadding: 4
                                    background: Rectangle { color: palette.button; border.color: palette.mid }
                                }
                            }
                        }
                        delegate: Row {
                            required property string name
                            required property string sha1
                            required property bool present
                            Label { text: parent.name; width: 300; elide: Text.ElideMiddle; leftPadding: 4 }
                            Label { text: parent.sha1.substring(0, 16); width: 180; font.family: "monospace"; leftPadding: 4 }
                            Label { text: parent.present ? qsTrId("new_project.ready") : qsTrId("new_project.missing"); width: 72; leftPadding: 4 }
                        }
                    }
                }
                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, win.vm.compileTotal)
                    value: win.vm.compileDone
                    indeterminate: win.vm.compiling && win.vm.compileTotal === 0
                    visible: win.vm.compiling || win.vm.compileTotal > 0
                }
                RowLayout {
                    Button {
                        text: win.vm.compiling ? qsTrId("common.stop") : qsTrId("new_project.compile")
                        enabled: win.vm.compiling || !win.vm.compileReady
                        onClicked: win.run(win.vm.compiling ? "stopCompile" : "compile")
                    }
                    Label { text: win.vm.compileStatus; Layout.fillWidth: true; elide: Text.ElideRight }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                objectName: "wizardStatus"
                text: win.said
                color: win.errorColor
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Button { text: qsTrId("new_project.back"); Layout.preferredWidth: 84; enabled: win.page > 0 && !win.vm.making; onClicked: win.back() }
            Button {
                id: nextButton
                objectName: "nextButton"
                text: win.lastPage ? qsTrId("new_project.create") : qsTrId("new_project.next")
                Layout.preferredWidth: 84
                // the last page's Create waits for the firmware it requires, and for the project it is making
                enabled: (!win.lastPage || win.page !== 3 || win.vm.firmwareMissing === "") && !win.vm.making
                onClicked: win.advance()
            }
            Button { text: qsTrId("common.cancel"); Layout.preferredWidth: 84; enabled: !win.vm.making; onClicked: win.close() }
        }
    }
}
