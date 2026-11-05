// Chimera's Undo History (TAStudio's Edit menu), laid out as its form: a
// roll of the steps, ID and Undo Step, the last done in the greenzone's
// colour and those taken back in the lag colour, as TAStudio's own colours
// have them; a step picked, then double-clicked, is undone to, and the
// right button offers Undo To Selection, Redo To Selection and Clear
// History To Selection. Under it Undo, Redo, Auto Scroll (the last done
// kept in view), Max: and Clear. Layout and bindings only, over the
// pianoRoll view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "undoHistoryWindow"
    required property QtObject vm
    // TAStudio's colour for a role: its own, else the theme's
    property var rollColor: (role) => ""
    title: qsTrId("undo_history.undo_history")
    width: 460
    height: 300
    minimumWidth: 460
    minimumHeight: 200
    property int picked: -1
    // the last step done (-1 for none): the steps after it are taken back
    property int undoIndex: -1
    readonly property var widths: [44, 300]

    // where the history stands, from the steps done
    function countDone() {
        let last = -1
        for (let i = 0; i < stepRows.count; i++) {
            const o = stepRows.objectAt(i)
            if (o && o.done) last = i
        }
        if (last !== win.undoIndex) {
            win.undoIndex = last
            // Auto Scroll: the last done in view, the one before it picked
            if (autoScroll.checked) {
                steps.positionViewAtIndex(Math.max(last, 0), ListView.Contain)
                win.picked = last - 1
            }
        }
    }
    Instantiator {
        id: stepRows
        model: win.vm.rows("history")
        delegate: QtObject { required property bool done }
        onObjectAdded: Qt.callLater(win.countDone)
        onObjectRemoved: Qt.callLater(win.countDone)
    }

    Menu {
        delegate: MenuEntry {}
        id: stepMenu
        MenuItem { text: qsTrId("undo_history.undo_to_selection"); enabled: win.picked >= 0; onTriggered: win.vm.run("undoTo", [win.picked]) }
        MenuItem { text: qsTrId("undo_history.redo_to_selection"); enabled: win.picked >= 0; onTriggered: win.vm.run("redoTo", [win.picked]) }
        MenuItem { text: qsTrId("undo_history.clear_history_to_selection"); enabled: win.picked >= 0; onTriggered: win.vm.run("clearHistoryTo", [win.picked]) }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6
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
                        model: [qsTrId("undo_history.id"), qsTrId("undo_history.undo_step")]
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
                    boundsBehavior: Flickable.StopAtBounds
                    id: steps
                    objectName: "historyList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: win.vm.rows("history")
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: step
                        required property int index
                        required property string name
                        required property bool done
                        width: steps.width
                        height: 20
                        readonly property bool isPicked: step.index === win.picked
                        // as Chimera colours them: the last done green, those taken back red
                        readonly property string role: step.index === win.undoIndex ? "TasGreenZoneInput" : step.index > win.undoIndex ? "TasLagZoneInput" : ""
                        color: step.isPicked ? palette.highlight : step.role !== "" ? (win.rollColor(step.role) || "transparent") : "transparent"
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            Label { text: step.index; width: win.widths[0]; leftPadding: 4; color: step.isPicked ? palette.highlightedText : palette.text }
                            Label { text: step.name; width: win.widths[1]; leftPadding: 4; elide: Text.ElideRight; color: step.isPicked ? palette.highlightedText : palette.text }
                        }
                        TapHandler {
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onTapped: (point, button) => {
                                win.picked = step.index
                                if (button === Qt.RightButton) stepMenu.popup()
                            }
                            onDoubleTapped: (point, button) => { if (button === Qt.LeftButton) win.vm.run("undoTo", [step.index]) }
                        }
                    }
                }
            }
        }
        RowLayout {
            Button { text: qsTrId("undo_history.undo"); Layout.preferredWidth: 60; onClicked: win.vm.run("undo") }
            Button { text: qsTrId("undo_history.redo"); Layout.preferredWidth: 60; onClicked: win.vm.run("redo") }
            CheckBox { id: autoScroll; text: qsTrId("undo_history.auto_scroll"); checked: true; padding: 2 }
            Item { Layout.fillWidth: true }
            Label { text: qsTrId("undo_history.max") }
            SpinBox {
                from: 1
                to: 2147483647
                editable: true
                Layout.preferredWidth: 90
                value: win.vm.maxUndoSteps
                onValueModified: win.vm.maxUndoSteps = value
            }
            Button { text: qsTrId("common.clear"); Layout.preferredWidth: 60; onClicked: { win.vm.run("clearHistory"); win.picked = -1 } }
        }
    }
}
