// A package's settings, one row each, as it declares them, laid out as
// Chimera's settings grid (issue 417): the name and the value side by side, a
// line between rows, and what the row chosen does in a box under the grid, so
// many settings are seen at once. The control follows the declared type.
// Layout and bindings only, over any view model with "settings" rows and a
// "set" command (Settings, New Project).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property QtObject vm
    // the presets row at the top; a window that places its own leaves it out
    property bool presetsHere: true
    // the clock's row is the one chosen, not a setting's
    property bool clockChosen: false
    // the box under the grid: the row chosen, its name and what it does, as Chimera's
    readonly property string described: root.clockChosen
        ? qsTrId("settings_list.the_machine_s_clock_starts_at") + "\n\n" + qsTrId("settings_list.every_run_of_this_project_starts")
        : list.currentItem !== null ? list.currentItem.describes : ""
    spacing: 6

    FontMetrics { id: metrics }

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        color: palette.base
        border.color: palette.mid
        ListView {
            id: list
            anchors.fill: parent
            anchors.margins: 1
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.vm.rows("settings")
            // the row's own colour shows which is chosen; the list does not scroll to it
            highlightFollowsCurrentItem: false
            ScrollBar.vertical: ScrollBar { id: bar }
            // the rows' width: the scroll bar's room only while it is there
            readonly property real rowWidth: width - (bar.size < 1 ? bar.width : 0)
            // the first row chosen, as Chimera's grid chooses it, so the box says something
            onCountChanged: if (currentIndex < 0 && count > 0) currentIndex = 0
            // the presets the machine is offered: Apply writes one's values below
            header: RowLayout {
                width: list.rowWidth
                visible: root.presetsHere && presetBox.count > 0
                height: visible ? implicitHeight + 8 : 0
                Label { text: qsTrId("common.preset"); Layout.leftMargin: 6 }
                ComboBox {
                    id: presetBox
                    model: root.vm.rows("presets")
                    textRole: "label"
                    valueRole: "id"
                    Layout.fillWidth: true
                    // the list is given again after every edit: the choice stays
                    property string chosen: ""
                    onActivated: chosen = currentValue
                    Connections {
                        target: presetBox.model
                        function onModelReset() { Qt.callLater(presetBox.keep) }
                    }
                    function keep() {
                        const at = chosen === "" ? -1 : indexOfValue(chosen)
                        currentIndex = at >= 0 ? at : (count > 0 ? 0 : -1)
                    }
                    Component.onCompleted: keep()
                }
                Button { text: qsTrId("common.apply"); enabled: presetBox.currentIndex >= 0; onClicked: root.vm.run("applyPreset", [presetBox.currentValue]) }
            }
            // when the machine's clock starts: the project's, for every core, so the last row
            // and scrolled with the settings like any other (owner, 2026-10-27), where the view
            // model has one (New Project, Project Settings)
            footer: Item {
                width: list.rowWidth
                visible: root.vm.clockStart !== undefined
                height: visible ? clockLine.implicitHeight + 1 : 0
                RowLayout {
                    id: clockLine
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: 0
                    NameCell {
                        Layout.preferredWidth: Math.round(parent.width * 0.45)
                        text: qsTrId("settings_list.the_machine_s_clock_starts_at")
                        chosen: root.clockChosen
                        onPicked: root.clockChosen = true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 4
                        Layout.rightMargin: 4
                        TextField {
                            objectName: "clockStart"
                            text: root.vm.clockStart === undefined ? "" : root.vm.clockStart
                            placeholderText: "2000-01-01 00:00:00"
                            onTextEdited: root.vm.clockStart = text
                            onActiveFocusChanged: if (activeFocus) root.clockChosen = true
                            Layout.fillWidth: true
                        }
                        Label { text: qsTrId("settings_list.utc") }
                    }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: palette.mid }
            }
            delegate: Item {
                id: row
                required property int index
                required property string name
                required property string label
                required property string type
                required property var value
                required property string options
                required property string optionLabels
                required property var min
                required property var max
                required property string description
                required property bool edited
                readonly property bool chosen: ListView.isCurrentItem && !root.clockChosen
                // what the box under the grid shows for it: its name, then what it does
                readonly property string describes: row.label + (row.description !== "" ? "\n\n" + row.description : "")
                function choose() {
                    list.currentIndex = row.index
                    root.clockChosen = false
                }
                width: list.rowWidth
                height: line.implicitHeight + 1

                RowLayout {
                    id: line
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: 0
                    NameCell {
                        Layout.preferredWidth: Math.round(row.width * 0.45)
                        text: row.label + (row.edited ? " *" : "")
                        chosen: row.chosen
                        onPicked: row.choose()
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 4
                        Layout.rightMargin: 4
                        CheckBox {
                            visible: row.type === "bool"
                            checked: row.value === true
                            onToggled: { row.choose(); root.vm.run("set", [row.name, checked]) }
                            onActiveFocusChanged: if (activeFocus) row.choose()
                            Layout.fillWidth: true
                        }
                        ComboBox {
                            visible: row.type === "enum"
                            model: row.optionLabels.split("\n")
                            currentIndex: row.options.split("\n").indexOf(String(row.value))
                            onActivated: (i) => { row.choose(); root.vm.run("set", [row.name, row.options.split("\n")[i]]) }
                            onActiveFocusChanged: if (activeFocus) row.choose()
                            Layout.fillWidth: true
                        }
                        SpinBox {
                            visible: row.type === "int"
                            editable: true
                            from: row.min === undefined ? -2147483647 : row.min
                            to: row.max === undefined ? 2147483647 : row.max
                            value: row.type === "int" ? row.value : 0
                            onValueModified: root.vm.run("set", [row.name, value])
                            onActiveFocusChanged: if (activeFocus) row.choose()
                            Layout.fillWidth: true
                        }
                        // Chimera's uint, long and ulong as their digits (the window's numbers would lose a
                        // ulong's last ones), and a date and time as it is shown, in UTC (issue 358)
                        TextField {
                            visible: row.type === "uint" || row.type === "long" || row.type === "ulong" || row.type === "datetime"
                            text: row.value === undefined ? "" : String(row.value)
                            placeholderText: row.type === "datetime" ? "2000-01-01 00:00:00" : ""
                            validator: RegularExpressionValidator {
                                regularExpression: row.type === "datetime" ? /[0-9: -]*/ : row.type === "long" ? /-?[0-9]*/ : /[0-9]*/
                            }
                            onEditingFinished: root.vm.run("set", [row.name, text])
                            onActiveFocusChanged: if (activeFocus) row.choose()
                            Layout.fillWidth: true
                        }
                        Label { visible: row.type === "datetime"; text: qsTrId("settings_list.utc") }
                        TextField {
                            visible: row.type === "string" || row.type === "float"
                            text: row.value === undefined ? "" : String(row.value)
                            onEditingFinished: root.vm.run("set", [row.name, row.type === "float" ? Number(text) : text])
                            onActiveFocusChanged: if (activeFocus) row.choose()
                            Layout.fillWidth: true
                        }
                    }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: palette.mid }
            }
        }
    }

    // what the row chosen does, five lines tall and scrolled when longer, as Chimera's
    ScrollView {
        Layout.fillWidth: true
        Layout.preferredHeight: metrics.lineSpacing * 5 + 12
        TextArea {
            objectName: "settingDescription"
            text: root.described
            readOnly: true
            wrapMode: TextEdit.Wrap
            activeFocusOnTab: false
            background: Rectangle { color: palette.base; border.color: palette.mid }
        }
    }

    // a row's name, its half of the grid: chosen by a click, coloured while it is
    component NameCell: Rectangle {
        id: cell
        property alias text: name.text
        property bool chosen: false
        signal picked()
        Layout.fillHeight: true
        implicitHeight: name.implicitHeight + 10
        color: cell.chosen ? palette.highlight : "transparent"
        Label {
            id: name
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 4
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            color: cell.chosen ? palette.highlightedText : palette.text
        }
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: palette.mid }
        MouseArea { anchors.fill: parent; onClicked: cell.picked() }
    }
}
