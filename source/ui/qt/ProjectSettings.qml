// Chimera's File > Project Settings (issue 370): the open project's settings,
// changed after it was made. The settings the New Project window shows for
// the project's files, the renderer among them, and when the machine's clock
// starts; not the machine, which goes with the project's files. Restart with
// these asks once more, then the machine starts again at frame 0 with them.
// Layout and bindings only, over the settings view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "projectSettingsWindow"
    required property QtObject vm
    property var themeColor: (role, fallback) => fallback
    property Window mainWindow: null
    title: qsTrId("project_settings.project_settings")
    width: 600
    height: 520
    minimumWidth: 600
    minimumHeight: 520
    modality: Qt.ApplicationModal
    transientParent: win.mainWindow
    // opened again: what the machine was made with, not what was left last time
    onVisibleChanged: if (visible) win.vm.run("reset")

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTrId("project_settings.a_change_restarts_the_machine_at")
        }
        SettingsList {
            vm: win.vm
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
        RowLayout {
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: win.vm.restartNote
                color: win.vm.clockWrong ? win.themeColor("AccentError", "#b00020") : palette.windowText
            }
            Button {
                text: qsTrId("project_settings.restart_with_these")
                enabled: win.vm.canRestart
                onClicked: restartQuestion.open()
            }
            Button { text: qsTrId("common.cancel"); onClicked: win.close() }
        }
    }
    Dialog {
        id: restartQuestion
        title: qsTrId("project_settings.project_settings")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            width: 420
            wrapMode: Text.Wrap
            text: qsTrId("project_settings.restart_the_machine_with_the_new")
        }
        onAccepted: { win.vm.run("restartWith"); win.close() }
    }
}
