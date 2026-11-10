// The mouse over a list's rows, as a Windows list has it (RAM Search's and
// RAM Watch's): a click picks a row, Ctrl+click picks one more or lets one go,
// Shift+click picks every row from the one clicked before, and a drag picks
// every row from where it began to the one under the pointer, scrolling the
// list when held past its top or bottom (issue 554). A click below the rows
// lets every row go. One area over the whole list rather than one in each
// row, so a drag goes on while the rows under it scroll away.
//
// What it picks it tells the window (pick); the window's view model keeps
// the rows picked, and the row a Shift+click or a drag counts from.
import QtQuick
import QtQuick.Controls

MouseArea {
    id: area
    required property ListView list
    // a row and how: "only", "toggle", "range" (from the row clicked before), or -1 and "none"
    signal pick(int row, string mode)
    // a right click, after its row is picked (-1 below the rows)
    signal menu(int row)
    // a double click on a row
    signal activated(int row)
    // whether a row is picked now: a right click on a picked row keeps the rows picked
    property var isPicked: (row) => false

    Component.onCompleted: parent = list
    anchors.fill: parent
    anchors.rightMargin: list.ScrollBar.vertical && list.ScrollBar.vertical.visible ? list.ScrollBar.vertical.width : 0
    z: 1
    acceptedButtons: Qt.LeftButton | Qt.RightButton
    // the list would take a drag to scroll itself
    preventStealing: true

    property bool dragging: false
    property real lastY: 0
    property int lastRow: -1

    function rowAt(y) { return list.indexAt(list.contentX + 1, y + list.contentY) }
    function dragTo(y) {
        lastY = y
        if (list.count === 0) return
        let r = rowAt(Math.max(0, Math.min(y, list.height - 1)))
        if (r < 0) r = y < 0 ? 0 : list.count - 1
        if (r === lastRow) return
        lastRow = r
        area.pick(r, "range")
    }

    onPressed: (mouse) => {
        list.forceActiveFocus()
        const r = rowAt(mouse.y)
        if (mouse.button === Qt.RightButton) {
            if (r < 0) area.pick(-1, "none")
            else if (!area.isPicked(r)) area.pick(r, "only")
            area.menu(r)
            return
        }
        if (r < 0) {
            area.pick(-1, "none")
            return
        }
        area.pick(r, (mouse.modifiers & Qt.ShiftModifier) ? "range" : (mouse.modifiers & Qt.ControlModifier) ? "toggle" : "only")
        dragging = true
        lastY = mouse.y
        lastRow = r
    }
    onPositionChanged: (mouse) => { if (dragging) dragTo(mouse.y) }
    onReleased: dragging = false
    onCanceled: dragging = false
    onDoubleClicked: (mouse) => {
        const r = rowAt(mouse.y)
        if (mouse.button === Qt.LeftButton && r >= 0) area.activated(r)
    }

    // held past the top or the bottom: the list scrolls a row at a time and the drag follows
    Timer {
        interval: 50
        repeat: true
        running: area.dragging && (area.lastY < 0 || area.lastY >= area.list.height)
        onTriggered: {
            const l = area.list
            const most = Math.max(l.originY, l.originY + l.contentHeight - l.height)
            l.contentY = Math.max(l.originY, Math.min(most, l.contentY + (area.lastY < 0 ? -22 : 22)))
            area.dragTo(area.lastY)
        }
    }
}
