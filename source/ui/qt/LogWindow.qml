// The log, as Chimera's Log Window: the lines, picked one or several at a
// time (Ctrl, Shift), Copy Sel. (Ctrl+C) and Copy All, Clear and Close; the
// Settings menu Chimera adds to a tool with no menu of its own. Layout and
// bindings only, over the list of every message the window has shown
// (Chimera's shows its console's).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "logWindow"
    required property ListModel log
    required property QtObject clipboard
    property QtObject customize: null
    property QtObject toolWindows: null
    property Window mainWindow: null
    title: qsTrId("log_window.log_window")
    width: 640
    height: 400
    minimumWidth: 440
    minimumHeight: 220

    WindowOptions { id: windowOptions; window: win; tool: "logWindow"; vm: win.toolWindows; mainWindow: win.mainWindow }
    onVisibleChanged: if (visible) windowOptions.placeBack(false)

    // the lines picked, by place, and the one a Shift-click counts from
    property var picked: ({})
    property int anchor: -1
    readonly property int pickedCount: Object.keys(win.picked).length
    function lineText(i) { return win.log.get(i).time + " " + win.log.get(i).message }
    function pick(i, mode) {
        const now = mode === "only" ? {} : Object.assign({}, win.picked)
        if (mode === "toggle") { if (now[i]) delete now[i]; else now[i] = true; win.anchor = i }
        else if (mode === "range" && win.anchor >= 0) { for (let k = Math.min(win.anchor, i); k <= Math.max(win.anchor, i); k++) now[k] = true }
        else { now[i] = true; win.anchor = i }
        win.picked = now
    }
    // Chimera's Copy Sel.: the lines picked, a line each, trimmed
    function copyPicked() {
        const rows = Object.keys(win.picked).map(Number).sort((a, b) => a - b)
        if (rows.length === 0) return
        const text = rows.map(win.lineText).join("\n").trim()
        if (text !== "") win.clipboard.setText(text)
    }

    menuBar: MenuBar {
        Menu {
            delegate: MenuEntry {}
            title: qsTrId("common.settings")
            MenuItem { text: qsTrId("common.save_window_position"); checkable: true; checked: windowOptions.savePosition; onTriggered: windowOptions.set("save", checked) }
            MenuItem {
                text: qsTrId("common.stay_on_top"); checkable: true; checked: windowOptions.onTop
                visible: windowOptions.canStayOnTop; height: visible ? implicitHeight : 0
                onTriggered: windowOptions.set("top", checked)
            }
            MenuItem { text: qsTrId("common.float_from_main_window"); checkable: true; checked: windowOptions.floating; onTriggered: windowOptions.set("float", checked) }
            MenuItem { text: qsTrId("common.restore_defaults_mnemonic"); onTriggered: windowOptions.restore() }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        ListView {
            boundsBehavior: Flickable.StopAtBounds
            id: list
            objectName: "logLines"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            model: win.log
            ScrollBar.vertical: ScrollBar {}
            onCountChanged: positionViewAtEnd()
            Keys.onPressed: (event) => { if (event.matches(StandardKey.Copy)) { win.copyPicked(); event.accepted = true } }
            delegate: Rectangle {
                id: line
                required property int index
                required property string time
                required property string message
                readonly property bool isPicked: win.picked[line.index] === true
                width: list.width - 16
                height: lineLabel.implicitHeight + 4
                color: line.isPicked ? palette.highlight : "transparent"
                Label {
                    id: lineLabel
                    anchors.verticalCenter: parent.verticalCenter
                    x: 3
                    text: line.time + "  " + line.message
                    font.family: "monospace"
                    elide: Text.ElideRight
                    width: parent.width - 6
                    color: line.isPicked ? palette.highlightedText : palette.text
                }
                MouseArea {
                    anchors.fill: parent
                    onPressed: (mouse) => {
                        list.forceActiveFocus()
                        win.pick(line.index, (mouse.modifiers & Qt.ShiftModifier) ? "range" : (mouse.modifiers & Qt.ControlModifier) ? "toggle" : "only")
                    }
                }
            }
        }
        RowLayout {
            Button { text: qsTrId("log_window.clear"); onClicked: { win.log.clear(); win.picked = {}; win.anchor = -1 } }
            Button { text: qsTrId("log_window.copy_sel"); enabled: win.pickedCount > 0; onClicked: win.copyPicked() }
            Button {
                text: qsTrId("log_window.copy_all"); enabled: win.log.count > 0
                onClicked: {
                    let text = ""
                    for (let i = 0; i < win.log.count; i++) text += win.lineText(i) + "\n"
                    win.clipboard.setText(text)
                }
            }
            Item { Layout.fillWidth: true }
            Button { text: qsTrId("common.close"); onClicked: win.close() }
        }
    }
}
