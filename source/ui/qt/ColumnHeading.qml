// A list's column heading: its name at the left, as the other lists' are
// (issue 520); a click sorts by it; its right edge dragged makes the column
// wider or narrower (issue 523), said while dragged (resizing) and when let
// go (resized). Layout and bindings only.
import QtQuick
import QtQuick.Controls

Button {
    id: heading
    property real minimum: 24
    signal resizing(real width)
    signal resized(real width)
    flat: true
    padding: 2
    leftPadding: 3
    contentItem: Label {
        text: heading.text
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignLeft
        verticalAlignment: Text.AlignVCenter
        color: heading.palette.buttonText
    }
    MouseArea {
        objectName: "columnEdge"
        width: 6
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        cursorShape: Qt.SplitHCursor
        property real startX: 0
        property real startWidth: 0
        function widthAt(mouse) { return Math.max(heading.minimum, startWidth + mapToItem(null, mouse.x, 0).x - startX) }
        onPressed: (mouse) => { startX = mapToItem(null, mouse.x, 0).x; startWidth = heading.width }
        onPositionChanged: (mouse) => { if (pressed) heading.resizing(widthAt(mouse)) }
        onReleased: (mouse) => heading.resized(widthAt(mouse))
    }
}
