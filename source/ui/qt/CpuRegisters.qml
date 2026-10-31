// CPU Registers: the registers a core shows, read as the machine moves and
// set by hand (values in hex, flags as buttons pressed when set). In the
// Emulator menu for a core that shows its registers. It takes the place of
// a Debugger whose disassembler, breakpoints and stepping no core could
// use (owner, 2026-10-31, issue 428). Layout and bindings only, over the
// registers view model.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "cpuRegistersWindow"
    required property QtObject vm
    required property QtObject session
    property QtObject customize: null
    // the theme's colour for a role (Main hands it its lookup)
    property var themeColor: (role, fallback) => fallback
    property QtObject toolWindows: null
    property Window mainWindow: null
    title: qsTrId("cpu_registers.cpu_registers")
    width: 320
    height: 420
    onVisibleChanged: {
        if (visible) { windowOptions.placeBack(false); win.vm.run("refresh") }
    }

    WindowOptions { id: windowOptions; window: win; tool: "registers"; vm: win.toolWindows; mainWindow: win.mainWindow }

    // read again as the machine moves, while the window is open, at most ten times a second
    Connections {
        target: win.session
        enabled: win.visible
        function onFrameChanged() { if (!refreshTimer.running) refreshTimer.start() }
        function onRunningChanged() { win.vm.run("refresh") }
    }
    Timer { id: refreshTimer; interval: 100; onTriggered: win.vm.run("refresh") }

    menuBar: MenuBar {
        Menu {
            delegate: MenuEntry {}
            title: qsTrId("common.file")
            MenuItem { text: qsTrId("common.close_mnemonic"); onTriggered: win.close() }
        }
        // Chimera adds a Settings menu for the window's items to a tool that has none
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

    footer: Label {
        text: win.vm.error !== "" ? win.vm.error
            : !win.session.hasRegisters ? (win.session.isOpen ? qsTrId("cpu_registers.this_core_does_not_show_its") : qsTrId("cpu_registers.no_core_is_running"))
            : ""
        color: win.vm.error !== "" ? win.themeColor("AccentError", "#b00020") : palette.text
        padding: 4
        visible: text !== ""
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 8
        clip: true
        enabled: win.session.hasRegisters
        ColumnLayout {
            // the registers of more than one bit: a name and its value in hex
            Repeater {
                model: win.vm.rows("registers")
                delegate: RowLayout {
                    id: reg
                    required property int index
                    required property string name
                    required property string hex
                    required property int bits
                    visible: reg.bits !== 1
                    Label { text: reg.name; Layout.preferredWidth: 80 }
                    TextField {
                        text: reg.hex
                        font.family: "monospace"
                        font.capitalization: Font.AllUppercase
                        // as many digits as the view model shows (a 6-bit register is 2)
                        maximumLength: Math.max(1, Math.ceil(reg.bits / 4))
                        Layout.preferredWidth: 16 + Math.max(1, Math.ceil(reg.bits / 4)) * 10
                        onEditingFinished: if (text !== reg.hex && text !== "") win.vm.run("setRegister", [reg.index, "0x" + text])
                    }
                }
            }
            // the flags: a button each, pressed when set, the last first, "Flag" left off, as Chimera's
            Flow {
                Layout.preferredWidth: 280
                spacing: 0
                Repeater {
                    model: win.vm.rows("registers")
                    delegate: Button {
                        id: flag
                        required property int index
                        required property string name
                        required property var value
                        required property int bits
                        visible: flag.bits === 1
                        width: visible ? implicitWidth : 0
                        text: flag.name.replace("Flag", "").trim()
                        checkable: true
                        checked: Number(flag.value) === 1
                        padding: 4
                        onClicked: win.vm.run("setRegister", [flag.index, checked ? 1 : 0])
                    }
                }
            }
        }
    }
}
