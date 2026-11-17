// Chimera's Emulator menu, Game for a game core: there only with a project
// open, and holding only what applies to it, the menu gone when there is
// nothing. Chimera's own item is Display Game Time (a game core's timer, a
// declared game property); every other item is one of the core's own windows,
// its design in its package and the core its view model (owner, 2026-11-17,
// issue 610: Chimera knows no core tool - Export Save Data, CPU Registers,
// the Trace Logger and the Surface Viewer are the cores' own now). A core's
// file tools are under Tools > Core-Specific Tools (owner, 2026-11-01).
// Layout and bindings only; the main window does what an item asks for.
import QtQuick
import QtQuick.Controls

Menu {
    id: menu
    required property QtObject session
    // a hotkey's keys, for the item it runs
    property var keysOf: (hotkey) => ""
    // one of the core's own windows asked for, by its id (issue 610)
    signal openCoreWindow(string id)

    readonly property bool gameTime: session.isOpen && session.hasGameTimer
    // the core's own windows, as its package names them
    readonly property var coreWindowList: session.isOpen ? JSON.parse(session.coreWindows) : []
    readonly property bool coreWindowsShown: coreWindowList.length > 0
    readonly property bool shown: gameTime || coreWindowsShown
    title: session.isGameCore ? qsTrId("main.game") : qsTrId("main.emulator")

    component Entry: KeyedMenuEntry { height: visible ? implicitHeight : 0 }
    Entry {
        text: qsTrId("main.display_game_time"); visible: menu.gameTime; keys: menu.keysOf("Game Time")
        checkable: true; checked: menu.session.showGameTime; onTriggered: menu.session.showGameTime = checked
    }
    MenuDivider {
        visible: menu.gameTime && menu.coreWindowsShown
        height: visible ? implicitHeight : 0
    }
    // the core's own windows, in the package's order
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
