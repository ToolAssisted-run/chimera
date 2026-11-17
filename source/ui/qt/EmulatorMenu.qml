// Chimera's Emulator menu, Game for a game core: there only with a project
// open, and holding only what applies to it - what the core open offers,
// each item there only when it does (issue 428), the menu gone when it
// offers nothing. A core's own tools are not here: they are offered for
// every installed core under Tools > Core-Specific Tools (owner,
// 2026-11-01). Layout and bindings only; the main window does what an item
// asks for.
import QtQuick
import QtQuick.Controls

Menu {
    id: menu
    required property QtObject session
    // a hotkey's keys, for the item it runs
    property var keysOf: (hotkey) => ""
    // a tool window asked for: "registers", "surfaces", "trace"
    signal openTool(string tool)
    // one of the core's own windows asked for, by its id (issue 610)
    signal openCoreWindow(string id)
    signal exportSaveData()

    readonly property bool gameTime: session.isOpen && session.hasGameTimer
    readonly property bool saveData: session.isOpen && session.hasSaveData
    readonly property bool registers: session.isOpen && session.hasRegisters
    readonly property bool surfaces: session.isOpen && session.surfaces !== "[]"
    readonly property bool trace: session.isOpen && session.hasTrace
    // the core's own windows, as its package names them (issue 610)
    readonly property var coreWindowList: session.isOpen ? JSON.parse(session.coreWindows) : []
    readonly property bool coreWindowsShown: coreWindowList.length > 0
    readonly property bool shown: gameTime || saveData || registers || surfaces || trace || coreWindowsShown
    title: session.isGameCore ? qsTrId("main.game") : qsTrId("main.emulator")

    component Entry: KeyedMenuEntry { height: visible ? implicitHeight : 0 }
    Entry {
        text: qsTrId("main.display_game_time"); visible: menu.gameTime; keys: menu.keysOf("Game Time")
        checkable: true; checked: menu.session.showGameTime; onTriggered: menu.session.showGameTime = checked
    }
    Entry { text: qsTrId("main.export_save_data_dialog"); visible: menu.saveData; onTriggered: menu.exportSaveData() }
    MenuDivider {
        visible: (menu.gameTime || menu.saveData) && (menu.registers || menu.surfaces || menu.trace)
        height: visible ? implicitHeight : 0
    }
    // the tools the core offers, in Chimera's order
    Entry {
        text: qsTrId("main.cpu_registers"); icon.source: "qrc:/chimera/menu/Debugger.png"; visible: menu.registers
        onTriggered: menu.openTool("registers")
    }
    Entry { text: qsTrId("main.surface_viewer"); visible: menu.surfaces; onTriggered: menu.openTool("surfaces") }
    Entry { text: qsTrId("main.trace_logger"); visible: menu.trace; keys: menu.keysOf("Trace Logger"); onTriggered: menu.openTool("trace") }
    // the core's own windows, after Chimera's, in the package's order
    MenuDivider {
        visible: menu.coreWindowsShown && (menu.gameTime || menu.saveData || menu.registers || menu.surfaces || menu.trace)
        height: visible ? implicitHeight : 0
    }
    Instantiator {
        model: menu.coreWindowList
        delegate: Entry {
            required property var modelData
            text: modelData.menu
            onTriggered: menu.openCoreWindow(modelData.id)
        }
        onObjectAdded: (index, object) => menu.insertItem(menu.count, object)
        onObjectRemoved: (index, object) => menu.removeItem(object)
    }
}
