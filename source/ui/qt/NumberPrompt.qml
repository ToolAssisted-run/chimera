// A dialog asking for a whole number, with the number's box ready to type
// in when it opens (issue 621: a window opened by a hotkey, such as Ctrl+G,
// did not have the keyboard, and the box had to be clicked first), and Enter
// as OK. TAStudio's Insert # Frames, Clone # Times and Auto-hold's value.
// Layout and bindings only.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: prompt
    property string question: ""
    property alias from: box.from
    property alias to: box.to
    property alias value: box.value
    modal: true
    // a popup's content has the keyboard only when the popup asks for it
    focus: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    // opens with the number to start from
    function openWith(start) { prompt.value = start; prompt.open() }
    ColumnLayout {
        Label { text: prompt.question; visible: text !== "" }
        SpinBox {
            id: box
            objectName: "numberBox"
            editable: true
            // Enter in the box is OK
            Keys.onReturnPressed: { box.focus = false; prompt.accept() }
            Keys.onEnterPressed: { box.focus = false; prompt.accept() }
        }
    }
    onOpened: { box.forceActiveFocus(); box.contentItem.selectAll() }
}
