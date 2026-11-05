// Chimera's Reproducible Media Maker (Tools), laid out as its form: what it
// is for at the top; Folder (read-only) with Browse..., Format with what
// the shape is for under it, what an installed core's recipe recognised the
// folder as, Save as with its own Save as...; the bar, the file being
// written, what came of it (the file, its SHA1 and size, or why not), and
// Make (Cancel while packing) and Close. A dialog of the main window, as
// Chimera's; Escape closes it, and closing during a pack asks first, then
// stops it. Layout
// and bindings only, over the mediaMaker view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ChimeraDialogs

ApplicationWindow {
    id: win
    objectName: "mediaMakerWindow"
    required property QtObject vm
    property Window mainWindow: null
    title: qsTrId("media_maker.reproducible_media_maker")
    width: 600
    height: 380
    minimumWidth: 600
    maximumWidth: 600
    minimumHeight: 380
    maximumHeight: 380
    modality: Qt.ApplicationModal
    transientParent: win.mainWindow
    flags: Qt.Dialog
    // closed while a file is being made: asked first, and the file is only
    // given up on Yes (issue 503)
    property bool closingAsked: false
    onClosing: (close) => {
        if (!win.vm.running || win.closingAsked) { win.closingAsked = false; if (win.vm.running) win.vm.run("cancel"); return }
        close.accepted = false
        stopAsk.open()
    }
    MessageDialog {
        id: stopAsk
        title: qsTrId("media_maker.reproducible_media_maker")
        text: qsTrId("media_maker.stop_making_and_close")
        buttons: MessageDialog.Yes | MessageDialog.No
        onButtonClicked: (button) => {
            if (button !== MessageDialog.Yes) return
            win.closingAsked = true
            win.close()
        }
    }
    // a path as a file dialog takes it
    function urlOf(path) { return localFiles.folderUrl(path) }
    readonly property var extensions: [".zip", ".iso", ".img"]

    Shortcut { sequence: "Escape"; onActivated: win.close() }

    component Caption: Label { Layout.preferredWidth: 80 }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 6
        Label {
            text: qsTrId("media_maker.a_folder_becomes_one_file_and")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            verticalAlignment: Text.AlignTop
        }
        GridLayout {
            columns: 3
            columnSpacing: 0
            rowSpacing: 6
            Layout.fillWidth: true
            Caption { text: qsTrId("media_maker.folder"); Layout.row: 0; Layout.column: 0 }
            TextField { text: win.vm.folder; readOnly: true; Layout.row: 0; Layout.column: 1; Layout.fillWidth: true }
            Button { text: qsTrId("common.browse"); Layout.row: 0; Layout.column: 2; Layout.leftMargin: 8; Layout.preferredWidth: 96; onClicked: folderDialog.open() }
            Caption { text: qsTrId("media_maker.format"); Layout.row: 1; Layout.column: 0 }
            ComboBox {
                Layout.row: 1
                Layout.column: 1
                Layout.fillWidth: true
                model: win.vm.run("shapes").split("\n")
                currentIndex: win.vm.format
                onActivated: (i) => win.vm.format = i
            }
            Label {
                text: win.vm.formatHelp
                wrapMode: Text.WordWrap
                Layout.row: 2
                Layout.column: 1
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                verticalAlignment: Text.AlignTop
            }
            // what a core's recipe recognised the folder as; empty for most folders
            Label {
                text: win.vm.recognised
                wrapMode: Text.WordWrap
                Layout.row: 3
                Layout.column: 1
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                verticalAlignment: Text.AlignTop
            }
            Caption { text: qsTrId("media_maker.save_as"); Layout.row: 4; Layout.column: 0 }
            TextField { text: win.vm.output; onEditingFinished: win.vm.output = text; Layout.row: 4; Layout.column: 1; Layout.fillWidth: true }
            Button { text: qsTrId("media_maker.save_as_dialog"); Layout.row: 4; Layout.column: 2; Layout.leftMargin: 8; Layout.preferredWidth: 96; onClicked: outputDialog.open() }
        }
        ProgressBar { Layout.fillWidth: true; Layout.topMargin: 6; from: 0; to: 1; value: win.vm.progress }
        Label { text: win.vm.current; elide: Text.ElideRight; Layout.fillWidth: true; Layout.preferredHeight: 18 }
        Label {
            objectName: "mediaResult"
            text: win.vm.error !== "" ? win.vm.error : win.vm.result
            wrapMode: Text.WrapAnywhere
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            verticalAlignment: Text.AlignTop
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            Item { Layout.fillWidth: true }
            Button {
                text: win.vm.running ? qsTrId("common.cancel") : qsTrId("media_maker.make")
                Layout.preferredWidth: 96
                enabled: win.vm.running || (win.vm.folder !== "" && win.vm.output !== "")
                onClicked: win.vm.run(win.vm.running ? "cancel" : "make")
            }
            Button { text: qsTrId("common.close"); Layout.preferredWidth: 96; onClicked: win.close() }
        }
    }
    FolderDialog {
        id: folderDialog
        title: qsTrId("media_maker.the_folder_to_pack")
        onAccepted: win.vm.run("setFolder", [selectedFolder])
    }
    FileDialog {
        id: outputDialog
        title: qsTrId("media_maker.write_the_image_to")
        fileMode: FileDialog.SaveFile
        selectedFile: win.urlOf(win.vm.output)
        defaultSuffix: win.extensions[win.vm.format].substring(1)
        nameFilters: [win.vm.run("shapes").split("\n")[win.vm.format] + " (*" + win.extensions[win.vm.format] + ")", qsTrId("media_maker.all_files")]
        onAccepted: win.vm.run("setOutput", [selectedFile])
    }
}
