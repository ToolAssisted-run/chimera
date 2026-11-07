// Chimera's Report an Issue (Help), laid out as its form, a dialog of the
// main window: 1. the report form's address with Open in Browser and Copy
// Link; 2. the report's first lines, filled in from this session, with
// Copy Text; 3. the files worth attaching, each with a box ticked (the game
// as it is now first, while a game runs), Save as Zip... and what it last
// said, and Close (Enter or Escape). Nothing is sent from here. Layout and
// bindings only, over the help view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ChimeraDialogs

ApplicationWindow {
    id: win
    objectName: "reportIssueWindow"
    required property QtObject vm
    required property QtObject clipboard
    required property QtObject pictureFiles
    // the main window's Paths folders
    property var folderOf: null
    property bool gameRunning: false
    property Window mainWindow: null
    title: qsTrId("report_issue.report_an_issue")
    width: 680
    height: 560
    minimumWidth: 520
    minimumHeight: 460
    modality: Qt.platform.os === "osx" ? Qt.WindowModal : Qt.ApplicationModal // a sheet on a Mac, which keeps an application-modal window over other apps (#541)
    transientParent: win.mainWindow
    flags: Qt.Dialog | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMaximizeButtonHint | Qt.WindowCloseButtonHint

    readonly property string pictureRow: qsTrId("report_issue.a_picture_of_the_game_as")
    // the game as it was when the window opened, "" for none
    property string picturePath: ""
    // the rows not ticked, by their text
    property var unticked: ({})
    readonly property var rows: (win.picturePath !== "" ? [win.pictureRow] : []).concat(win.vm.reportFiles.split("\n").filter(s => s !== ""))
    onVisibleChanged: if (visible) {
        win.unticked = {}
        win.picturePath = win.gameRunning ? win.pictureFiles.savePng(win.pictureFiles.temporaryFile("chimera-report-game.png")) : ""
        win.vm.run("makeReport")
    }
    function saveZip(url) {
        const files = win.rows.filter(r => r !== win.pictureRow && !win.unticked[r])
        const picture = win.picturePath !== "" && !win.unticked[win.pictureRow] ? win.picturePath : ""
        win.vm.run("saveReport", [url, picture, files.join("\n")])
    }
    // the name Chimera suggests: chimera-report-yyyy-MM-dd-HHmm.zip
    function zipName() { return "chimera-report-" + Qt.formatDateTime(new Date(), "yyyy-MM-dd-HHmm") + ".zip" }

    Shortcut { sequences: ["Escape", "Return", "Enter"]; onActivated: win.close() }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 4
        Label { text: qsTrId("report_issue.1_open_the_report_form_it"); Layout.fillWidth: true; elide: Text.ElideRight }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            TextField { text: win.vm.formUrl; readOnly: true; activeFocusOnTab: false; Layout.fillWidth: true }
            Button { text: qsTrId("report_issue.open_in_browser"); Layout.preferredWidth: 120; onClicked: Qt.openUrlExternally(win.vm.formUrl) }
            Button {
                text: qsTrId("report_issue.copy_link")
                Layout.preferredWidth: 120
                onClicked: { win.clipboard.setText(win.vm.formUrl); win.vm.status = qsTrId("report_issue.the_link_is_on_the_clipboard") }
            }
        }
        Label {
            text: qsTrId("report_issue.2_choose_bug_report_there_and")
            Layout.fillWidth: true
            Layout.topMargin: 10
            elide: Text.ElideRight
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            TextArea { objectName: "reportText"; text: win.vm.reportText; readOnly: true; activeFocusOnTab: false; wrapMode: TextEdit.Wrap }
        }
        Button {
            text: qsTrId("report_issue.copy_text")
            Layout.preferredWidth: 120
            Layout.topMargin: 2
            onClicked: { win.clipboard.setText(win.vm.reportText); win.vm.status = qsTrId("report_issue.the_text_is_on_the_clipboard") }
        }
        Label {
            text: qsTrId("report_issue.3_tick_what_belongs_to_the")
            Layout.fillWidth: true
            Layout.topMargin: 4
            elide: Text.ElideRight
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 100
            color: palette.base
            border.color: palette.mid
            ListView {
                boundsBehavior: Flickable.StopAtBounds
                id: files
                objectName: "reportFiles"
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                model: win.rows
                ScrollBar.vertical: ScrollBar {}
                delegate: CheckBox {
                    required property string modelData
                    text: modelData
                    width: ListView.view.width
                    padding: 1
                    checked: !win.unticked[modelData]
                    onClicked: { const now = Object.assign({}, win.unticked); if (checked) delete now[modelData]; else now[modelData] = true; win.unticked = now }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 10
            Button {
                text: qsTrId("report_issue.save_as_zip")
                Layout.preferredWidth: 120
                // in Paths' Bug reports folder, made if it is not there yet (issue 435)
                onClicked: {
                    zipDialog.currentFolder = win.folderOf("Bug reports", true)
                    zipDialog.selectedFile = zipDialog.currentFolder + "/" + win.zipName()
                    zipDialog.open()
                }
            }
            Label { objectName: "reportStatus"; text: win.vm.status; Layout.fillWidth: true; elide: Text.ElideRight }
            Button { text: qsTrId("common.close"); Layout.preferredWidth: 90; onClicked: win.close() }
        }
    }
    FileDialog {
        id: zipDialog
        title: qsTrId("report_issue.save_the_report_s_files_as")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "zip"
        nameFilters: [qsTrId("report_issue.zip_archive_zip")]
        onAccepted: win.saveZip(selectedFile)
    }
}
