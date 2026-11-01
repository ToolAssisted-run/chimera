// Tools > Core-Specific Tools: every installed core's own tools, grouped by
// core - a submenu a core, its tools in its package's words and order -
// offered whatever is open, a project or none (owner, 2026-11-01). Greyed
// out while no core has any. Layout and bindings only; the main window opens
// the tool's window.
import QtQuick
import QtQuick.Controls

Menu {
    id: toolsMenu
    required property QtObject cores
    // a tool asked for: its core's name and its id
    signal openCoreTool(string core, string id)
    readonly property var list: {
        try { return JSON.parse(cores.coreTools) } catch (e) { return [] }
    }
    title: qsTrId("main.core_specific_tools")
    enabled: list.length > 0

    Instantiator {
        model: toolsMenu.list
        delegate: Menu {
            id: coreMenu
            required property var modelData
            title: coreMenu.modelData.core
            Instantiator {
                model: coreMenu.modelData.tools
                delegate: MenuItem {
                    required property var modelData
                    text: modelData.menu !== undefined ? modelData.menu : modelData.id
                    onTriggered: toolsMenu.openCoreTool(coreMenu.modelData.core, modelData.id)
                }
                onObjectAdded: (index, object) => coreMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => coreMenu.removeItem(object)
            }
        }
        onObjectAdded: (index, object) => toolsMenu.insertMenu(index, object)
        onObjectRemoved: (index, object) => toolsMenu.removeMenu(object)
    }
}
