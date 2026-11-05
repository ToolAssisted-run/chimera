// Macro Input, as a step-by-step wizard (owner, 2026-11-05, issue 511: the
// window copied from Chimera's said nothing of how to use it). A macro is a
// stretch of the movie's input - some frames, some of its controls - placed
// again elsewhere, or saved as a .chimeraMacro file to place in another
// movie. Two ways through: make one (its frames, its controls, its name;
// then save it, place it, or both), or place one (made before, or loaded
// from a file; where it goes, and how). The File menu keeps Save Selected
// As..., Load Macro... and Recent. Layout and bindings only, over the
// macroTool view model, which holds the zones.
import QtQuick
import QtQuick.Controls
import ChimeraDialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "macroToolWindow"
    required property QtObject vm
    required property QtObject pianoRoll
    property var folderOf: null
    // the theme's colour for a role (Main hands it its lookup)
    property var themeColor: (role, fallback) => fallback
    property QtObject customize: null
    property QtObject toolWindows: null
    property Window mainWindow: null
    title: qsTrId("macro_tool.macro_input")
    width: 520
    height: 480
    minimumWidth: 520
    minimumHeight: 480

    WindowOptions { id: windowOptions; window: win; tool: "macroInput"; vm: win.toolWindows; mainWindow: win.mainWindow }
    onVisibleChanged: if (visible) { windowOptions.placeBack(false); win.page = 0 }

    readonly property var zoneList: win.vm.rows("zones")
    readonly property var movieColumns: JSON.parse(win.vm.movieColumns)
    property var pickedRow: null

    // the pages: 0 what to do; making: 1 frames, 2 controls, 3 name; placing: 4 which, 5 where
    property int page: 0
    // the zone this pass made, let go again when Back goes before it
    property int made: -1
    function go(to) { win.page = to }
    function next() {
        if (win.page === 0) { win.go(makeChoice.checked ? 1 : 4); return }
        if (win.page === 1) {
            const z = win.vm.run("setZone", [firstFrame.value, lastFrame.value])
            if (z === undefined) return
            win.made = z
            win.go(2)
            return
        }
        if (win.page === 2) { win.go(3); return }
        if (win.page === 4) {
            if (win.pickedRow === null) { notice.text = qsTrId("macro_tool.please_select_a_zone_first"); notice.open(); return }
            win.go(5)
        }
    }
    function back() {
        if (win.page === 2 && win.made >= 0) {
            win.vm.run("remove", [win.made])
            win.made = -1
        }
        win.go(win.page === 4 ? 0 : win.page === 5 ? (win.made >= 0 ? 3 : 4) : win.page - 1)
    }
    function startOver() { win.made = -1; win.go(0) }

    // Chimera asks before macros not saved are let go: Yes saves each, No lets them go
    property bool leaving: false
    property var toSave: []
    onClosing: (close) => {
        if (win.vm.unsavedCount > 0 && !win.leaving) { close.accepted = false; unsavedDialog.open() }
        win.leaving = false
    }
    function saveNext() {
        if (win.toSave.length === 0) { win.leaving = true; win.close(); return }
        const z = win.toSave[0]
        win.toSave = win.toSave.slice(1)
        saveDialog.zone = z
        saveDialog.open()
    }
    MessageDialog {
        id: unsavedDialog
        title: qsTrId("macro_tool.save")
        text: qsTrId("macro_tool.you_have_unsaved_macro_s_do")
        buttons: MessageDialog.Yes | MessageDialog.No | MessageDialog.Cancel
        onButtonClicked: (button) => {
            if (button === MessageDialog.Cancel) return
            if (button === MessageDialog.No) { win.leaving = true; win.close(); return }
            const list = []
            for (let i = 0; i < zones.count; i++) if (!zones.itemAtIndex(i) || zones.itemAtIndex(i).saved === false) list.push(i)
            win.toSave = list
            win.saveNext()
        }
    }
    MessageDialog { id: notice; buttons: MessageDialog.Ok }

    FileDialog {
        id: saveDialog
        title: qsTrId("macro_tool.save_selected_as")
        property int zone: -1
        fileMode: FileDialog.SaveFile
        defaultSuffix: "chimeraMacro"
        currentFolder: win.folderOf ? win.folderOf("Macros") : ""
        nameFilters: [qsTrId("macro_tool.movie_macros_chimeramacro")]
        onAccepted: {
            win.vm.run("saveAs", [saveDialog.zone, selectedFile])
            if (win.toSave.length > 0 || win.vm.unsavedCount === 0) win.saveNext()
        }
    }
    // a macro's name as its file's first answer, in the Macros folder
    function saveZone(zone) {
        saveDialog.zone = zone
        const name = win.pickedRow ? win.pickedRow.name.replace(/[<>:"\/\\|?*]/g, "_") : ""
        if (name !== "" && saveDialog.currentFolder.toString() !== "")
            saveDialog.selectedFile = saveDialog.currentFolder + "/" + name + ".chimeraMacro"
        saveDialog.open()
    }
    FileDialog {
        id: loadDialog
        title: qsTrId("macro_tool.load_macro")
        currentFolder: win.folderOf ? win.folderOf("Macros") : ""
        nameFilters: [qsTrId("macro_tool.movie_macros_chimeramacro"), qsTrId("common.all_files")]
        onAccepted: win.vm.run("load", [selectedFile])
    }

    menuBar: MenuBar {
        Menu {
            delegate: MenuEntry {}
            title: qsTrId("common.file")
            MenuItem {
                text: qsTrId("macro_tool.save_selected_as_dialog")
                onTriggered: {
                    if (win.pickedRow === null) { notice.text = qsTrId("macro_tool.please_select_a_zone_first"); notice.open(); return }
                    win.saveZone(win.vm.picked)
                }
            }
            MenuItem { text: qsTrId("macro_tool.load_macro_dialog"); onTriggered: loadDialog.open() }
            Menu {
                delegate: MenuEntry {}
                id: recentMenu
                title: qsTrId("common.recent")
                MenuItem { text: qsTrId("common.none"); enabled: false; visible: win.pianoRoll.recentMacros === ""; height: visible ? implicitHeight : 0 }
                Instantiator {
                    model: win.pianoRoll.recentMacros === "" ? [] : win.pianoRoll.recentMacros.split("\n")
                    delegate: MenuItem {
                        required property string modelData
                        text: modelData
                        onTriggered: win.vm.run("load", [modelData])
                    }
                    onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => recentMenu.removeItem(object)
                }
            }
            MenuDivider {}
            MenuItem { text: qsTrId("common.close_mnemonic"); onTriggered: win.close() }
        }
        // Chimera adds a Settings menu for the window's items to a tool that has none
        Menu {
            delegate: MenuEntry {}
            title: qsTrId("common.settings")
            MenuItem { text: qsTrId("common.save_window_position"); checkable: true; checked: windowOptions.savePosition; onTriggered: windowOptions.set("save", checked) }
            MenuItem {
                text: qsTrId("common.stay_on_top"); checkable: true; checked: windowOptions.onTop
                visible: windowOptions.canStayOnTop; height: visible ? implicitHeight : 0
                onTriggered: windowOptions.set("top", checked)
            }
            MenuItem { text: qsTrId("common.float_from_main_window"); checkable: true; checked: windowOptions.floating; onTriggered: windowOptions.set("float", checked) }
            MenuItem { text: qsTrId("common.restore_defaults_mnemonic"); onTriggered: windowOptions.restore() }
        }
    }

    footer: Label {
        objectName: "macroStatus"
        text: win.vm.error !== "" ? win.vm.error : win.vm.status
        color: win.vm.error !== "" ? win.themeColor("AccentError", "#b00020") : palette.text
        padding: 4
        elide: Text.ElideRight
    }

    // a step's heading: what it asks, wrapped
    component Step: Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; Layout.bottomMargin: 6 }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        StackLayout {
            id: pages
            objectName: "macroPages"
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: win.page

            // ---- 0: what a macro is, and what to do ----
            ColumnLayout {
                spacing: 8
                Step { text: qsTrId("macro_tool.wizard_about") }
                RadioButton { id: makeChoice; objectName: "macroMake"; text: qsTrId("macro_tool.wizard_make"); checked: true }
                RadioButton { objectName: "macroPlace"; text: qsTrId("macro_tool.wizard_place") }
                Item { Layout.fillHeight: true }
            }

            // ---- 1: its frames ----
            ColumnLayout {
                spacing: 8
                Step { text: qsTrId("macro_tool.wizard_frames") }
                GridLayout {
                    columns: 2
                    columnSpacing: 8
                    Label { text: qsTrId("macro_tool.first_frame") }
                    SpinBox {
                        id: firstFrame; objectName: "macroFirst"; from: 0; to: Math.max(0, win.pianoRoll.frameCount - 1); editable: true
                        value: win.pianoRoll.selectFirst >= 0 ? win.pianoRoll.selectFirst : 0
                        Layout.preferredWidth: 120
                    }
                    Label { text: qsTrId("macro_tool.last_frame") }
                    SpinBox {
                        id: lastFrame; objectName: "macroLast"; from: 0; to: Math.max(0, win.pianoRoll.frameCount - 1); editable: true
                        value: win.pianoRoll.selectLast >= 0 ? win.pianoRoll.selectLast : 0
                        Layout.preferredWidth: 120
                    }
                }
                Button {
                    text: qsTrId("macro_tool.use_selection")
                    enabled: win.pianoRoll.selectFirst >= 0
                    onClicked: { firstFrame.value = win.pianoRoll.selectFirst; lastFrame.value = win.pianoRoll.selectLast }
                }
                Item { Layout.fillHeight: true }
            }

            // ---- 2: its controls ----
            ColumnLayout {
                spacing: 6
                Step { text: qsTrId("macro_tool.wizard_controls") }
                Button { text: qsTrId("macro_tool.all_controls"); onClicked: win.vm.run("setColumns", [win.vm.picked, ""]) }
                // the movie's controls, in its order, a grid that scrolls when they are many (a keyboard's)
                ScrollView {
                    id: controlsScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true
                    GridLayout {
                        width: controlsScroll.availableWidth
                        columns: Math.max(1, Math.floor(controlsScroll.availableWidth / 160))
                        columnSpacing: 4
                        rowSpacing: 0
                        Repeater {
                            model: win.movieColumns
                            delegate: CheckBox {
                                required property string modelData
                                text: modelData
                                padding: 1
                                Layout.fillWidth: true
                                readonly property var carried: win.pickedRow && win.pickedRow.columns !== "" ? win.pickedRow.columns.split("|") : win.movieColumns
                                checked: carried.indexOf(modelData) >= 0
                                onToggled: {
                                    const now = win.movieColumns.filter(c => c === modelData ? checked : carried.indexOf(c) >= 0)
                                    win.vm.run("setColumns", [win.vm.picked, now.length === win.movieColumns.length ? "" : now.join("|")])
                                }
                            }
                        }
                    }
                }
            }

            // ---- 3: its name, and what to do with it ----
            ColumnLayout {
                spacing: 8
                Step { text: qsTrId("macro_tool.wizard_name") }
                RowLayout {
                    Label { text: qsTrId("macro_tool.macro_name") }
                    TextField {
                        objectName: "macroName"
                        Layout.fillWidth: true
                        text: win.pickedRow ? win.pickedRow.name : ""
                        onTextEdited: if (win.pickedRow) win.vm.run("rename", [win.vm.picked, text])
                    }
                }
                RowLayout {
                    Button { text: qsTrId("macro_tool.save_file"); onClicked: win.saveZone(win.vm.picked) }
                    Button { text: qsTrId("macro_tool.place_it"); onClicked: win.go(5) }
                }
                Item { Layout.fillHeight: true }
            }

            // ---- 4: which macro to place ----
            ColumnLayout {
                spacing: 6
                Step { text: qsTrId("macro_tool.wizard_pick") }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: palette.base
                    border.color: palette.mid
                    ListView {
                        id: zones
                        objectName: "macroZones"
                        anchors.fill: parent
                        anchors.margins: 1
                        boundsBehavior: Flickable.StopAtBounds
                        clip: true
                        model: win.zoneList
                        currentIndex: win.vm.picked
                        ScrollBar.vertical: ScrollBar {}
                        delegate: ItemDelegate {
                            id: z
                            required property int index
                            required property string name
                            required property int length
                            required property bool overlay
                            required property bool replace
                            required property string columns
                            required property bool saved
                            width: zones.width
                            padding: 2
                            highlighted: ListView.isCurrentItem
                            // the entire movie's zone, as Chimera says, does not change
                            text: qsTrId("macro_tool.length").arg(z.name).arg(z.length) + (z.index === 0 ? qsTrId("macro_tool.zones_don_t_change") : "")
                            onClicked: win.vm.picked = z.index
                            onDoubleClicked: { win.vm.picked = z.index; win.go(5) }
                            Component.onCompleted: if (z.index === win.vm.picked) win.pickedRow = z
                            ListView.onIsCurrentItemChanged: if (ListView.isCurrentItem) win.pickedRow = z
                        }
                    }
                }
                Button { text: qsTrId("macro_tool.load_macro_dialog"); onClicked: loadDialog.open() }
            }

            // ---- 5: where it goes, and how ----
            ColumnLayout {
                spacing: 8
                Step { text: qsTrId("macro_tool.wizard_where").arg(win.pickedRow ? win.pickedRow.name : "") }
                RowLayout {
                    Label { text: qsTrId("macro_tool.frame") }
                    SpinBox { id: placeAt; objectName: "macroAt"; from: 0; to: Math.max(0, win.pianoRoll.frameCount); editable: true; Layout.preferredWidth: 120 }
                    Button { text: qsTrId("macro_tool.current"); onClicked: placeAt.value = win.pianoRoll.currentFrame }
                }
                RadioButton {
                    text: qsTrId("macro_tool.write_over")
                    checked: win.pickedRow ? win.pickedRow.replace : true
                    onToggled: if (checked) win.vm.run("setReplace", [win.vm.picked, true])
                }
                RadioButton {
                    text: qsTrId("macro_tool.insert_before")
                    checked: win.pickedRow ? !win.pickedRow.replace : false
                    onToggled: if (checked) win.vm.run("setReplace", [win.vm.picked, false])
                }
                CheckBox {
                    text: qsTrId("macro_tool.press_on_top")
                    checked: win.pickedRow ? win.pickedRow.overlay : false
                    onToggled: win.vm.run("setOverlay", [win.vm.picked, checked])
                }
                Button {
                    objectName: "macroPlaceButton"
                    text: qsTrId("macro_tool.place")
                    highlighted: true
                    onClicked: win.vm.run("place", [win.vm.picked, placeAt.value])
                }
                Item { Layout.fillHeight: true }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Button { text: qsTrId("macro_tool.start_over"); visible: win.page > 0; onClicked: win.startOver() }
            Item { Layout.fillWidth: true }
            Button { text: qsTrId("new_project.back"); Layout.preferredWidth: 90; enabled: win.page > 0; onClicked: win.back() }
            Button {
                objectName: "macroNext"
                text: qsTrId("new_project.next")
                Layout.preferredWidth: 90
                visible: win.page !== 3 && win.page !== 5
                onClicked: win.next()
            }
            Button { text: qsTrId("common.close"); Layout.preferredWidth: 90; onClicked: win.close() }
        }
    }
}
