// Chimera's Path Configuration (Config > Paths), laid out as Chimera's
// PathConfig: Always use recent path for ROMs and Special Commands at the
// top; Global and by System (a system picked, its own folders) as tabs, a
// row a folder (its path, a button to browse for it, its name); Save,
// Defaults, OK and Cancel. The rows are a copy: Save or OK keeps them. It
// opens on the system of the game open, as Chimera's does. Layout and
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
    modality: Qt.ApplicationModal
    transientParent: win.mainWindow

    readonly property var systemNames: JSON.parse(win.vm.systems)
    // the rows as typed, "system|name" to the path; what is not here is the view model's
    property var edits: ({})
    property bool recent: true
    function keyOf(system, name) { return system + "|" + name }
    // what a person reads for a folder: its name is the setting's identifier
    function labelOf(name) { return qsTrId("paths.folder_" + name.toLowerCase().replace(/[^a-z0-9]+/g, "_").replace(/^_|_$/g, "")) }
    function textOf(system, name, path) { const k = win.keyOf(system, name); return k in win.edits ? win.edits[k] : path }
    function edit(system, name, text) {
        const now = Object.assign({}, win.edits)
        now[win.keyOf(system, name)] = text
        win.edits = now
    }
    function save() {
        win.vm.recentForRoms = win.recent
        for (const k of Object.keys(win.edits)) {
            const at = k.indexOf("|")
            const system = k.substring(0, at), name = k.substring(at + 1)
            if (system === "Global") win.vm.run("setPath", [name, win.edits[k]])
            else win.vm.run("setSystemPath", [system, name, win.edits[k]])
        }
        win.edits = {}
    }
    onVisibleChanged: if (visible) {
        win.edits = {}
        win.recent = win.vm.recentForRoms
        // the system of the game open, when it has folders of its own
        const sys = win.session ? win.session.systemId : ""
        const at = win.systemNames.indexOf(sys)
        systemBox.currentIndex = at >= 0 ? at : (win.systemNames.length > 0 ? 0 : -1)
        tabs.currentIndex = at >= 0 ? 1 : 0
    }

    // Chimera's defaults for every global folder (the by System tab keeps what it has)
    readonly property var chimeraGlobals: ({
        "Base": ".", "ROM": ".", "Firmware": "./Firmware", "Movies": "./Movies", "Movie backups": "./Movies/backup", "A/V Dumps": ".",
        "Tools": "./Tools", "Lua": "./Lua", "Themes": "./Themes", "Watch (.wch)": ".", "Debug Logs": ".", "Bug reports": "./Reports",
        "Macros": "./Movies/Macros", "Temp Files": ""
    })

    // one tab's rows: the path, Browse, the name
    component PathRows: ScrollView {
        id: page
        required property string system
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
                    required property string system
                    required property string name
                    required property string path
                    visible: row.system === page.system
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? implicitHeight : 0
                    Layout.leftMargin: 6
                    Layout.rightMargin: 6
                    // the ROM folder is not used while the recent one is, as Chimera's
                    enabled: !(row.name === "ROM" && win.recent)
                    TextField {
                        id: box
                        objectName: "pathBox"
                        Layout.fillWidth: true
                        text: win.textOf(row.system, row.name, row.path)
                        onTextEdited: { win.edit(row.system, row.name, text); suggestions.refresh() }
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
                                        onClicked: { box.text = modelData; win.edit(row.system, row.name, modelData); suggestions.close() }
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
                            // a base or a global folder is browsed from the global base, as Chimera's
                            const forSystem = row.name === "Base" || row.system === "Global" ? "" : row.system
                            folderDialog.system = row.system
                            folderDialog.name = row.name
                            folderDialog.forSystem = forSystem
                            folderDialog.title = qsTrId("paths_config.set_the_directory_for").arg(win.labelOf(row.name))
                            folderDialog.currentFolder = localFiles.folderUrl(win.vm.run("absolute", [box.text, forSystem]))
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
        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: qsTrId("paths_config.global"); width: implicitWidth }
            TabButton { text: qsTrId("paths_config.by_system"); width: implicitWidth }
        }
        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 0
            StackLayout {
                anchors.fill: parent
                currentIndex: tabs.currentIndex
                PathRows { system: "Global" }
                ColumnLayout {
                    RowLayout {
                        Layout.margins: 4
                        Label { text: qsTrId("paths_config.system") }
                        ComboBox { id: systemBox; model: win.systemNames; Layout.preferredWidth: 200 }
                    }
                    PathRows {
                        system: systemBox.currentIndex >= 0 ? win.systemNames[systemBox.currentIndex] : ""
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Button { text: qsTrId("common.save"); onClicked: win.save() }
            Button {
                text: qsTrId("paths_config.defaults")
                onClicked: {
                    const now = Object.assign({}, win.edits)
                    for (const n of Object.keys(win.chimeraGlobals)) now[win.keyOf("Global", n)] = win.chimeraGlobals[n]
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
        property string system: ""
        property string name: ""
        property string forSystem: ""
        onAccepted: {
            const local = localFiles.localPath(selectedFolder)
            win.edit(folderDialog.system, folderDialog.name, win.vm.run("makeRelative", [local, folderDialog.forSystem]))
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
