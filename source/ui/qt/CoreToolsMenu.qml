// Tools > Core-Specific Tools: every installed core's own tools, offered
// whatever is open, a project or none (owner, 2026-11-01), in one list, each
// named after its core ("DSDA-Doom: Import Demo (LMP)..."), cores in the
// list's order and each core's tools in its package's (owner, 2026-11-05:
// a menu a core put a third level under it, which went over this one when
// the window had no room beside it, issue 502). Greyed out while no core
// has any. Layout and bindings only; the main window opens the tool's window.
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
    // a row a tool: its core, its id and its words
    readonly property var rows: {
        const out = []
        for (const c of toolsMenu.list)
            for (const t of (c.tools || [])) out.push({core: c.core, id: t.id, text: c.core + ": " + (t.menu !== undefined ? t.menu : t.id)})
        return out
    }
    title: qsTrId("main.core_specific_tools")
    enabled: rows.length > 0

    Instantiator {
        model: toolsMenu.rows
        delegate: MenuItem {
            required property var modelData
            text: modelData.text
            onTriggered: toolsMenu.openCoreTool(modelData.core, modelData.id)
        }
        onObjectAdded: (index, object) => toolsMenu.insertItem(index, object)
        onObjectRemoved: (index, object) => toolsMenu.removeItem(object)
    }
}
