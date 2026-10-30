// Chimera's movie import (File > the core's Import menu item): what it does
// at the top; the movie with Browse...;
// each file the import reads (a list with Add..., Remove, Up and Down for
// one that takes several, else a box with Browse...); the import's options;
// what keeps it from going, in the error colour; Import and Cancel. Taken,
// it gives way to the New Project wizard on the core's answers. A dialog
// of the main window, as Chimera's. Layout and bindings only, over the
// newProject view model.
import QtQuick
import QtQuick.Controls
import ChimeraDialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "movieImportWindow"
    required property QtObject vm
    property Window mainWindow: null
    property var themeColor: (role, fallback) => fallback
    // the import was taken: the wizard is next
    signal taken()
    title: win.vm.importMenu === "" ? qsTrId("movie_import.import_movie") : win.vm.importMenu.replace(/[.\s]+$/, "").replace("&", "")
    width: 600
    height: Math.max(260, content.implicitHeight + 24)
    minimumWidth: 600
    maximumWidth: 600
    modality: Qt.ApplicationModal
    transientParent: win.mainWindow
    flags: Qt.Dialog

    // what keeps the import from going, and whether the core is being asked
    property string said: ""
    property bool asking: false
    property string pickFor: ""
    onVisibleChanged: {
        // closed while the core is asked: its answer is not taken
        if (!visible && win.asking) win.vm.run("importCancel")
        if (visible) { win.said = ""; win.asking = false }
    }
    Connections {
        target: win.vm
        // a change made by the view model: QQmlPropertyMap sends valueChanged only for one made from QML
        function onImportFailureChanged() {
            if (win.visible && win.vm.importFailure !== "") { win.said = win.vm.importFailure; win.asking = false }
        }
        function onImportsTakenChanged() {
            if (win.visible) { win.asking = false; win.close(); win.taken() }
        }
    }
    function importNow() {
        win.said = ""
        win.asking = true
        win.vm.run("importRun")
        if (win.vm.error !== "") { win.said = win.vm.error; win.asking = false }
    }
    function addFiles(option, urls) {
        win.said = ""
        for (const u of urls) {
            win.vm.run("importAddFile", [option, u])
            if (win.vm.error !== "") win.said = win.vm.error
        }
    }

    Shortcut { sequences: ["Return", "Enter"]; enabled: win.active && !win.asking; onActivated: win.importNow() }
    Shortcut { sequence: "Escape"; enabled: win.active; onActivated: win.close() }

    FileDialog {
        id: movieDialog
        title: win.vm.importMovieLabel
        nameFilters: [win.vm.importMovieLabel + " (" + win.vm.importMovieFilter + ")", qsTrId("common.all_files")]
        onAccepted: { win.said = ""; win.vm.run("importSetMovie", [selectedFile]) }
    }
    FileDialog {
        id: fileDialog
        onAccepted: win.addFiles(win.pickFor, fileMode === FileDialog.OpenFiles ? selectedFiles : [selectedFile])
    }

    component RowName: Label { Layout.preferredWidth: 120; Layout.alignment: Qt.AlignTop; Layout.topMargin: 4; elide: Text.ElideRight }
    component Side: Button { Layout.preferredWidth: 76 }

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        Label {
            text: qsTrId("movie_import.the_core_reads_these_files_and")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            RowName { text: (win.vm.importMovieLabel !== "" ? win.vm.importMovieLabel : qsTrId("movie_import.movie")) + ":" }
            TextField { text: win.vm.importMovie; readOnly: true; activeFocusOnTab: false; Layout.fillWidth: true }
            Side { text: qsTrId("common.browse"); onClicked: movieDialog.open() }
        }
        Repeater {
            model: win.vm.rows("importFiles")
            delegate: RowLayout {
                id: input
                required property string option
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
                        text: input.multiple ? qsTrId("movie_import.add") : qsTrId("common.browse")
                        onClicked: {
                            win.pickFor = input.option
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
                        onClicked: { if (input.picked >= 0) { win.vm.run("importRemoveFile", [input.option, input.picked]); input.picked = -1 } }
                    }
                    Side {
                        visible: input.multiple
                        text: qsTrId("movie_import.up")
                        onClicked: { if (input.picked > 0) { win.vm.run("importMoveFile", [input.option, input.picked, input.picked - 1]); input.picked-- } }
                    }
                    Side {
                        visible: input.multiple
                        text: qsTrId("movie_import.down")
                        onClicked: {
                            if (input.picked >= 0 && input.picked < input.names.length - 1) {
                                win.vm.run("importMoveFile", [input.option, input.picked, input.picked + 1])
                                input.picked++
                            }
                        }
                    }
                }
            }
        }
        // the importer's switches: only a ticked one is sent
        Repeater {
            model: win.vm.rows("importOptions")
            delegate: CheckBox {
                required property string name
                required property string label
                required property bool on
                Layout.leftMargin: 124
                text: label
                checked: on
                padding: 2
                onToggled: win.vm.run("importSetOption", [name, checked])
            }
        }
        Label {
            objectName: "importStatus"
            text: win.said
            color: win.themeColor("AccentError", "#c62828")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            verticalAlignment: Text.AlignTop
        }
        RowLayout {
            Item { Layout.fillWidth: true }
            Button { text: qsTrId("movie_import.import"); Layout.preferredWidth: 80; enabled: !win.asking; onClicked: win.importNow() }
            Button { text: qsTrId("common.cancel"); Layout.preferredWidth: 76; onClicked: win.close() }
        }
    }
}
