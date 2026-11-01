// A box a binding is made in, as Chimera does: the keys
// bound, and an arrow for the bindings a key cannot give (Escape, the
// pointer's buttons). While it has the focus it takes the next key or
// gamepad input pressed: a key when it is let go, as Chimera's (so Ctrl
// alone, or Ctrl+S, can be bound), Escape at once. Controller Config and
// Configure Hotkeys use it.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: box
    required property string keys
    required property QtObject session
    required property QtObject keyNamer
    // a hotkey takes combinations ("Ctrl+S"); a controller's button a key alone
    property bool combination: false
    // a key or gamepad input pressed, and a special binding picked: true when the next box takes the next
    property var press: (name) => false
    property var special: (name) => false
    // goes on to the next box, for Auto Tab
    property var next: null
    property var themeColor: (role, fallback) => fallback
    readonly property string tips: qsTrId("binding_box.escape_clears_a_key_mapping_disable")
    spacing: 0
    function focusField() { catcher.forceActiveFocus() }
    function take(name) {
        if (box.press(name) === true && box.next) box.next()
        // the box keeps the focus: it waits for the next input
        else if (catcher.activeFocus) box.session.run("catchPad")
    }
    // the last key pressed, bound when a key is let go
    property string lastPress: ""

    Connections {
        target: box.session
        enabled: catcher.activeFocus
        function onCaughtPadChanged() { if (box.session.caughtPad !== "") box.take(box.session.caughtPad) }
    }
    TextField {
        id: field
        Layout.fillWidth: true
        Layout.preferredHeight: 23
        readOnly: true
        // the keys are taken by the catcher over it: a read-only field
        // refuses every shortcut's override, so a window's or a menu's
        // shortcut on Escape or Ctrl+S took the key from the box (issue 431)
        focusPolicy: Qt.NoFocus
        activeFocusOnPress: false
        text: box.keys
        padding: 2
        // the start of the keys shown, as a text box shows them
        onTextChanged: cursorPosition = 0
        Component.onCompleted: cursorPosition = 0
        background: Rectangle {
            color: catcher.activeFocus ? box.themeColor("InputAwaitingBackground", "#e0ffff") : box.themeColor("InputBackground", palette.base)
            border.color: catcher.activeFocus ? palette.highlight : palette.mid
        }
        ToolTip.visible: hovered
        ToolTip.delay: 500
        ToolTip.text: qsTrId("binding_box.current_binding").arg(box.keys) + "\n---\n" + box.tips
        Item {
            id: catcher
            objectName: "bindingBox"
            anchors.fill: parent
            activeFocusOnTab: true
            onActiveFocusChanged: {
                box.lastPress = ""
                box.session.run(activeFocus ? "catchPad" : "stopCatchPad")
            }
            // Alt+F4 still closes the window; nothing else gets past the box
            function closes(event) { return event.key === Qt.Key_F4 && (event.modifiers & Qt.AltModifier) }
            Keys.onShortcutOverride: (event) => { event.accepted = !catcher.closes(event) }
            Keys.onPressed: (event) => {
                if (catcher.closes(event)) return
                event.accepted = true
                if (event.isAutoRepeat) return
                const name = box.keyNamer.name(event.key, event.modifiers, box.combination)
                if (name === "") return
                if (name === "Escape") { box.lastPress = ""; box.take(name); return }
                box.lastPress = name
            }
            Keys.onReleased: (event) => {
                event.accepted = true
                if (event.isAutoRepeat || box.lastPress === "") return
                const name = box.lastPress
                box.lastPress = ""
                box.take(name)
            }
        }
        TapHandler { onTapped: catcher.forceActiveFocus() }
    }
    ToolButton {
        focusPolicy: Qt.NoFocus
        Layout.preferredWidth: 18
        Layout.preferredHeight: 23
        icon.source: "qrc:/chimera/icons/arrow_black_down.png"
        ToolTip.visible: hovered
        ToolTip.delay: 500
        ToolTip.text: qsTrId("binding_box.click_here_for_special_tricky_bindings")
        // made when asked for: a menu in every box was most of what a window
        // of boxes cost to make (issue 438)
        onClicked: specialMenu.createObject(this).popup()
        Component {
            id: specialMenu
            Menu {
                delegate: MenuEntry {}
                onClosed: destroy()
                Repeater {
                    model: [
                        { name: "Escape", tip: qsTrId("binding_box.binds_the_escape_key") },
                        { name: "WMouse L", tip: qsTrId("binding_box.binds_the_left_mouse_button") },
                        { name: "WMouse M", tip: qsTrId("binding_box.binds_the_middle_mouse_button") },
                        { name: "WMouse R", tip: qsTrId("binding_box.binds_the_right_mouse_button") },
                        { name: "WMouse 1", tip: qsTrId("binding_box.binds_the_mouse_auxiliary_button_1") },
                        { name: "WMouse 2", tip: qsTrId("binding_box.binds_the_mouse_auxiliary_button_2") }
                    ]
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.name
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.tip
                        onTriggered: if (box.special(modelData.name) === true && box.next) box.next()
                    }
                }
            }
        }
    }
}
