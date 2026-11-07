// Chimera's Core Manager (Tools > Core Manager...), laid out as its
// The Core Manager: the folder and how a core arrives at the top, Select all
// and Show over the list, a row a core with a box to tick (Core, Type,
// Systems, Version, Size), the versions of the core picked on the right
// (Remove version, where it is, when it was built, its terms), the line
// under the list, and Open cores folder, Change folder..., Refresh List,
// Remove, Show Systems... and Close. Chimera downloads nothing: a core is a
// .chimeraCore file someone put in the folder. Layout and bindings only,
// over the cores view model.
import QtQuick
import QtQuick.Controls
import ChimeraDialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "coresWindow"
    required property QtObject vm
    title: qsTrId("cores.core_manager")
    width: 1100
    height: 500
    minimumWidth: 900
    minimumHeight: 420

    // the columns: Systems takes the room the others leave, and Version gives
    // some up when the window is narrow, so the list never runs under the
    // versions beside it (issue 505)
    readonly property var widths: {
        const room = coreListBox.width - 24 - 12
        const version = Math.min(250, Math.max(150, room - 150 - 70 - 70 - 80))
        return [150, 70, Math.max(80, room - 150 - 70 - version - 70), version, 70]
    }
    // the cores ticked, by name, and the one picked
    property var ticked: ({})
    readonly property int tickedCount: Object.keys(win.ticked).length
    property string picked: ""
    readonly property var versions: win.picked === "" ? [] : JSON.parse(win.vm.run("versionsOf", [win.picked]) || "[]")
    onVisibleChanged: if (visible) { win.vm.run("refresh"); win.ticked = {}; win.picked = ""; Qt.callLater(win.pickFirst) }
    // the first core listed is picked when none is
    function pickFirst() {
        if (win.picked === "" && list.count > 0 && list.itemAtIndex(0)) win.picked = list.itemAtIndex(0).core
    }
    function tick(name, on) {
        const now = Object.assign({}, win.ticked)
        if (on) now[name] = true; else delete now[name]
        win.ticked = now
    }

    FolderDialog {
        id: folderDialog
        title: qsTrId("cores.the_cores_folder")
        currentFolder: localFiles.folderUrl(win.vm.coresFolder)
        onAccepted: {
            win.vm.coresFolder = localFiles.localPath(selectedFolder)
            win.vm.managerStatus = qsTrId("cores.the_cores_folder_is_now_core").arg(win.vm.coresFolder).arg(list.count)
            win.picked = ""
            Qt.callLater(win.pickFirst)
        }
    }
    MessageDialog {
        id: removeCores
        title: qsTrId("cores.remove_cores")
        property int versions: 0
        text: qsTrId("cores.delete_package_file_s_of_core").arg(removeCores.versions).arg(win.tickedCount)
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: { win.vm.run("removeCores", [JSON.stringify(Object.keys(win.ticked))]); win.ticked = {}; win.picked = ""; Qt.callLater(win.pickFirst) }
    }
    MessageDialog {
        id: removeVersion
        title: qsTrId("cores.remove_core")
        property string path: ""
        text: qsTrId("cores.delete_a_movie_recorded_on_this").arg(removeVersion.path.replace(/^.*[\\/]/, ""))
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: win.vm.run("removeVersion", [removeVersion.path])
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6
        Label { text: win.vm.managerHeader; Layout.fillWidth: true; elide: Text.ElideRight }
        RowLayout {
            Layout.fillWidth: true
            CheckBox {
                text: win.tickedCount > 0 ? qsTrId("cores.select_all_ticked").arg(win.tickedCount) : qsTrId("cores.select_all")
                checked: list.count > 0 && win.tickedCount === list.count
                onClicked: {
                    const now = {}
                    if (checked) for (let i = 0; i < list.count; i++) { const it = list.itemAtIndex(i); if (it) now[it.core] = true }
                    win.ticked = now
                }
                padding: 2
            }
            Item { Layout.fillWidth: true }
            Label { text: qsTrId("cores.show") }
            RadioButton { text: qsTrId("cores.all"); checked: win.vm.shows === 0; padding: 2; onClicked: { win.vm.shows = 0; win.ticked = {} } }
            RadioButton { text: qsTrId("common.emulators"); checked: win.vm.shows === 1; padding: 2; onClicked: { win.vm.shows = 1; win.ticked = {} } }
            RadioButton { text: qsTrId("common.games"); checked: win.vm.shows === 2; padding: 2; onClicked: { win.vm.shows = 2; win.ticked = {} } }
            Item { Layout.preferredWidth: 328 }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            Rectangle {
                id: coreListBox
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: palette.base
                border.color: palette.mid
                clip: true
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 1
                    spacing: 0
                    Row {
                        Repeater {
                            model: [qsTrId("common.core"), qsTrId("cores.type"), qsTrId("cores.systems"), qsTrId("cores.version"), qsTrId("common.size")]
                            delegate: Label {
                                required property int index
                                required property string modelData
                                text: modelData
                                width: win.widths[index] + (index === 0 ? 22 : 0)
                                leftPadding: 4; topPadding: 2; bottomPadding: 2
                                horizontalAlignment: index === 4 ? Text.AlignRight : Text.AlignLeft
                                rightPadding: index === 4 ? 4 : 0
                                background: Rectangle { color: palette.button; border.color: palette.mid }
                            }
                        }
                    }
                    ListView {
                        boundsBehavior: Flickable.StopAtBounds
                        id: list
                        objectName: "coreList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: win.vm.rows("manager")
                        ScrollBar.vertical: ScrollBar {}
                        onCountChanged: Qt.callLater(win.pickFirst)
                        delegate: Rectangle {
                            id: row
                            required property int index
                            required property string core
                            required property string type
                            required property string systems
                            required property string version
                            required property string size
                            width: list.width
                            height: 22
                            readonly property bool isPicked: row.core === win.picked
                            color: isPicked ? palette.highlight : "transparent"
                            readonly property color ink: isPicked ? palette.highlightedText : palette.text
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                CheckBox {
                                    checked: win.ticked[row.core] === true
                                    onClicked: win.tick(row.core, checked)
                                    padding: 0
                                    width: 22
                                }
                                Label { text: row.core; color: row.ink; width: win.widths[0]; elide: Text.ElideRight; leftPadding: 2 }
                                Label { text: row.type; color: row.ink; width: win.widths[1]; leftPadding: 4 }
                                Label { text: row.systems; color: row.ink; width: win.widths[2]; elide: Text.ElideRight; leftPadding: 4 }
                                Label { text: row.version; color: row.ink; width: win.widths[3]; elide: Text.ElideRight; leftPadding: 4 }
                                Label { text: row.size; color: row.ink; width: win.widths[4]; horizontalAlignment: Text.AlignRight; rightPadding: 4 }
                            }
                            TapHandler { onTapped: win.picked = row.core }
                        }
                    }
                }
            }
            // the versions of the core picked
            ColumnLayout {
                Layout.preferredWidth: 320
                Layout.maximumWidth: 320
                Layout.fillHeight: true
                Label { text: qsTrId("cores.version") }
                ComboBox {
                    id: versionBox
                    Layout.fillWidth: true
                    model: win.versions.map(v => v.label)
                    onModelChanged: currentIndex = 0
                }
                Button {
                    text: qsTrId("cores.remove_version")
                    enabled: versionBox.currentIndex >= 0 && versionBox.currentIndex < win.versions.length
                    Layout.preferredWidth: 155
                    onClicked: { removeVersion.path = win.versions[versionBox.currentIndex].path; removeVersion.open() }
                }
                // where it is, when it was built and its terms, scrolled when longer than the room (issue 461)
                ScrollView {
                    id: detailScroll
                    objectName: "versionDetail"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: 10
                    Layout.topMargin: 8
                    contentWidth: availableWidth
                    clip: true
                    Label {
                        width: detailScroll.availableWidth
                        text: versionBox.currentIndex >= 0 && versionBox.currentIndex < win.versions.length
                            ? (win.vm.run("versionDetail", [win.versions[versionBox.currentIndex].path]) || "") : ""
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
        Label { text: win.vm.managerStatus; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.preferredHeight: 32 }
        RowLayout {
            Layout.fillWidth: true
            Button {
                text: qsTrId("cores.open_cores_folder"); Layout.preferredWidth: 140
                onClicked: {
                    const folder = win.vm.run("makeCoresFolder") || ""
                    if (folder !== "" && !Qt.openUrlExternally(localFiles.folderUrl(folder))) win.vm.managerStatus = qsTrId("cores.could_not_be_opened").arg(folder)
                }
            }
            Button { text: qsTrId("cores.change_folder"); Layout.preferredWidth: 140; onClicked: folderDialog.open() }
            Button { text: qsTrId("cores.refresh_list"); Layout.preferredWidth: 140; onClicked: win.vm.run("refreshList") }
            Button {
                text: qsTrId("common.remove"); Layout.preferredWidth: 140
                enabled: win.tickedCount > 0
                onClicked: {
                    let n = 0
                    for (const name of Object.keys(win.ticked)) n += JSON.parse(win.vm.run("versionsOf", [name]) || "[]").length
                    removeCores.versions = n
                    removeCores.open()
                }
            }
            Button { text: qsTrId("cores.show_systems"); Layout.preferredWidth: 140; onClicked: { systemsWindow.all = JSON.parse(win.vm.run("supportedSystems") || "[]"); systemsWindow.show() } }
            Item { Layout.fillWidth: true }
            Button { text: qsTrId("common.close"); Layout.preferredWidth: 90; onClicked: win.close() }
        }
    }

    // Chimera's Supported Systems: which machines the cores here add up to, and which core runs each
    Window {
        id: systemsWindow
        objectName: "supportedSystemsWindow"
        title: qsTrId("cores.supported_systems")
        width: 720
        height: 520
        minimumWidth: 480
        minimumHeight: 300
        transientParent: win
        color: palette.window
        property var all: []
        // a system answers the filter by its name, its id or one of its cores
        readonly property var shown: {
            const t = filterBox.text.trim().toLowerCase()
            return t === "" ? systemsWindow.all
                : systemsWindow.all.filter(e => e.name.toLowerCase().includes(t) || e.id.toLowerCase().includes(t) || e.cores.toLowerCase().includes(t))
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            RowLayout {
                Label { text: qsTrId("cores.filter") }
                TextField { id: filterBox; Layout.fillWidth: true }
                Label {
                    text: systemsWindow.shown.length === systemsWindow.all.length ? qsTrId("cores.systems_2").arg(systemsWindow.all.length)
                        : qsTrId("cores.of").arg(systemsWindow.shown.length).arg(systemsWindow.all.length)
                    Layout.preferredWidth: 100
                    horizontalAlignment: Text.AlignRight
                }
            }
            Rectangle {
                id: systemsBox
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: palette.base
                border.color: palette.mid
                clip: true
                // the columns: Cores takes the room the others leave, and
                // System gives some up in a narrow window (issue 506)
                readonly property real systemWidth: Math.min(260, Math.max(140, systemsBox.width - 2 - 110 - 120 - 14))
                readonly property real coresWidth: Math.max(120, systemsBox.width - 2 - systemWidth - 110 - 14)
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 1
                    spacing: 0
                    Row {
                        Repeater {
                            model: [[qsTrId("cores.system"), systemsBox.systemWidth], [qsTrId("cores.id"), 110], [qsTrId("cores.cores"), systemsBox.coresWidth]]
                            delegate: Label {
                                required property var modelData
                                text: modelData[0]; width: modelData[1]
                                leftPadding: 4; topPadding: 2; bottomPadding: 2
                                background: Rectangle { color: palette.button; border.color: palette.mid }
                            }
                        }
                    }
                    ListView {
                        boundsBehavior: Flickable.StopAtBounds
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: systemsWindow.shown
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Row {
                            required property var modelData
                            height: 20
                            Label { text: modelData.name; width: systemsBox.systemWidth; leftPadding: 4; elide: Text.ElideRight }
                            Label { text: modelData.id; width: 110; leftPadding: 4; elide: Text.ElideRight }
                            Label { text: modelData.cores; width: systemsBox.coresWidth; leftPadding: 4; elide: Text.ElideRight }
                        }
                    }
                }
            }
            RowLayout {
                Item { Layout.fillWidth: true }
                Button { text: qsTrId("common.close"); Layout.preferredWidth: 90; onClicked: systemsWindow.close() }
            }
        }
    }
}
