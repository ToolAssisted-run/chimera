// One of a core's own tools (Tools > Core-Specific Tools): what the tool
// does at the top; each file it reads (a list with Add..., Remove, Up and Down for one
// that takes several, else a box with Browse...); its options; each file it
// writes with Browse... to choose where; what the core said, or what keeps
// the tool from going in the error colour; Run and Close. All of it is drawn
// from the core's package: the window knows nothing of what a tool does. A
// dialog of the main window. Layout and bindings only, over the coreTool
// view model.
import QtQuick
import QtQuick.Controls
import ChimeraDialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "coreToolWindow"
    required property QtObject vm
    property Window mainWindow: null
    property var themeColor: (role, fallback) => fallback
    title: win.vm.title
    width: 600
    height: Math.max(260, content.implicitHeight + 24)
    minimumWidth: 600
    maximumWidth: 600
    modality: Qt.platform.os === "osx" ? Qt.WindowModal : Qt.ApplicationModal // a sheet on a Mac, which keeps an application-modal window over other apps (#541)
    transientParent: win.mainWindow
    flags: Qt.Dialog

    // what keeps the tool from going (a pick refused here)
    property string said: ""
    property string pickFor: ""
    onVisibleChanged: {
        // closed while it runs: the run is stopped
        if (!visible && win.vm.running) win.vm.run("cancel")
        if (visible) win.said = ""
    }
    Connections {
        target: win.vm
        // a change made by the view model: QQmlPropertyMap sends valueChanged only for one made from QML
        function onFailureChanged() { if (win.vm.failure !== "") win.said = "" }
    }
    function runNow() {
        win.said = ""
        win.vm.run("run")
        if (win.vm.error !== "" && win.vm.failure === "") win.said = win.vm.error
    }
    function addFiles(input, urls) {
        win.said = ""
        for (const u of urls) {
            win.vm.run("addFile", [input, u])
            if (win.vm.error !== "") win.said = win.vm.error
        }
    }

    // only while it is shown (issue 484, as Locate Project Files)
    Shortcut { sequences: ["Return", "Enter"]; enabled: win.visible && win.active && !win.vm.running; onActivated: win.runNow() }
    Shortcut { sequence: "Escape"; enabled: win.visible && win.active; onActivated: win.close() }

    FileDialog {
        id: fileDialog
        onAccepted: win.addFiles(win.pickFor, fileMode === FileDialog.OpenFiles ? selectedFiles : [selectedFile])
    }
    FileDialog {
        id: saveDialog
        fileMode: FileDialog.SaveFile
        onAccepted: { win.said = ""; win.vm.run("setOutput", [win.pickFor, selectedFile]) }
    }

    component RowName: Label { Layout.preferredWidth: 120; Layout.alignment: Qt.AlignTop; Layout.topMargin: 4; elide: Text.ElideRight }
    component Side: Button { Layout.preferredWidth: 76 }

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        Label {
            text: win.vm.description
            visible: text !== ""
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Repeater {
            model: win.vm.rows("inputs")
            delegate: RowLayout {
                id: input
                required property string name
                required property string label
                required property string filter
                required property bool multiple
                required property string files
                property int picked: -1
                readonly property var names: input.files === "" ? [] : input.files.split("\n")
                Layout.fillWidth: true
                RowName { text: input.label + ":" }
                // one that takes several: a list in the order they are read
                Rectangle {
                    visible: input.multiple
                    Layout.fillWidth: true
                    Layout.preferredHeight: 108
                    color: palette.base
                    border.color: palette.mid
                    ListView {
                        boundsBehavior: Flickable.StopAtBounds
                        id: inputList
                        anchors.fill: parent
                        anchors.margins: 1
                        clip: true
                        model: input.names
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            required property string modelData
                            required property int index
                            width: inputList.width
                            height: inputName.implicitHeight + 4
                            color: index === input.picked ? palette.highlight : "transparent"
                            Label {
                                id: inputName
                                anchors.verticalCenter: parent.verticalCenter
                                leftPadding: 3
                                text: parent.modelData
                                color: parent.index === input.picked ? palette.highlightedText : palette.text
                            }
                            TapHandler { onTapped: input.picked = parent.index }
                        }
                    }
                }
                TextField {
                    visible: !input.multiple
                    text: input.names.length > 0 ? input.names[0] : ""
                    readOnly: true
                    activeFocusOnTab: false
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                }
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    Side {
                        text: input.multiple ? qsTrId("core_tool.add") : qsTrId("common.browse")
                        onClicked: {
                            win.pickFor = input.name
                            fileDialog.title = input.label
                            fileDialog.fileMode = input.multiple ? FileDialog.OpenFiles : FileDialog.OpenFile
                            fileDialog.nameFilters = input.filter === "" || input.filter === "*" ? [qsTrId("common.all_files")]
                                : [input.label + " (" + input.filter + ")", qsTrId("common.all_files")]
                            fileDialog.open()
                        }
                    }
                    Side {
                        visible: input.multiple
                        text: qsTrId("common.remove")
                        onClicked: { if (input.picked >= 0) { win.vm.run("removeFile", [input.name, input.picked]); input.picked = -1 } }
                    }
                    Side {
                        visible: input.multiple
                        text: qsTrId("core_tool.up")
                        onClicked: { if (input.picked > 0) { win.vm.run("moveFile", [input.name, input.picked, input.picked - 1]); input.picked-- } }
                    }
                    Side {
                        visible: input.multiple
                        text: qsTrId("core_tool.down")
                        onClicked: {
                            if (input.picked >= 0 && input.picked < input.names.length - 1) {
                                win.vm.run("moveFile", [input.name, input.picked, input.picked + 1])
                                input.picked++
                            }
                        }
                    }
                }
            }
        }
        Repeater {
            model: win.vm.rows("options")
            delegate: CheckBox {
                required property string name
                required property string label
                required property string help
                required property bool on
                Layout.leftMargin: 124
                text: label
                checked: on
                padding: 2
                ToolTip.visible: hovered && help !== ""
                ToolTip.text: help
                onToggled: win.vm.run("setOption", [name, checked])
            }
        }
        Repeater {
            model: win.vm.rows("outputs")
            delegate: RowLayout {
                id: output
                required property string name
                required property string label
                required property string extension
                required property string path
                Layout.fillWidth: true
                RowName { text: output.label + ":" }
                TextField { text: output.path; readOnly: true; activeFocusOnTab: false; Layout.fillWidth: true }
                Side {
                    text: qsTrId("common.browse")
                    onClicked: {
                        win.pickFor = output.name
                        saveDialog.title = output.label
                        saveDialog.nameFilters = output.extension === "" ? [qsTrId("common.all_files")]
                            : [output.label + " (*." + output.extension + ")", qsTrId("common.all_files")]
                        if (output.path !== "") saveDialog.selectedFile = "file:///" + output.path.replace(/^\/+/, "")
                        saveDialog.open()
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            BusyIndicator { running: win.vm.running; visible: win.vm.running; Layout.preferredWidth: 28; Layout.preferredHeight: 28 }
            Label {
                objectName: "coreToolStatus"
                readonly property bool bad: win.said !== "" || win.vm.failure !== ""
                text: win.said !== "" ? win.said : win.vm.failure !== "" ? win.vm.failure
                    : win.vm.running ? qsTrId("core_tool.running") : win.vm.result
                color: bad ? win.themeColor("AccentError", "#c62828") : palette.windowText
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                verticalAlignment: Text.AlignTop
            }
        }
        RowLayout {
            Item { Layout.fillWidth: true }
            Button { text: qsTrId("core_tool.run"); Layout.preferredWidth: 80; enabled: !win.vm.running; onClicked: win.runNow() }
            Button {
                text: win.vm.running ? qsTrId("common.cancel") : qsTrId("common.close")
                Layout.preferredWidth: 76
                onClicked: { if (win.vm.running) win.vm.run("cancel"); else win.close() }
            }
        }
    }
}
