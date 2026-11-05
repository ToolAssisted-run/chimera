// Macro Input, as Chimera's: the File menu (Save Selected As..., Load
// Macro..., Recent); the zone's start and end with Set Macro; the zones, the
// entire movie first; the picked zone's name, the frame it goes to with
// Current, Place Zone, Replace and Overlay; and the movie's controls along
// the bottom, each ticked when the picked zone carries it. Layout and
// bindings only, over the macroTool view model.
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
    width: 420
    height: 460
    minimumWidth: 420
    minimumHeight: 460

    WindowOptions { id: windowOptions; window: win; tool: "macroInput"; vm: win.toolWindows; mainWindow: win.mainWindow }
    onVisibleChanged: if (visible) windowOptions.placeBack(false)

    readonly property var zoneList: win.vm.rows("zones")
    readonly property var movieColumns: JSON.parse(win.vm.movieColumns)
    property var pickedRow: null

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
                    saveDialog.zone = win.vm.picked
                    saveDialog.open()
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
        text: win.vm.error !== "" ? win.vm.error : win.vm.status
        color: win.vm.error !== "" ? win.themeColor("AccentError", "#b00020") : palette.text
        padding: 4
        elide: Text.ElideRight
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 7
        spacing: 6
        Label { text: qsTrId("macro_tool.macro_start_macro_end") }
        RowLayout {
            SpinBox {
                id: zoneStart; from: 0; to: Math.max(0, win.pianoRoll.frameCount - 1); editable: true
                value: win.pianoRoll.selectFirst >= 0 ? win.pianoRoll.selectFirst : 0
                Layout.preferredWidth: 100
            }
            SpinBox {
                id: zoneEnd; from: 0; to: Math.max(0, win.pianoRoll.frameCount - 1); editable: true
                value: win.pianoRoll.selectLast >= 0 ? win.pianoRoll.selectLast : 0
                Layout.preferredWidth: 100
            }
            Button { text: qsTrId("macro_tool.set_macro"); onClicked: win.vm.run("setZone", [zoneStart.value, zoneEnd.value]) }
        }
        ListView {
            boundsBehavior: Flickable.StopAtBounds
            id: zones
            objectName: "macroZones"
            Layout.fillWidth: true
            Layout.preferredHeight: 140
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
                Component.onCompleted: if (z.index === win.vm.picked) win.pickedRow = z
                ListView.onIsCurrentItemChanged: if (ListView.isCurrentItem) win.pickedRow = z
            }
        }
        GridLayout {
            columns: 4
            enabled: win.pickedRow !== null
            Label { text: qsTrId("macro_tool.macro_name") }
            TextField {
                Layout.preferredWidth: 110
                text: win.pickedRow ? win.pickedRow.name : ""
                onTextEdited: if (win.pickedRow) win.vm.run("rename", [win.vm.picked, text])
            }
            Label { text: qsTrId("macro_tool.frame") }
            SpinBox { id: placeAt; from: 0; to: Math.max(0, win.pianoRoll.frameCount); editable: true; Layout.preferredWidth: 90 }
            Button { text: qsTrId("macro_tool.place_zone"); onClicked: win.vm.run("place", [win.vm.picked, placeAt.value]) }
            CheckBox { text: qsTrId("macro_tool.replace"); checked: win.pickedRow ? win.pickedRow.replace : true; onToggled: win.vm.run("setReplace", [win.vm.picked, checked]) }
            CheckBox { text: qsTrId("macro_tool.overlay"); checked: win.pickedRow ? win.pickedRow.overlay : false; onToggled: win.vm.run("setOverlay", [win.vm.picked, checked]) }
            Button { text: qsTrId("macro_tool.current"); onClicked: placeAt.value = win.pianoRoll.currentFrame }
        }
        // the movie's controls, each ticked when the picked zone carries it: as Chimera places them, the last
        // in the bottom row and the rows right-aligned (laid out from the end and turned over); more than the
        // window holds (a keyboard's, as Ruffle's) scroll (issue 472)
        ScrollView {
            id: controlsScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            contentWidth: availableWidth
            contentHeight: Math.max(availableHeight, controlsFlow.implicitHeight)
            clip: true
            Flow {
                id: controlsFlow
                width: controlsScroll.availableWidth
                y: Math.max(0, controlsScroll.availableHeight - implicitHeight)
                rotation: 180
                spacing: 4
                enabled: win.pickedRow !== null
                Repeater {
                    model: win.movieColumns.slice().reverse()
                    delegate: CheckBox {
                        required property string modelData
                        text: modelData
                        padding: 1
                        rotation: 180
                        readonly property var carried: win.pickedRow && win.pickedRow.columns !== "" ? win.pickedRow.columns.split("|") : win.movieColumns
                        checked: carried.indexOf(modelData) >= 0
                        opacity: checked ? 1 : 0.6
                        onToggled: {
                            const now = win.movieColumns.filter(c => c === modelData ? checked : carried.indexOf(c) >= 0)
                            win.vm.run("setColumns", [win.vm.picked, now.length === win.movieColumns.length ? "" : now.join("|")])
                        }
                    }
                }
            }
        }
    }
}
