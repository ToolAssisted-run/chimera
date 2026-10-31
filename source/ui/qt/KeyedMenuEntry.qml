import QtQuick
import QtQuick.Controls

// A menu's row that shows the keys bound to what it does, at its right, as
// Chimera's menus show a hotkey beside its item.
MenuEntry {
    id: entry
    property string keys: ""
    rightPadding: padding + (keys !== "" ? keyLabel.implicitWidth + 24 : 0)
    Label {
        id: keyLabel
        text: entry.keys
        anchors.right: parent.right
        anchors.rightMargin: entry.padding
        anchors.verticalCenter: parent.verticalCenter
        color: entry.highlighted ? entry.palette.highlightedText : entry.palette.text
    }
}
