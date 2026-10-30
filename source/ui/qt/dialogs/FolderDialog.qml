// Chimera's folder dialog (qmldir): the desktop's own chooser, Chimera's
// where there is none, the system's on Windows and macOS (PickerBase.qml).
// It has the properties of Qt's FolderDialog that Chimera's windows use.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as QtDialogs

PickerBase {
    id: dialog

    property url currentFolder
    property url selectedFolder
    // Qt's dialog is drawn inside its window (#389)
    readonly property int popupType: qtDialog.popupType

    system: QtDialogs.FolderDialog {
        id: qtDialog
        popupType: Popup.Item
        onAccepted: {
            dialog.selectedFolder = selectedFolder
            dialog.currentFolder = currentFolder
            dialog.shown = false
            dialog.result = 1
            dialog.accepted()
        }
        onRejected: {
            dialog.shown = false
            dialog.result = 0
            dialog.rejected()
        }
    }

    function open() {
        const asked = { mode: "folder", folder: currentFolder.toString() }
        if (usesSystem) {
            if (shown) return
            if (systemWithBox(asked)) return
            qtDialog.title = title
            if (currentFolder.toString() !== "") qtDialog.currentFolder = currentFolder
            if (parentWindow) qtDialog.parentWindow = parentWindow
            shown = true
            result = 0
            qtDialog.open()
            return
        }
        begin(asked)
    }
    function close() {
        if (usesSystem) qtDialog.close()
        end()
    }
    onPicked: urls => {
        selectedFolder = urls[0]
        currentFolder = urls[0]
    }
}
