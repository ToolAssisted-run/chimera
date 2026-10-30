// Chimera's Firmware window (Config > Firmware), laid out as Chimera's
// The Firmware window: what the window is for at the top, Show (All,
// Emulators, Games) above the list, the list grouped by core (the core's
// name and its line) with a mark and Firmware, Release, Expected SHA1,
// Found, Where and Status columns, what the line picked says in full under
// it, and Set File..., Clear, Scan Folder..., Rescan, Show every release
// and Close. A double click is Set File.... Layout and bindings only, over
// the firmware view model.
import QtQuick
import QtQuick.Controls
import ChimeraDialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "firmwareWindow"
    required property QtObject vm
    property var folderOf: null
    property var themeColor: (role, fallback) => fallback
    title: qsTrId("firmware_config.firmware")
    width: 860
    height: 440
    minimumWidth: 640
    minimumHeight: 300
    onVisibleChanged: if (visible) { win.vm.picked = -1; win.vm.run("refresh") }
    // a command that fills the list again, the list left where it was scrolled to, as Chimera's
    function keepingPlace(command, args) {
        const y = list.contentY
        win.vm.run(command, args || [])
        Qt.callLater(() => { list.contentY = Math.max(0, Math.min(y, list.contentHeight - list.height)) })
    }

    // the columns, as wide as Chimera's
    readonly property var widths: [26, 200, 150, 95, 150, 110, 105]

    // Chimera's marks: a tick, a warning triangle, a cross, a circle
    component Mark: Canvas {
        required property int mark
        width: 16; height: 16
        onMarkChanged: requestPaint()
        onPaint: {
            const g = getContext("2d")
            g.reset()
            g.lineCap = "round"
            if (mark === 0) {
                g.strokeStyle = win.themeColor("GlyphGood", "#2e7d32"); g.lineWidth = 2.4
                g.beginPath(); g.moveTo(3, 8); g.lineTo(6, 12); g.lineTo(13, 4); g.stroke()
            } else if (mark === 1) {
                g.fillStyle = win.themeColor("GlyphWarning", "#f0a000")
                g.beginPath(); g.moveTo(8, 1); g.lineTo(15, 14); g.lineTo(1, 14); g.closePath(); g.fill()
                g.fillStyle = win.themeColor("GlyphWarningInk", "#000000")
                g.fillRect(7, 5, 2, 5); g.fillRect(7, 11, 2, 2)
            } else if (mark === 2) {
                g.strokeStyle = win.themeColor("GlyphError", "#c62828"); g.lineWidth = 2.4
                g.beginPath(); g.moveTo(4, 4); g.lineTo(12, 12); g.moveTo(12, 4); g.lineTo(4, 12); g.stroke()
            } else {
                g.strokeStyle = win.themeColor("GlyphNeutral", "#9e9e9e"); g.lineWidth = 1.4
                g.beginPath(); g.ellipse(3, 3, 10, 10); g.stroke()
            }
        }
    }

    FileDialog {
        id: fileDialog
        currentFolder: win.folderOf ? win.folderOf("Firmware") : ""
        onAccepted: win.keepingPlace("setFile", [win.vm.picked, localFiles.localPath(selectedFile)])
    }
    FolderDialog {
        id: folderDialog
        title: qsTrId("firmware_config.scan_a_folder_for_firmware_files")
        currentFolder: win.folderOf ? win.folderOf("Firmware") : ""
        // in the dialog itself, as New Project's and Project Files' (docs/file-picker.md)
        checkBoxText: qsTrId("firmware_config.include_sub_folders")
        checkBoxChecked: true
        onAccepted: win.keepingPlace("scanFolder", [localFiles.localPath(selectedFolder), checkBoxChecked])
    }
    function browse() {
        if (!win.vm.canSet) return
        fileDialog.title = win.vm.pickTitle
        fileDialog.open()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4
        Label {
            text: qsTrId("firmware_config.files_your_installed_cores_expect_you")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        RowLayout {
            // Chimera's Find box (issue 368): the lines whose core, firmware, release, file asked for or file found holds it
            Label { text: qsTrId("firmware_config.find") }
            TextField {
                objectName: "firmwareFind"
                text: win.vm.find
                Layout.preferredWidth: 220
                onTextEdited: win.vm.find = text
            }
            Item { Layout.fillWidth: true }
            Label { text: qsTrId("firmware_config.show") }
            RadioButton { text: qsTrId("firmware_config.all"); checked: win.vm.shows === 0; onClicked: win.vm.shows = 0; padding: 2 }
            RadioButton { text: qsTrId("common.emulators"); checked: win.vm.shows === 1; onClicked: win.vm.shows = 1; padding: 2 }
            RadioButton { text: qsTrId("common.games"); checked: win.vm.shows === 2; onClicked: win.vm.shows = 2; padding: 2 }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: palette.base
            border.color: palette.mid
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 1
                spacing: 0
                // the column headings
                Row {
                    Repeater {
                        model: ["", qsTrId("firmware_config.firmware"), qsTrId("firmware_config.release"), qsTrId("firmware_config.expected_sha1"), qsTrId("firmware_config.found"), qsTrId("firmware_config.where"), qsTrId("common.status")]
                        delegate: Label {
                            required property int index
                            required property string modelData
                            text: modelData
                            width: win.widths[index]
                            leftPadding: 4
                            topPadding: 2; bottomPadding: 2
                            elide: Text.ElideRight
                            background: Rectangle { color: palette.button; border.color: palette.mid }
                        }
                    }
                }
                ListView {
                    id: list
                    objectName: "firmwareList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: win.vm.rows("lines")
                    currentIndex: win.vm.picked
                    focus: true
                    // + opens a folded core, - folds the picked line's core, as Chimera's list
                    Keys.onPressed: event => {
                        if (win.vm.picked < 0) return
                        if (event.key === Qt.Key_Plus) { win.vm.run("expand", [win.vm.picked]); event.accepted = true }
                        else if (event.key === Qt.Key_Minus) { win.vm.run("collapse", [win.vm.picked]); event.accepted = true }
                    }
                    ScrollBar.vertical: ScrollBar {}
                    // a group a core: its name and its line
                    section.property: "heading"
                    section.delegate: Label {
                        required property string section
                        width: list.width
                        text: section
                        font.bold: true
                        topPadding: 6
                        leftPadding: 4
                        color: palette.highlight
                    }
                    delegate: Rectangle {
                        id: line
                        required property int index
                        required property string core
                        required property string summary
                        required property string kind
                        required property int mark
                        required property string firmware
                        required property string release
                        required property string expected
                        required property string found
                        required property string where
                        required property string status
                        required property int state
                        width: list.width
                        height: 20
                        readonly property bool isPicked: line.index === win.vm.picked
                        color: isPicked ? palette.highlight : "transparent"
                        // Chimera's colours: right, used anyway, gone, nothing
                        readonly property color ink: line.isPicked ? palette.highlightedText
                            : line.kind !== "row" ? palette.mid
                            : line.state === 0 ? win.themeColor("AccentGood", "#2e7d32")
                            : line.state === 1 || line.state === 2 ? win.themeColor("AccentWarning", "#b26a00")
                            : line.state === 3 ? win.themeColor("AccentError", "#c62828") : palette.mid
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            Item { width: win.widths[0]; height: 16; Mark { anchors.centerIn: parent; mark: line.mark } }
                            Label { text: line.kind === "folded" ? line.firmware + "    " + line.release : line.firmware; color: line.ink; width: line.kind === "row" ? win.widths[1] : 800; elide: Text.ElideRight; leftPadding: 4 }
                            Label { visible: line.kind === "row"; text: line.release; color: line.ink; width: win.widths[2]; elide: Text.ElideRight; leftPadding: 4 }
                            Label { visible: line.kind === "row"; text: line.expected; color: line.ink; width: win.widths[3]; font.family: "monospace"; leftPadding: 4 }
                            Label { visible: line.kind === "row"; text: line.found; color: line.ink; width: win.widths[4]; elide: Text.ElideRight; leftPadding: 4 }
                            Label { visible: line.kind === "row"; text: line.where; color: line.ink; width: win.widths[5]; elide: Text.ElideRight; leftPadding: 4 }
                            Label { visible: line.kind === "row"; text: line.status; color: line.ink; width: win.widths[6] + 200; elide: Text.ElideRight; leftPadding: 4 }
                        }
                        TapHandler {
                            onTapped: { win.vm.picked = line.index; list.forceActiveFocus() }
                            // a folded core's line opens it; any other line chooses a file
                            onDoubleTapped: {
                                win.vm.picked = line.index
                                if (line.kind === "folded") win.vm.run("expand", [line.index])
                                else win.browse()
                            }
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: { win.vm.picked = line.index; foldMenu.popup() }
                        }
                    }
                }
            }
        }
        // the core's line for the group picked, and what the line picked says in full
        Label {
            text: win.vm.detail
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            elide: Text.ElideRight
            wrapMode: Text.NoWrap
        }
        RowLayout {
            Button { text: qsTrId("firmware_config.set_file"); enabled: win.vm.canSet; onClicked: win.browse() }
            Button { text: qsTrId("common.clear"); enabled: win.vm.canClear; onClicked: win.keepingPlace("clearFile", [win.vm.picked]) }
            Button { text: qsTrId("common.scan_folder"); onClicked: folderDialog.open() }
            Button { text: qsTrId("firmware_config.rescan"); onClicked: win.keepingPlace("refresh") }
            // the file found for the line picked, in its folder
            Button {
                text: qsTrId("common.open_folder")
                enabled: win.vm.canSet
                onClicked: {
                    const folder = win.vm.run("folderOf", [win.vm.picked]) || ""
                    if (folder !== "") Qt.openUrlExternally(localFiles.folderUrl(folder))
                }
            }
            CheckBox { text: qsTrId("firmware_config.show_every_release"); checked: win.vm.showAll; onToggled: win.vm.showAll = checked }
            Label { text: win.vm.status; elide: Text.ElideRight; Layout.fillWidth: true; opacity: 0.8 }
            Button { text: qsTrId("common.close"); onClicked: win.close() }
        }
    }
    // a core's files folded or opened, one core or all, as Chimera's right-click menu
    Menu {
        delegate: MenuEntry {}
        id: foldMenu
        Action { text: qsTrId("firmware_config.collapse"); onTriggered: win.vm.run("collapse", [win.vm.picked]) }
        Action { text: qsTrId("firmware_config.expand"); onTriggered: win.vm.run("expand", [win.vm.picked]) }
        Action { text: qsTrId("firmware_config.collapse_all"); onTriggered: win.vm.run("collapseAll") }
        Action { text: qsTrId("firmware_config.expand_all"); onTriggered: win.vm.run("expandAll") }
    }
}
