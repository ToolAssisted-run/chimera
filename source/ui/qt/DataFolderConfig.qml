// Chimera's Data Directory (Config > Data Directory), laid out as Chimera's
// The Data Directory window: what the directory holds, where it is (in bold), how
// much is in it and free on its drive, the change waiting for the next
// start, what was last said, and Change..., Use Default, Keep Current, Open
// Folder and Close. Picking a folder asks Chimera's questions: whether to
// use one that already holds data as it is, or whether to move what is
// here. Layout and bindings only, over the dataFolder view model.
import QtQuick
import QtQuick.Controls
import ChimeraDialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "dataFolderWindow"
    required property QtObject vm
    property var themeColor: (role, fallback) => fallback
    property Window mainWindow: null
    title: qsTrId("data_folder_config.data_directory")
    width: 640
    height: 300
    minimumWidth: 640
    minimumHeight: 300
    modality: Qt.platform.os === "osx" ? Qt.WindowModal : Qt.ApplicationModal // a sheet on a Mac, which keeps an application-modal window over other apps (#541)
    transientParent: win.mainWindow
    onVisibleChanged: if (visible) { win.said = ""; win.vm.run("refresh") }

    // the last thing the window said (Chimera's status line)
    property string said: ""
    // the folder being asked about ("" for the default), and what it is
    property string asking: ""
    property var found: ({})
    function request(folder) {
        win.asking = folder
        const what = JSON.parse(win.vm.run("inspect", [folder]) || "{}")
        win.found = what
        if (what.choice === "same" || what.choice === "nested" || what.choice === "unwritable") {
            win.vm.run("request", [folder, false])
            win.said = win.vm.error !== "" ? win.vm.error : win.vm.status
            return
        }
        if (what.choice === "holdsData") useAsIs.open()
        else moveAsk.open()
    }
    function recorded(move) {
        win.vm.run("request", [win.asking, move])
        win.said = win.vm.error !== "" ? win.vm.error : win.vm.status
        win.vm.run("refresh")
    }

    MessageDialog {
        id: useAsIs
        title: qsTrId("data_folder_config.data_directory")
        text: qsTrId("data_folder_config.already_holds_chimera_data_use_it").arg(win.found.target || "")
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: win.recorded(false)
    }
    MessageDialog {
        id: moveAsk
        title: qsTrId("data_folder_config.data_directory")
        text: qsTrId("data_folder_config.move_the_already_here_to").arg(win.found.size || "").arg(win.found.target || "")
            + (win.found.tight ? qsTrId("data_folder_config.there_may_not_be_room_to").arg(win.found.size).arg(win.found.free) : "")
            + qsTrId("data_folder_config.yes_it_is_moved_when_chimera")
        buttons: MessageDialog.Yes | MessageDialog.No | MessageDialog.Cancel
        onButtonClicked: (button) => {
            if (button === MessageDialog.Yes) win.recorded(true)
            else if (button === MessageDialog.No) win.recorded(false)
        }
    }
    FolderDialog {
        id: folderDialog
        title: qsTrId("data_folder_config.where_chimera_should_keep_its_data")
        onAccepted: win.request(localFiles.localPath(selectedFolder))
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 6
        Label {
            text: qsTrId("data_folder_config.everything_chimera_keeps_for_you_is")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label { text: (win.vm.current, win.vm.standard, win.vm.environment, win.vm.run("inUse") || ""); font.bold: true; elide: Text.ElideMiddle; Layout.fillWidth: true; Layout.topMargin: 6 }
        Label { text: win.vm.figures; Layout.fillWidth: true }
        Label {
            text: win.vm.environment !== "" ? qsTrId("data_folder_config.chimera_data_home_is_set_and")
                : win.vm.pending === "" ? ""
                : qsTrId("data_folder_config.at_the_next_start_this_becomes").arg(win.vm.pending) + (win.vm.pendingMove ? qsTrId("data_folder_config.and_everything_here_is_moved_there") : qsTrId("data_folder_config.what_is_here_stays_here"))
            color: win.themeColor("AccentWarning", "#b26a00")
            wrapMode: Text.WrapAnywhere
            Layout.fillWidth: true
            Layout.preferredHeight: 40
        }
        Label { text: win.said; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.preferredHeight: 56 }
        Item { Layout.fillHeight: true }
        RowLayout {
            Button { text: qsTrId("data_folder_config.change"); Layout.preferredWidth: 100; enabled: win.vm.environment === ""; onClicked: folderDialog.open() }
            Button {
                text: qsTrId("data_folder_config.use_default"); Layout.preferredWidth: 100
                // only when the directory is, or is to become, one of the person's, and the environment does not decide
                enabled: win.vm.environment === "" && (win.vm.pending !== "" ? win.vm.pending : win.vm.current) !== ""
                onClicked: win.request("")
            }
            Button {
                text: qsTrId("data_folder_config.keep_current"); Layout.preferredWidth: 110
                visible: win.vm.pending !== ""
                onClicked: { win.vm.run("withdraw"); win.said = win.vm.status }
            }
            Button {
                text: qsTrId("common.open_folder"); Layout.preferredWidth: 100
                onClicked: {
                    const at = win.vm.run("inUse")
                    if (!Qt.openUrlExternally(localFiles.folderUrl(at))) win.said = qsTrId("data_folder_config.could_not_open").arg(at)
                }
            }
            Item { Layout.fillWidth: true }
            Button { text: qsTrId("common.close"); Layout.preferredWidth: 100; onClicked: win.close() }
        }
    }
}
