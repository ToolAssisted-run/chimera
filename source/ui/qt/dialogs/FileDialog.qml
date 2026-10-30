// Chimera's file dialog (qmldir): the desktop's own chooser, Chimera's where
// there is none, the system's on Windows and macOS (PickerBase.qml). It has
// the properties and enums of Qt's FileDialog that Chimera's windows use.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as QtDialogs

PickerBase {
    id: dialog

    enum FileMode { OpenFile, OpenFiles, SaveFile }

    property int fileMode: FileDialog.OpenFile
    property var nameFilters: []
    property url currentFolder
    // before it opens, a save's file (or, as "file:///<name>", only its name)
    property url selectedFile
    property list<url> selectedFiles
    property string defaultSuffix
    // Qt's dialog is drawn inside its window (#389)
    readonly property int popupType: qtDialog.popupType

    system: QtDialogs.FileDialog {
        id: qtDialog
        popupType: Popup.Item
        onAccepted: {
            dialog.selectedFile = selectedFile
            dialog.selectedFiles = selectedFiles
            dialog.currentFolder = currentFolder
            dialog.finishSystem(true)
        }
        onRejected: dialog.finishSystem(false)
    }

    function folderOf(url) {
        const s = url.toString()
        return s.substring(0, s.lastIndexOf("/"))
    }
    function open() {
        const asked = {
            mode: fileMode === FileDialog.SaveFile ? "save" : fileMode === FileDialog.OpenFiles ? "openMany" : "open",
            folder: currentFolder.toString() !== "" ? currentFolder.toString() : "",
            name: fileMode === FileDialog.SaveFile && selectedFile.toString() !== "" ? decodeURIComponent(selectedFile.toString().substring(selectedFile.toString().lastIndexOf("/") + 1)) : "",
            nameFilters: nameFilters,
            defaultSuffix: defaultSuffix,
        }
        if (usesSystem) {
            if (shown) return
            if (systemWithBox(asked)) return
            qtDialog.title = title
            qtDialog.fileMode = fileMode
            qtDialog.nameFilters = nameFilters
            qtDialog.defaultSuffix = defaultSuffix
            if (currentFolder.toString() !== "") qtDialog.currentFolder = currentFolder
            if (selectedFile.toString() !== "") qtDialog.selectedFile = selectedFile
            if (parentWindow) qtDialog.parentWindow = parentWindow
            shown = true
            result = 0
            qtDialog.open()
            return
        }
        // a file named as "file:///<name>" names no folder
        const fromFile = selectedFile.toString() === "" ? "" : folderOf(selectedFile)
        if (asked.folder === "" && fromFile !== "file://") asked.folder = fromFile
        begin(asked)
    }
    function close() {
        if (usesSystem) qtDialog.close()
        end()
    }
    function finishSystem(chosen) {
        shown = false
        result = chosen ? 1 : 0
        if (chosen) accepted()
        else rejected()
    }
    onPicked: urls => {
        selectedFiles = urls
        selectedFile = urls[0]
        currentFolder = folderOf(urls[0])
    }
}
