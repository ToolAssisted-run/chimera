// A tool window's options as Chimera keeps them (Save Window Position, Stay
// on Top, Float From Main Window, Restore Defaults), applied to its window:
// put back where it was left, above everything when asked, and owned by the
// main window unless floating. The menu items are the window's own, at the
// end of its Settings menu as Chimera puts them. Bindings only, over the
// toolWindows view model.
import QtQuick

QtObject {
    id: opts
    required property Window window
    required property string tool
    property QtObject vm: null
    property Window mainWindow: null
    readonly property var mine: opts.vm ? (JSON.parse(opts.vm.settings)[opts.tool] || ({})) : ({})
    readonly property bool savePosition: opts.mine.save !== false
    readonly property bool onTop: opts.mine.top === true
    readonly property bool floating: opts.mine.float === true
    // Chimera leaves staying on top to the window manager off Windows
    readonly property bool canStayOnTop: Qt.platform.os === "windows"
    // the size the window was made with, what Restore Defaults gives back
    property int defaultWidth: 0
    property int defaultHeight: 0
    Component.onCompleted: { opts.defaultWidth = opts.window.width; opts.defaultHeight = opts.window.height }
    // the program quit with the window open: kept as Chimera keeps it on closing
    Component.onDestruction: if (opts.window.visible) opts.keepPlace()

    function set(name, on) { if (opts.vm) opts.vm.run("setOption", [opts.tool, name, on]) }
    function restore() {
        if (opts.vm) opts.vm.run("restoreDefaults", [opts.tool])
        opts.window.width = opts.defaultWidth
        opts.window.height = opts.defaultHeight
    }
    // where it was left (or only its size, for a window that places itself),
    // when that is kept; false when nothing is
    function placeBack(sizeOnly) {
        const m = opts.mine
        if (!opts.savePosition || m.w === undefined) return false
        if (!sizeOnly) {
            opts.window.x = m.x
            opts.window.y = m.y
        }
        opts.window.width = m.w
        opts.window.height = m.h
        return true
    }
    function keepPlace() {
        if (opts.vm) opts.vm.run("place", [opts.tool, opts.window.x, opts.window.y, opts.window.width, opts.window.height])
    }

    property Connections closing: Connections {
        target: opts.window
        function onVisibleChanged() { if (!opts.window.visible) opts.keepPlace() }
    }
    property Binding owner: Binding {
        target: opts.window
        property: "transientParent"
        value: opts.floating ? null : opts.mainWindow
        when: opts.mainWindow !== null
    }
    // Windows takes a window's owner when the window is shown, so Float From
    // Main Window did nothing to a window already open: it is shown again
    // (issue 441)
    onFloatingChanged: if (opts.window.visible) Qt.callLater(() => { opts.window.hide(); opts.window.show() })
    // a title bar and its buttons named, as a window has them: with the
    // stay-on-top hint alone Windows drew none (issue 441)
    property Binding above: Binding {
        target: opts.window
        property: "flags"
        value: Qt.Window | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMinMaxButtonsHint | Qt.WindowCloseButtonHint
            | (opts.onTop && opts.canStayOnTop ? Qt.WindowStaysOnTopHint : 0)
    }
}
