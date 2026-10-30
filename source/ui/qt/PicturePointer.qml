// The pointer over the picture as the machine is told of it: where it is
// and which of its buttons are held. Only watched, never taken from what
// is under it. Qt's PointHandler empties its point when the last button is
// let go (no id, at 0,0) and the hover handler says where the pointer is
// only afterwards, so a button let go is told where the pointer last was,
// and a pointer that leaves stays where it was last seen: a Flash game took
// a button let go in the window's corner as no click (issue 424).
import QtQuick

Item {
    id: pointer
    // a place in this item, and the buttons held there (-1: a move only)
    signal noted(point at, int buttons)
    property point lastAt: Qt.point(NaN, NaN)
    function tell(at, buttons) {
        pointer.lastAt = at
        pointer.noted(at, buttons)
    }
    PointHandler {
        acceptedButtons: Qt.AllButtons
        grabPermissions: PointerHandler.ApprovesTakeOverByAnything
        onPointChanged: pointer.tell(point.id >= 0 || isNaN(pointer.lastAt.x) ? point.position : pointer.lastAt, point.pressedButtons)
    }
    HoverHandler {
        onPointChanged: if (point.id >= 0) pointer.tell(point.position, -1)
    }
}
