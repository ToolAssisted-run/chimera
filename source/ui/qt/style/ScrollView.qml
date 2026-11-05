// Fusion's scroll view, with its content kept clear of its scroll bars: they
// are seen all the time now (ScrollBar.qml), and Fusion draws them over the
// content's edge, where they hid the end of a line.
import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T

T.ScrollView {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    // text that wraps is as wide as the view and never goes sideways: its own width,
    // taken before the vertical bar took its room, showed a sideways bar for a few pixels
    readonly property bool wrapsText: contentChildren.length === 1 && contentChildren[0] instanceof T.TextArea
        && contentChildren[0].wrapMode !== TextEdit.NoWrap

    // the content stops at its ends: Qt's own let a mouse wheel carry it past
    // them, out of sight, until it sprang back (issue 515)
    function stopAtEnds() { if (control.contentItem instanceof Flickable) control.contentItem.boundsBehavior = Flickable.StopAtBounds }
    onContentItemChanged: stopAtEnds()
    Component.onCompleted: stopAtEnds()

    rightPadding: control.mirrored ? 0 : effectiveScrollBarWidth
    leftPadding: control.mirrored ? effectiveScrollBarWidth : 0
    bottomPadding: effectiveScrollBarHeight

    ScrollBar.vertical: ScrollBar {
        parent: control
        x: control.mirrored ? 0 : control.width - width
        y: control.topPadding
        height: control.availableHeight
    }

    ScrollBar.horizontal: ScrollBar {
        policy: control.wrapsText ? ScrollBar.AlwaysOff : ScrollBar.AsNeeded
        parent: control
        x: control.leftPadding
        y: control.height - height
        width: control.availableWidth
    }
}
