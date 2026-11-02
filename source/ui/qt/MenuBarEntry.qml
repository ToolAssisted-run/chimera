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
}
