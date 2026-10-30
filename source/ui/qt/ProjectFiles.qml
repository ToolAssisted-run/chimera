// Chimera's Locate Project Files (its project resolution form), laid out
// as it is: shown while a project's files were not found beside it nor
// where this machine last had them, or the firmware it needs was not found
// (issue 397: listed after the files, "firmware" in their Slot column, and
// remembered for the core once located). The files listed under File, Slot and
// Status (a missing one in the warning colour, one that is not the
// recorded bytes in the error colour), what the one picked is under them,
// and Locate... (or a double-click), Scan Folder... (Include sub-folders),
// Open (Open Anyway past a mismatch, never past a missing file) and
// Cancel. A dialog of the main window; Enter opens, Escape cancels.
// Layout and bindings only, over the session's waiting project.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ChimeraDialogs

ApplicationWindow {
    id: win
    objectName: "projectFilesWindow"
    required property QtObject session
    property Window mainWindow: null
    property var themeColor: (role, fallback) => fallback
    title: qsTrId("project_files.locate_project_files")
    width: 600
    height: 360
    minimumWidth: 480
    minimumHeight: 270
    modality: Qt.ApplicationModal
    transientParent: win.mainWindow
    visible: win.session.resolvingFiles
    readonly property var widths: [200, 90, 280]
    // what the scan last said, in Chimera's words, over the line about the file picked
    property string scanSaid: ""
    property int picked: -1
    property bool anyMissing: false
    property bool anyMismatch: false
    function recount() {
        let missing = false, mismatch = false
        for (let i = 0; i < files.count; i++) {
            const row = files.itemAtIndex(i)
            if (!row) continue
            if (row.status === 1) missing = true
            if (row.status === 2) mismatch = true
        }
        win.anyMissing = missing
        win.anyMismatch = mismatch
    }
    // closing it is Cancel
    onClosing: if (win.session.resolvingFiles) win.session.run("cancelOpen")
    onVisibleChanged: if (visible) { win.picked = -1; win.scanSaid = "" }
    function open() { if (!win.anyMissing) win.session.run("openWithFiles") }
    Shortcut { sequences: ["Return", "Enter"]; enabled: win.active; onActivated: win.open() }
    Shortcut { sequence: "Escape"; enabled: win.active; onActivated: win.session.run("cancelOpen") }
    MessageDialog {
        id: unreadable
        title: qsTrId("project_files.cannot_use_that_file")
        buttons: MessageDialog.Ok
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        Label { text: qsTrId("project_files.the_project_needs_these_files_locate") }
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
                        model: [qsTrId("project_files.file"), qsTrId("project_files.slot"), qsTrId("common.status")]
                        delegate: Label {
                            required property int index
                            required property string modelData
                            text: modelData
                            width: win.widths[index]
                            leftPadding: 4; topPadding: 2; bottomPadding: 2
                            background: Rectangle { color: palette.button; border.color: palette.mid }
                        }
                    }
                }
                ListView {
                    id: files
                    objectName: "projectFileList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: win.session.rows("projectFiles")
                    ScrollBar.vertical: ScrollBar {}
                    onCountChanged: Qt.callLater(win.recount)
                    delegate: Rectangle {
                        id: row
                        required property int index
                        required property string name
                        required property string slot
                        required property int status
                        required property string statusText
                        required property string sha1
                        required property string actualSha1
                        width: files.width
                        height: 20
                        readonly property bool isPicked: row.index === win.picked
                        color: row.isPicked ? palette.highlight : "transparent"
                        // the row in the warning colour when missing, the error colour when not the recorded bytes
                        readonly property color ink: row.isPicked ? palette.highlightedText
                            : row.status === 2 ? win.themeColor("AccentError", "#c62828")
                            : row.status === 1 ? win.themeColor("AccentWarning", "#a05a00") : palette.text
                        onStatusChanged: Qt.callLater(win.recount)
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            Label { text: row.name; width: win.widths[0]; leftPadding: 4; elide: Text.ElideMiddle; color: row.ink }
                            Label { text: row.slot; width: win.widths[1]; leftPadding: 4; elide: Text.ElideRight; color: row.ink }
                            Label { text: row.statusText; width: win.widths[2]; leftPadding: 4; elide: Text.ElideRight; color: row.ink }
                        }
                        TapHandler {
                            onTapped: { win.picked = row.index; win.scanSaid = "" }
                            onDoubleTapped: { win.picked = row.index; locateDialog.open() }
                        }
                    }
                }
            }
        }
        Label {
            objectName: "projectFileDetail"
            Layout.fillWidth: true
            elide: Text.ElideRight
            text: {
                if (win.scanSaid !== "") return win.scanSaid
                const row = win.picked >= 0 ? files.itemAtIndex(win.picked) : null
                if (!row) return ""
                if (row.status === 2) return qsTrId("project_files.recorded_provided").arg(row.sha1).arg(row.actualSha1)
                if (row.status === 0) return qsTrId("project_files.sha1").arg(row.sha1)
                return qsTrId("project_files.double_click_or_press_locate_to")
            }
        }
        RowLayout {
            Button { text: qsTrId("project_files.locate_3"); enabled: win.picked >= 0; onClicked: locateDialog.open() }
            Button { text: qsTrId("common.scan_folder"); onClicked: scanDialog.open() }
            Item { Layout.fillWidth: true }
            // a missing file cannot be passed over; one that is not the recorded bytes can, knowingly
            Button {
                text: win.anyMismatch ? qsTrId("project_files.open_anyway") : qsTrId("project_files.open")
                Layout.preferredWidth: 90
                enabled: !win.anyMissing
                onClicked: win.open()
            }
            Button { text: qsTrId("common.cancel"); Layout.preferredWidth: 90; onClicked: win.session.run("cancelOpen") }
        }
    }
    FileDialog {
        id: locateDialog
        title: win.picked >= 0 && files.itemAtIndex(win.picked) ? qsTrId("project_files.locate").arg(files.itemAtIndex(win.picked).name) : qsTrId("project_files.locate_2")
        onAccepted: {
            win.session.run("locateProjectFile", [win.picked, selectedFile])
            // what the engine could not read, in Chimera's message box
            if (win.session.error !== "") {
                unreadable.text = win.session.error.replace(/^Cannot read that file: /, "")
                unreadable.open()
            }
        }
    }
    FolderDialog {
        id: scanDialog
        title: qsTrId("project_files.scan_a_folder_for_the_project")
        // in the dialog itself, as a box of the chooser's (docs/file-picker.md)
        checkBoxText: qsTrId("project_files.include_sub_folders")
        checkBoxChecked: true
        onAccepted: {
            const resolved = win.session.run("scanForProjectFiles", [selectedFolder, checkBoxChecked])
            // in Chimera's words
            win.scanSaid = !resolved ? qsTrId("project_files.the_scan_found_nothing_new") : resolved === 1 ? qsTrId("project_files.the_scan_resolved_1_file") : qsTrId("project_files.the_scan_resolved_files").arg(resolved)
        }
    }
}
