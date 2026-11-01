// A dialog asking for a line of text, as wide as its field, with Enter as
// OK (issues 437, 455): TAStudio's branch and marker texts. Layout and
// bindings only.
import QtQuick
import QtQuick.Controls

Dialog {
    id: prompt
    property alias text: field.text
    property alias placeholderText: field.placeholderText
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    TextField {
        id: field
        objectName: "promptField"
        implicitWidth: 300
        onAccepted: prompt.accept()
    }
    onOpened: field.forceActiveFocus()
}
