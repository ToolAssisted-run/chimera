// What FileDialog and FolderDialog share (qmldir). On Linux and the BSDs a
// file or folder is picked with the desktop's own chooser, asked for through
// the XDG desktop portal (portalChooser, portal_file_chooser.cpp); where the
// desktop has none, with Chimera's own (BuiltinChooser.qml). On Windows and
// macOS each shows Qt's dialog, which is the system's (docs/file-picker.md).
import QtQuick
import QtQuick.Controls

QtObject {
    id: base

    property string title
    // the window it is shown over; by default the one it is declared in
    property Window parentWindow: null
    // 1 once accepted, 0 otherwise, as Qt's dialogs have it
    property int result: 0
    // a tick box the chooser itself carries, such as Include sub-folders:
    // its label (none when empty), and whether it is ticked, before the
    // dialog opens and as the person confirmed it after
    property string checkBoxText
    property bool checkBoxChecked: false
    readonly property bool visible: shown
    signal accepted()
    signal rejected()

    // Qt's dialog, set by FileDialog and FolderDialog: the system's on Windows
    // and macOS, and everywhere the way to the window it is declared in
    property QtObject system: null
    readonly property bool usesSystem: Qt.platform.os === "windows" || Qt.platform.os === "osx"
    // what was picked, given to FileDialog and FolderDialog before accepted()
    signal picked(var urls)

    property bool shown: false
    property bool asking: false
    property var request: null
    property Dialog builtin: null
    readonly property Component builtinComponent: Component { BuiltinChooser {} }

    // Chimera's chooser lives in the window's items: it goes with the dialog
    Component.onDestruction: if (builtin) builtin.destroy()

    // the window it is declared in: Qt's dialog, a property's value, has no
    // parent to find it from, while this object is its window's
    function windowShown() {
        if (parentWindow) return parentWindow
        const found = typeof portalChooser !== "undefined" && portalChooser !== null ? portalChooser.windowOf(base) : null
        return found ? found : (system ? system.parentWindow : null)
    }

    // request: mode ("open", "openMany", "save", "folder"), folder, name,
    // nameFilters, defaultSuffix
    function begin(asked) {
        if (shown) return
        shown = true
        result = 0
        request = Object.assign({ checkBox: checkBoxText, checkBoxChecked: checkBoxChecked }, asked)
        // a run nobody watches (--headless) asks nobody outside it
        const portal = typeof portalChooser !== "undefined" && portalChooser !== null
            && !(typeof headlessRun !== "undefined" && headlessRun !== null && headlessRun.enabled)
        if (portal) {
            asking = true
            if (portalChooser.ask(Object.assign({ title: title, window: windowShown() }, request))) return
            asking = false
        }
        showBuiltin()
    }
    function showBuiltin() {
        const window = windowShown()
        if (!window) { finish([]); return }
        if (!builtin) builtin = builtinComponent.createObject(window.contentItem)
        builtin.parent = window.contentItem
        builtin.title = title
        builtin.mode = request.mode
        builtin.folder = request.folder || ""
        builtin.name = request.name || ""
        builtin.nameFilters = request.nameFilters || []
        builtin.defaultSuffix = request.defaultSuffix || ""
        builtin.checkBoxText = request.checkBox || ""
        builtin.checkBoxChecked = !!request.checkBoxChecked
        builtin.open()
    }
    function finish(urls) {
        shown = false
        if (urls.length > 0) {
            result = 1
            picked(urls)
            accepted()
        } else {
            result = 0
            rejected()
        }
    }
    // on Windows, a dialog with a tick box is Windows' own, shown by Chimera
    // (Qt's takes no box); false when it could not be, and Qt's is shown
    function systemWithBox(asked) {
        if (checkBoxText === "" || typeof systemDialog === "undefined" || systemDialog === null || !systemDialog.available) return false
        const answer = systemDialog.ask(Object.assign({ title: title, window: windowShown(), checkBox: checkBoxText, checkBoxChecked: checkBoxChecked }, asked))
        if (answer.outcome === 2) return false
        if (answer.outcome === 0) checkBoxChecked = answer.checkBoxChecked
        finish(answer.outcome === 0 ? answer.files : [])
        return true
    }
    // hidden with no answer, as Qt's close()
    function end() {
        if (builtin && builtin.opened) {
            builtin.answered = true
            builtin.close()
        }
        shown = false
    }

    readonly property Connections portalAnswers: Connections {
        target: typeof portalChooser !== "undefined" ? portalChooser : null
        function onAnswered(outcome, files, checkBox) {
            if (!base.asking) return
            base.asking = false
            // a chooser that did not carry the box leaves it as it was
            if (outcome === 0 && checkBox >= 0) base.checkBoxChecked = checkBox === 1
            // the portal's chooser could not be shown: Chimera's instead
            if (outcome === 2) base.showBuiltin()
            else base.finish(outcome === 0 ? files : [])
        }
    }
    readonly property Connections builtinAnswers: Connections {
        target: base.builtin
        function onChosen(urls) {
            if (base.checkBoxText !== "") base.checkBoxChecked = base.builtin.checkBoxChecked
            base.finish(urls)
        }
        function onCancelled() { base.finish([]) }
    }
}
