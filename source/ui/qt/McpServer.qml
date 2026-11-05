// Chimera's Tools > MCP Server... (docs/mcp-server.md): where a person starts
// and stops the server that lets their AI agent work on the open project,
// allows it to make changes, and reads what it did. The server is the
// engine's; this window only shows it. It stays open beside the main window,
// and closing it does not stop the server. Layout and bindings only, over the
// mcp view model.
import QtQuick
import QtQuick.Controls
import ChimeraDialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "mcpServerWindow"
    required property QtObject vm
    property QtObject clipboard: null
    property Window mainWindow: null
    title: qsTrId("mcp_server.mcp_server")
    width: 620
    height: 480
    minimumWidth: 620
    minimumHeight: 480
    transientParent: win.mainWindow
    // brought up to date with the engine four times a second while it is open
    Timer { interval: 250; running: win.visible; repeat: true; onTriggered: win.vm.run("refresh") }
    onVisibleChanged: if (visible) win.vm.run("refresh")

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 6
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTrId("mcp_server.an_ai_agent_can_look_at")
        }
        RowLayout {
            Label { objectName: "mcpState"; text: win.vm.stateText; elide: Text.ElideRight; Layout.fillWidth: true }
            Button {
                objectName: "mcpStartStop"
                text: win.vm.started ? qsTrId("common.stop") : qsTrId("mcp_server.start")
                Layout.preferredWidth: 110
                onClicked: win.vm.run("toggle")
            }
        }
        CheckBox {
            text: qsTrId("mcp_server.allow_the_agent_to_make_changes")
            checked: win.vm.allowedNow
            // the person's choice is remembered for every later start (user-decided, 2026-10-22, issue 355)
            onToggled: win.vm.allowChanges = checked
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 18
            wrapMode: Text.Wrap
            opacity: 0.8
            text: qsTrId("mcp_server.remembered_and_applied_every_time_the")
        }
        Label {
            Layout.fillWidth: true
            Layout.topMargin: 6
            wrapMode: Text.Wrap
            text: qsTrId("mcp_server.to_set_up_an_agent_once")
        }
        RowLayout {
            TextField { text: win.vm.setupCommand; readOnly: true; Layout.fillWidth: true; selectByMouse: true }
            Button {
                text: qsTrId("common.copy")
                Layout.preferredWidth: 110
                onClicked: {
                    if (win.clipboard) { win.clipboard.setText(win.vm.setupCommand); win.vm.note = qsTrId("mcp_server.the_command_is_on_the_clipboard") }
                }
            }
        }
        Label { Layout.topMargin: 6; text: qsTrId("mcp_server.what_happened_one_line_a_call") }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            TextArea {
                objectName: "mcpLog"
                text: win.vm.log
                readOnly: true
                wrapMode: TextEdit.NoWrap
                font.family: "monospace"
                // the newest line in view
                onTextChanged: cursorPosition = length
            }
        }
        RowLayout {
            Button { text: qsTrId("mcp_server.save_log"); onClicked: saveLogDialog.open() }
            Label { text: win.vm.note !== "" ? win.vm.note : qsTrId("mcp_server.closing_this_window_does_not_stop"); elide: Text.ElideRight; Layout.fillWidth: true }
            Button { text: qsTrId("common.close"); Layout.preferredWidth: 110; onClicked: win.close() }
        }
    }
    FileDialog {
        id: saveLogDialog
        title: qsTrId("mcp_server.save_the_mcp_server_s_log")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTrId("mcp_server.text_file_txt")]
        defaultSuffix: "txt"
        selectedFile: "file:///chimera-mcp-log-" + Qt.formatDateTime(new Date(), "yyyy-MM-dd-HHmm") + ".txt"
        onAccepted: win.vm.run("saveLog", [localFiles.localPath(selectedFile)])
    }
}
