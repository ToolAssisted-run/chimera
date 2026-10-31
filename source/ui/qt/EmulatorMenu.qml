// Chimera's Emulator menu, Game for a game core: only what the core open
// offers, each item there only when it does, and the menu gone when it
// offers nothing. Chimera puts nothing of its own here: an importer's item
// is named by the core's package (owner, 2026-10-31, issue 428). Layout and
// bindings only; the main window does what an item asks for.
import QtQuick
import QtQuick.Controls

Menu {
    id: menu
    required property QtObject session
    // a hotkey's keys, for the item it runs
    property var keysOf: (hotkey) => ""
    // a tool window asked for: "registers", "surfaces", "trace"; or "import", the core's importer
    signal openTool(string tool)
    signal exportSaveData()

    readonly property bool gameTime: session.isOpen && session.hasGameTimer
    readonly property bool saveData: session.isOpen && session.hasSaveData
    readonly property bool importer: session.isOpen && session.movieImportMenu !== ""
    readonly property bool registers: session.isOpen && session.hasRegisters
    readonly property bool surfaces: session.isOpen && session.surfaces !== "[]"
    readonly property bool trace: session.isOpen && session.hasTrace
    readonly property bool shown: gameTime || saveData || importer || registers || surfaces || trace
    title: session.isGameCore ? qsTrId("main.game") : qsTrId("main.emulator")

    component Entry: KeyedMenuEntry { height: visible ? implicitHeight : 0 }
    Entry {
        text: qsTrId("main.display_game_time"); visible: menu.gameTime; keys: menu.keysOf("Game Time")
        checkable: true; checked: menu.session.showGameTime; onTriggered: menu.session.showGameTime = checked
    }
    Entry { text: qsTrId("main.export_save_data_dialog"); visible: menu.saveData; onTriggered: menu.exportSaveData() }
    // the core's own words for its importer, as its package names it
    Entry { text: menu.session.movieImportMenu; visible: menu.importer; onTriggered: menu.openTool("import") }
    MenuDivider {
        visible: (menu.gameTime || menu.saveData || menu.importer) && (menu.registers || menu.surfaces || menu.trace)
        height: visible ? implicitHeight : 0
    }
    // the tools the core offers, in Chimera's order
    Entry {
        text: qsTrId("main.cpu_registers"); icon.source: "qrc:/chimera/menu/Debugger.png"; visible: menu.registers
        onTriggered: menu.openTool("registers")
    }
    Entry { text: qsTrId("main.surface_viewer"); visible: menu.surfaces; onTriggered: menu.openTool("surfaces") }
    Entry { text: qsTrId("main.trace_logger"); visible: menu.trace; keys: menu.keysOf("Trace Logger"); onTriggered: menu.openTool("trace") }
}
