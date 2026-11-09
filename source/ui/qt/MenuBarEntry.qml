import QtQuick
import QtQuick.Controls

// An item of the main window's menu bar, left out and taking no room while
// its menu is not shown (System and Emulator while no machine runs). Its
// width goes by its menu, not by whether the item is visible: every item is
// invisible while the bar is hidden, as it is full screen, and a width that
// went to 0 there came back short of its text, which was then cut or not
// drawn at all (issue 451).
MenuBarItem {
    id: entry
    readonly property bool listed: entry.menu !== null && entry.menu.shown !== false
    visible: entry.listed
    width: entry.listed ? implicitWidth : 0

    // Its menu closed gives the keyboard back to what had it before (issue
    // 552): Qt's menu bar kept it after a menu opened with the mouse was
    // closed by clicking its name again, and Space then opened the menu
    // again instead of reaching the game, and the arrows moved along the bar.
    property Item before: null
    function inBar(item) {
        for (let i = item; i !== null; i = i.parent)
            if (i === entry.menuBar) return true
        return false
    }
    Connections {
        target: entry.menu
        function onAboutToShow() {
            const now = entry.Window.activeFocusItem
            if (now !== null && !entry.inBar(now) && !entry.inMenus(now)) entry.before = now
        }
        function onClosed() {
            Qt.callLater(() => {
                const now = entry.Window.activeFocusItem
                // another menu of the bar opened meanwhile: it has the keyboard, rightly
                if (entry.menuBar !== null && entry.menuBar.currentItem !== null && entry.menuBar.currentItem.highlighted) return
                if (now === null || entry.inBar(now) || entry.inMenus(now)) {
                    if (entry.before !== null && entry.before.visible) entry.before.forceActiveFocus()
                    else if (entry.Window.contentItem !== null) entry.Window.contentItem.forceActiveFocus()
                }
            })
        }
    }
    // an item of one of the bar's menus (their popups are not the bar's children)
    function inMenus(item) {
        if (entry.menuBar === null) return false
        for (let i = item; i !== null; i = i.parent)
            for (let m = 0; m < entry.menuBar.count; m++) {
                const menu = entry.menuBar.menuAt(m)
                if (menu !== null && (i === menu.contentItem || i === menu.background || (menu.contentItem !== null && i === menu.contentItem.parent))) return true
            }
        return false
    }
}
