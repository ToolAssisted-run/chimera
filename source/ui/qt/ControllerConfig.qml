// Chimera's Controller Config: Normal Controls, Autofire Controls and Analog
// Controls in tabs (inside each, a tab a player when there is more than
// one), the picture of the controller the package ships beside them, and
// along the bottom the tips, Auto Tab, Misc... (Load Defaults, Clear), Save
// and Cancel. The window works on a copy: Save keeps it; Cancel, or closing
// the window, lets it go. A box takes the next key or gamepad input pressed
// while it has the focus; its arrow gives the bindings a key cannot
// (Escape, the pointer's buttons). The box at a control's left shows it as
// a column in TAStudio, and the row under the controls shows or hides a
// tab's at once (issue 391: this took the place of TAStudio's Columns menu).
// Layout and bindings only, over the controls view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "controllerConfigWindow"
    required property QtObject controls
    required property QtObject keyNamer
    required property QtObject session
    property Window mainWindow: null
    // the window that opened it, when not the main one (TAStudio's Columns), for it to sit over
    property Window over: null
    property var themeColor: (role, fallback) => fallback
    title: win.controls.title
    width: 932
    height: 572
    minimumWidth: 932
    minimumHeight: 572
    modality: Qt.ApplicationModal
    transientParent: win.over !== null ? win.over : win.mainWindow

    readonly property var buttonGroups: JSON.parse(win.controls.buttonGroups)
    readonly property var axisGroups: JSON.parse(win.controls.axisGroups)
    // the analog control catching an axis (-1: none)
    property int axisCatch: -1

    onVisibleChanged: {
        if (visible) {
            win.controls.run("edit")
            kinds.currentIndex = 0
        } else {
            win.stopCatching()
            if (win.controls.editing) win.controls.run("cancel")
            win.over = null
        }
    }
    function stopCatching() {
        win.axisCatch = -1
        win.session.run("stopCatchPad")
    }
    function save() {
        win.controls.run("save")
        win.close()
    }

    // a gamepad axis moved, for the analog control catching one
    Connections {
        target: win.session
        enabled: win.axisCatch >= 0
        function onCaughtPadChanged() {
            if (win.session.caughtPad === "") return
            win.controls.run("caught", [win.axisCatch, win.session.caughtPad])
            win.axisCatch = -1
        }
    }
    // the pointer moved far enough over the window while an axis is caught: the pointer's axis, as Chimera's
    property point lastPointer: Qt.point(-1, -1)
    property real pointerMovedX: 0
    property real pointerMovedY: 0
    HoverHandler {
        onPointChanged: {
            const p = point.position
            if (win.axisCatch >= 0 && win.lastPointer.x >= 0) {
                win.pointerMovedX += 50 * Math.abs(p.x - win.lastPointer.x)
                win.pointerMovedY += 50 * Math.abs(p.y - win.lastPointer.y)
                const caught = win.pointerMovedX >= 20000 ? "WMouse X" : win.pointerMovedY >= 20000 ? "WMouse Y" : ""
                if (caught !== "") {
                    // the pointer's axis again: where it is, or how far it moves, as Chimera's toggles them
                    win.controls.run("caught", [win.axisCatch, caught])
                    win.stopCatching()
                }
            }
            win.lastPointer = p
        }
    }
    onAxisCatchChanged: { win.pointerMovedX = 0; win.pointerMovedY = 0 }

    // one player's buttons (or every button): boxes down a column, a label to the right of each, more columns as needed
    component KeyPanel: ScrollView {
        id: panel
        required property string table
        required property string group
        property bool everyGroup: false
        clip: true
        contentWidth: flow.width
        contentHeight: availableHeight
        readonly property var model: win.controls.rows(panel.table === "autofire" ? "autofire" : "buttons")
        // Chimera's panel: the label as wide as the longest name, 9 pixels a character
        property int labelWidth: 0
        function shownHere(group) { return panel.everyGroup || group === panel.group }
        function focusAfter(at) {
            const n = boxes.count
            for (let k = 1; k <= n; k++) {
                const item = boxes.itemAt((at + k) % n)
                if (item && item.visible) { item.focusField(); return }
            }
        }
        Flow {
            id: flow
            flow: Flow.TopToBottom
            height: Math.max(23, panel.availableHeight - 39)
            spacing: 1
            Repeater {
                id: boxes
                model: panel.model
                delegate: Row {
                    id: entry
                    required property int index
                    required property string shown
                    required property string group
                    required property string keys
                    required property string button
                    required property bool column
                    visible: panel.shownHere(entry.group)
                    spacing: 5
                    function focusField() { entryBox.focusField() }
                    Component.onCompleted: if (visible) panel.labelWidth = Math.max(panel.labelWidth, entry.shown.length * 9)
                    // a column in TAStudio (the buttons' tab only: autofire is the same buttons)
                    CheckBox {
                        visible: panel.table === "normal"
                        padding: 0
                        topPadding: 2
                        checked: entry.column
                        focusPolicy: Qt.NoFocus
                        onClicked: win.controls.run("showColumn", [entry.button, checked])
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: qsTrId("controller_config.a_column_in_tastudio")
                    }
                    BindingBox {
                        id: entryBox
                        width: 170
                        keys: entry.keys
                        session: win.session
                        keyNamer: win.keyNamer
                        themeColor: win.themeColor
                        press: (name) => win.controls.run("press", [panel.table, entry.index, name])
                        special: (name) => win.controls.run("special", [panel.table, entry.index, name])
                        next: () => panel.focusAfter(entry.index)
                    }
                    Label {
                        text: entry.shown
                        width: panel.labelWidth + 5
                        topPadding: 3
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    // one player's analog controls: a box each, one under another
    component AnalogPanel: ScrollView {
        id: apanel
        required property string group
        property bool everyGroup: false
        clip: true
        ColumnLayout {
            spacing: 4
            x: 4
            Repeater {
                model: win.controls.rows("analog")
                delegate: GridLayout {
                    id: bind
                    required property int index
                    required property string axis
                    required property string group
                    required property string value
                    required property double mult
                    required property double deadzone
                    required property string positive
                    required property string negative
                    required property bool column
                    visible: apanel.everyGroup || bind.group === apanel.group
                    columns: 5
                    columnSpacing: 6
                    rowSpacing: 4
                    Layout.preferredWidth: 474
                    // the axis bound, the control's name, Bind! / Unbind! / Flip Axis
                    TextField {
                        text: bind.value; readOnly: true; padding: 2
                        Layout.preferredWidth: 100; Layout.preferredHeight: 23
                        onTextChanged: cursorPosition = 0
                        Component.onCompleted: cursorPosition = 0
                    }
                    // the box: a column in TAStudio
                    RowLayout {
                        spacing: 2
                        Layout.preferredWidth: 112
                        CheckBox {
                            padding: 0
                            checked: bind.column
                            focusPolicy: Qt.NoFocus
                            onClicked: win.controls.run("showColumn", [bind.axis, checked])
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            ToolTip.text: qsTrId("controller_config.a_column_in_tastudio")
                        }
                        Label { text: bind.axis; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                    Button {
                        text: win.axisCatch === bind.index ? qsTrId("controller_config.cancel") : qsTrId("controller_config.bind")
                        Layout.preferredWidth: 75
                        onClicked: {
                            if (win.axisCatch === bind.index) { win.stopCatching(); return }
                            win.axisCatch = bind.index
                            win.session.run("catchAxis")
                        }
                    }
                    Button { text: qsTrId("controller_config.unbind"); Layout.preferredWidth: 75; onClicked: win.controls.run("unbind", [bind.index]) }
                    Button { text: qsTrId("controller_config.flip_axis"); Layout.preferredWidth: 75; onClicked: win.controls.run("flip", [bind.index]) }
                    // the key pushing it up, and the sensitivity
                    BindingBox {
                        keys: bind.positive; session: win.session; keyNamer: win.keyNamer; themeColor: win.themeColor
                        press: (name) => win.controls.run("press", ["positive", bind.index, name])
                        special: (name) => win.controls.run("special", ["positive", bind.index, name])
                        Layout.preferredWidth: 100
                    }
                    Label { text: bind.axis + "+"; Layout.preferredWidth: 92 }
                    Label { text: qsTrId("controller_config.sensitivity").arg(Math.round(bind.mult * 100)); Layout.preferredWidth: 110 }
                    Slider {
                        Layout.columnSpan: 2
                        Layout.preferredWidth: 160
                        from: -40; to: 40; stepSize: 1; snapMode: Slider.SnapAlways
                        value: Math.round(bind.mult * 20)
                        onMoved: win.controls.run("setAnalog", [bind.index, "mult", value / 20])
                    }
                    // the key pushing it down, and the dead zone
                    BindingBox {
                        keys: bind.negative; session: win.session; keyNamer: win.keyNamer; themeColor: win.themeColor
                        press: (name) => win.controls.run("press", ["negative", bind.index, name])
                        special: (name) => win.controls.run("special", ["negative", bind.index, name])
                        Layout.preferredWidth: 100
                    }
                    Label { text: bind.axis + "-"; Layout.preferredWidth: 92 }
                    Label { text: qsTrId("controller_config.deadzone").arg(Math.round(bind.deadzone * 100)); Layout.preferredWidth: 110 }
                    Slider {
                        Layout.columnSpan: 2
                        Layout.preferredWidth: 160
                        from: 0; to: 25; stepSize: 1; snapMode: Slider.SnapAlways
                        value: Math.round(bind.deadzone * 50)
                        onMoved: win.controls.run("setAnalog", [bind.index, "deadzone", value / 50])
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                TabBar {
                    id: kinds
                    Layout.fillWidth: true
                    TabButton { text: qsTrId("controller_config.normal_controls"); width: implicitWidth }
                    TabButton { text: qsTrId("controller_config.autofire_controls"); width: implicitWidth }
                    // Chimera leaves the tab out for a controller with no analog controls
                    TabButton { text: qsTrId("controller_config.analog_controls"); width: visible ? implicitWidth : 0; visible: win.axisGroups.length > 0 }
                }
                StackLayout {
                    currentIndex: kinds.currentIndex
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Repeater {
                        model: ["normal", "autofire", "analog"]
                        delegate: Frame {
                            id: kind
                            required property string modelData
                            readonly property var groups: kind.modelData === "analog" ? win.axisGroups : win.buttonGroups
                            padding: 3
                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 0
                                // what the boxes do, above them (issue 418), and TAStudio's columns of a tab shown or hidden at once
                                RowLayout {
                                    visible: kind.modelData !== "autofire"
                                    Layout.fillWidth: true
                                    Layout.bottomMargin: 4
                                    spacing: 6
                                    function showAll(shown) {
                                        win.controls.run("showColumns", [kind.modelData === "analog" ? "analog" : "buttons",
                                            kind.groups.length > 1 ? kind.groups[groupTabs.currentIndex] : "", shown])
                                    }
                                    Label {
                                        text: win.controls.columnsRoll > 0 ? qsTrId("controller_config.ticked_box_column_roll").arg(win.controls.columnsRoll)
                                                                           : qsTrId("controller_config.ticked_box_column")
                                        wrapMode: Text.Wrap
                                        Layout.fillWidth: true
                                    }
                                    Button { objectName: "showAllColumns"; text: qsTrId("controller_config.show_all"); onClicked: parent.showAll(true) }
                                    Button { objectName: "hideAllColumns"; text: qsTrId("controller_config.hide_all"); onClicked: parent.showAll(false) }
                                }
                                // a tab a player when there is more than one, as Chimera's
                                TabBar {
                                    id: groupTabs
                                    visible: kind.groups.length > 1
                                    Layout.fillWidth: true
                                    Repeater {
                                        model: kind.groups.length > 1 ? kind.groups : []
                                        // a group's identifier, "Player 2" or "Console", in the language in use
                                        delegate: TabButton {
                                            required property string modelData
                                            text: modelData.startsWith("Player ") ? qsTrId("controls.group_player").arg(modelData.substring(7)) : qsTrId("controls.group_console")
                                            width: implicitWidth
                                        }
                                    }
                                }
                                StackLayout {
                                    currentIndex: kind.groups.length > 1 ? groupTabs.currentIndex : 0
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    Repeater {
                                        model: kind.groups.length > 1 ? kind.groups : [""]
                                        delegate: Loader {
                                            id: page
                                            required property string modelData
                                            sourceComponent: kind.modelData === "analog" ? analogPage : keyPage
                                            Component { id: keyPage; KeyPanel { table: kind.modelData; group: page.modelData; everyGroup: kind.groups.length <= 1 } }
                                            Component { id: analogPage; AnalogPanel { group: page.modelData; everyGroup: kind.groups.length <= 1 } }
                                        }
                                    }
                                }
                                // Chimera's relative mouse sensitivity, for every control the pointer's movement moves
                                RowLayout {
                                    visible: kind.modelData === "analog"
                                    Layout.topMargin: 4
                                    spacing: 6
                                    Label { text: qsTrId("controller_config.pointer_movement_rmouse_x_y_sensitivity") }
                                    SpinBox {
                                        objectName: "mouseSensitivity"
                                        from: 1
                                        to: 1000
                                        stepSize: 10
                                        editable: true
                                        value: win.controls.mouseSensitivityCopy
                                        onValueModified: win.controls.mouseSensitivityCopy = value
                                    }
                                    Label { text: qsTrId("controller_config.text") }
                                }
                            }
                        }
                    }
                }
            }
            // the package's picture of the controller; without one the column is gone
            Image {
                objectName: "controllerPicture"
                visible: win.controls.pictureShown > 0
                source: visible ? "image://controller/" + win.controls.pictureShown : ""
                fillMode: Image.PreserveAspectFit
                Layout.preferredWidth: visible ? 334 : 0
                Layout.fillHeight: true
                Layout.topMargin: 20
                cache: false
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label { text: qsTrId("controller_config.tips") }
            Label { text: qsTrId("controller_config.escape_clears_a_key_mapping") }
            Label { text: qsTrId("controller_config.disable_auto_tab_to_multiply_bind") }
            Item { Layout.fillWidth: true }
            CheckBox {
                text: qsTrId("controller_config.auto_tab")
                checked: win.controls.autoTab
                onToggled: win.controls.autoTab = checked
            }
            Button {
                text: qsTrId("controller_config.misc")
                Layout.preferredWidth: 75
                onClicked: miscMenu.popup()
                Menu {
                    delegate: MenuEntry {}
                    id: miscMenu
                    MenuItem { text: qsTrId("controller_config.load_defaults"); onTriggered: win.controls.run("loadDefaults") }
                    MenuItem { text: qsTrId("common.clear"); onTriggered: win.controls.run("clear") }
                }
            }
            Button { text: qsTrId("common.save"); Layout.preferredWidth: 75; onClicked: win.save() }
            Button { text: qsTrId("common.cancel_mnemonic"); Layout.preferredWidth: 75; onClicked: win.close() }
        }
    }
}
