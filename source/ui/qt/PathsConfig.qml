// Chimera's Path Configuration (Config > Paths), laid out as Chimera's
// PathConfig: Always use recent path for ROMs and Special Commands at the
// top; a row a folder (its path, a button to browse for it, its name); Save,
// Defaults, OK and Cancel. The rows are a copy: Save or OK keeps them. One
// list of folders: Chimera keeps none for a system (issue 655); Output is
// where each core's own files go, in a folder of its id. Layout and
// bindings only, over the paths view model.
import QtQuick
import QtQuick.Controls
import ChimeraDialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "pathsWindow"
    required property QtObject vm
    property QtObject session: null
    property Window mainWindow: null
    // the theme's colour for a role (Main hands it its lookup)
    property var themeColor: (role, fallback) => fallback
    title: qsTrId("paths_config.path_configuration")
    width: 784
    height: 540
    minimumWidth: 784
    minimumHeight: 540
    modality: Qt.platform.os === "osx" ? Qt.WindowModal : Qt.ApplicationModal // a sheet on a Mac, which keeps an application-modal window over other apps (#541)
    transientParent: win.mainWindow

    // the rows as typed, name to the path; what is not here is the view model's
    property var edits: ({})
    property bool recent: true
    // what a person reads for a folder: its name is the setting's identifier
    function labelOf(name) { return qsTrId("paths.folder_" + name.toLowerCase().replace(/[^a-z0-9]+/g, "_").replace(/^_|_$/g, "")) }
    function textOf(name, path) { return name in win.edits ? win.edits[name] : path }
    function edit(name, text) {
        const now = Object.assign({}, win.edits)
        now[name] = text
        win.edits = now
    }
    function save() {
        win.vm.recentForRoms = win.recent
        for (const name of Object.keys(win.edits)) win.vm.run("setPath", [name, win.edits[name]])
        win.edits = {}
    }
    onVisibleChanged: if (visible) {
        win.edits = {}
        win.recent = win.vm.recentForRoms
    }

    // Chimera's defaults for every folder
    readonly property var chimeraGlobals: ({
        "Base": ".", "ROM": ".", "Firmware": "./Firmware", "Movies": "./Movies", "Movie backups": "./Movies/backup", "A/V Dumps": ".",
        "Tools": "./Tools", "Lua": "./Lua", "Themes": "./themes", "Watch (.wch)": ".", "Debug Logs": ".", "Bug reports": "./Reports",
        "Macros": "./Movies/Macros", "Output": "./output", "Temp Files": ""
    })

    // one tab's rows: the path, Browse, the name
    component PathRows: ScrollView {
        id: page
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: page.availableWidth
            spacing: 7
            Item { height: 6 }
            Repeater {
                model: win.vm.rows("paths")
                delegate: RowLayout {
                    id: row
                    required property string name
                    required property string path
                    Layout.fillWidth: true
                    Layout.leftMargin: 6
                    Layout.rightMargin: 6
                    // the ROM folder is not used while the recent one is, as Chimera's
                    enabled: !(row.name === "ROM" && win.recent)
                    TextField {
                        id: box
                        objectName: "pathBox"
                        Layout.fillWidth: true
                        text: win.textOf(row.name, row.path)
                        onTextEdited: { win.edit(row.name, text); suggestions.refresh() }
                        // Chimera's suggestions: the special words
                        Popup {
                            id: suggestions
                            y: box.height
                            width: 120
                            padding: 1
                            property var matches: []
                            function refresh() {
                                const t = box.text
                                suggestions.matches = t === "" ? [] : ["%recent%", "%exe%", "%rom%"].filter(w => w.startsWith(t) && w !== t)
                                if (suggestions.matches.length > 0 && box.activeFocus) open(); else close()
                            }
                            contentItem: Column {
                                Repeater {
                                    model: suggestions.matches
                                    delegate: ItemDelegate {
                                        required property string modelData
                                        width: 118
                                        padding: 2
                                        text: modelData
                                        onClicked: { box.text = modelData; win.edit(row.name, modelData); suggestions.close() }
                                    }
                                }
                            }
                        }
                    }
                    ToolButton {
                        icon.source: "qrc:/chimera/icons/OpenFile.png"
                        Layout.preferredWidth: 26
                        Layout.preferredHeight: 23
                        onClicked: {
                            folderDialog.name = row.name
                            folderDialog.title = qsTrId("paths_config.set_the_directory_for").arg(win.labelOf(row.name))
                            folderDialog.currentFolder = localFiles.folderUrl(win.vm.run("absolute", [box.text]))
                            folderDialog.open()
                        }
                    }
                    Label { text: win.labelOf(row.name); Layout.preferredWidth: 100; elide: Text.ElideRight }
                }
            }
            Item { Layout.fillHeight: true }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            CheckBox { text: qsTrId("paths_config.always_use_recent_path_for_roms"); checked: win.recent; onToggled: win.recent = checked; padding: 2 }
            Item { Layout.fillWidth: true }
            ToolButton {
                icon.source: "qrc:/chimera/icons/Help.png"
                Layout.preferredWidth: 26
                Layout.preferredHeight: 23
                onClicked: specialCommands.show()
            }
            Label { text: qsTrId("paths_config.special_commands") }
        }
        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 0
            PathRows { anchors.fill: parent }
        }
        RowLayout {
            Layout.fillWidth: true
            Button { text: qsTrId("common.save"); onClicked: win.save() }
            Button {
                text: qsTrId("paths_config.defaults")
                onClicked: {
                    const now = Object.assign({}, win.edits)
                    for (const n of Object.keys(win.chimeraGlobals)) now[n] = win.chimeraGlobals[n]
                    win.edits = now
                }
            }
            Label { text: win.vm.error; color: win.themeColor("AccentError", "#b00020"); elide: Text.ElideRight; Layout.fillWidth: true }
            Button { text: qsTrId("common.ok_mnemonic"); onClicked: { win.save(); win.close() } }
            Button { text: qsTrId("common.cancel_mnemonic"); onClicked: win.close() }
        }
    }
    FolderDialog {
        id: folderDialog
        property string name: ""
        onAccepted: {
            const local = localFiles.localPath(selectedFolder)
            win.edit(folderDialog.name, win.vm.run("makeRelative", [local]))
        }
    }

    // Chimera's Special Commands window: what the special words in a path mean
    Window {
        id: specialCommands
        title: qsTrId("paths_config.special_commands")
        width: 466
        height: 190
        transientParent: win
        color: palette.window
        GridLayout {
            anchors.fill: parent
            anchors.margins: 13
            columns: 2
            columnSpacing: 20
            rowSpacing: 4
            Label { text: "%recent%" }
            Label { text: qsTrId("paths_config.sets_the_path_to_the_windows") }
            Label { text: "%exe%" }
            Label { text: qsTrId("paths_config.sets_the_path_of_the_executable") }
            Label { text: ".\\"; Layout.topMargin: 12 }
            Label { text: qsTrId("paths_config.sets_the_path_to_the_base"); Layout.topMargin: 12 }
            Label { text: "..\\" }
            Label { text: qsTrId("paths_config.sets_the_path_to_the_folder") }
            Label {
                Layout.columnSpan: 2
                Layout.leftMargin: 80
                text: qsTrId("paths_config.setting_the_global_base_path_to")
            }
            Item { Layout.fillHeight: true; Layout.columnSpan: 2 }
            Item { Layout.fillWidth: true }
            Button { text: qsTrId("common.ok_mnemonic"); onClicked: specialCommands.close(); Layout.alignment: Qt.AlignRight }
        }
    }
}
