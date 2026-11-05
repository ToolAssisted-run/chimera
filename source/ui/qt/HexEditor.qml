// The Hex Editor, as Chimera's: its menus (File, Edit, Options, Settings);
// a box titled with the system, the domain and its addresses holding the
// column numbers, the addresses, the values and the text they spell; the
// address highlighted and those beside it drawn in the highlight colour.
// Keys and the mouse work as Chimera's do. Find and Colors are windows of
// their own. Only the lines on screen exist; the scroll bar and the wheel
// move the first line shown. Layout and bindings only, over the hexEditor
// view model.
import QtQuick
import QtQuick.Controls
import ChimeraDialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "hexEditorWindow"
    required property QtObject vm
    // the main window's: Paths folders, the clipboard, theme colours, the windows it opens
    property var folderOf: null
    property QtObject clipboard: null
    property var themeColor: (role, fallback) => fallback
    property QtObject customize: null
    property QtObject toolWindows: null
    property Window mainWindow: null
    property var showRamWatch: null
    title: win.vm.title
    width: 720
    height: 420
    minimumWidth: 600
    minimumHeight: 260

    WindowOptions { id: windowOptions; window: win; tool: "hexEditor"; vm: win.toolWindows; mainWindow: win.mainWindow }
    onVisibleChanged: {
        win.vm.watching = visible
        if (visible) windowOptions.placeBack(false)
    }

    // the colours: Custom Colors where set, else the theme's
    readonly property var custom: JSON.parse(win.vm.colors || "{}")
    function colour(role, themeRole, fallback) { return win.custom[role] || win.themeColor(themeRole, fallback) }
    readonly property color backColour: win.colour("Background", "HexBackground", palette.window)
    readonly property color textColour: win.colour("Foreground", "HexText", palette.text)
    readonly property color menuBarColour: win.colour("MenuBar", "HexMenuBar", palette.window)
    readonly property color highlightColour: win.colour("Highlight", "HexHighlight", "#FFC0CB")
    readonly property color secondaryColour: Qt.rgba(win.highlightColour.r, win.highlightColour.g, win.highlightColour.b, 0x44 / 255)

    readonly property bool anyHighlighted: win.vm.picked >= 0 || win.vm.secondaryCount > 0

    FontMetrics { id: fm; font.family: "monospace" }
    readonly property real charWidth: fm.advanceWidth("0")
    readonly property int lineHeight: Math.ceil(fm.height) + 2
    readonly property int digits: win.vm.lineCount * 16 <= 0x10000 ? 4 : win.vm.lineCount * 16 <= 0x1000000 ? 6 : 8
    readonly property real addressWidth: win.charWidth * (win.digits + 3)
    readonly property real cellWidth: win.charWidth * (win.vm.size * 2 + 1)
    readonly property real valuesWidth: win.cellWidth * (16 / win.vm.size)

    MessageDialog { id: notice; buttons: MessageDialog.Ok }
    function tell(title, text) { notice.title = title; notice.text = text; notice.open() }

    FileDialog {
        id: binaryDialog
        title: qsTrId("hex_editor.save_as_binary")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "bin"
        gameFileEnding: ".bin"
        nameFilters: [qsTrId("hex_editor.binary_files_bin"), qsTrId("common.all_files")]
        onAccepted: win.vm.run("saveBinary", [selectedFile])
    }
    FileDialog {
        id: textDialog
        title: qsTrId("hex_editor.save_as_text")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "txt"
        gameFileEnding: ".txt"
        nameFilters: [qsTrId("hex_editor.text_files_txt"), qsTrId("common.all_files")]
        onAccepted: win.vm.run("saveText", [selectedFile])
    }
    FileDialog {
        id: importDialog
        title: qsTrId("hex_editor.import_as_binary")
        currentFolder: win.folderOf ? win.folderOf("Tools") : ""
        nameFilters: [qsTrId("hex_editor.binary_files_bin_dat"), qsTrId("common.all_files")]
        onAccepted: { const r = win.vm.run("importBinary", [selectedFile]); if (r === undefined && win.vm.error !== "") win.tell(qsTrId("hex_editor.hex_editor"), win.vm.error) }
    }
    FileDialog {
        id: tableDialog
        title: qsTrId("hex_editor.load_tbl_file")
        currentFolder: win.folderOf ? win.folderOf("Tools") : ""
        nameFilters: [qsTrId("hex_editor.text_table_files_tbl"), qsTrId("common.all_files")]
        onAccepted: win.vm.run("loadTable", [selectedFile])
    }

    // a menu item showing its own shortcut, as Chimera's menus do
    Component {
        id: shortcutItem
        MenuItem {
            id: item
            readonly property string keys: action && typeof action.shortcut === "string" ? action.shortcut
                : action && action.keysShown ? action.keysShown : ""
            rightPadding: padding + (keys !== "" ? keyLabel.implicitWidth + 24 : 0)
            Label {
                id: keyLabel
                text: item.keys
                anchors.right: parent.right
                anchors.rightMargin: item.padding
                anchors.verticalCenter: parent.verticalCenter
                color: item.highlighted ? item.palette.highlightedText : item.palette.text
            }
        }
    }

    Action { id: copyAction; text: qsTrId("hex_editor.copy"); icon.source: "qrc:/chimera/icons/Duplicate.png"; shortcut: "Ctrl+C"; enabled: win.anyHighlighted; onTriggered: if (win.clipboard) win.clipboard.setText(win.vm.run("copyText")) }
    Action { id: exportAction; text: qsTrId("hex_editor.export"); shortcut: "Ctrl+E"; enabled: win.anyHighlighted; onTriggered: if (win.clipboard) win.clipboard.setText(win.vm.run("exportText")) }
    Action {
        id: pasteAction; text: qsTrId("hex_editor.paste"); icon.source: "qrc:/chimera/icons/Paste.png"; shortcut: "Ctrl+V"
        enabled: win.vm.writable && win.anyHighlighted
        onTriggered: if (win.clipboard) win.vm.run("paste", [win.clipboard.text()])
    }
    Action { id: findAction; text: qsTrId("hex_editor.find_dialog"); shortcut: "Ctrl+F"; onTriggered: find.start() }
    Action { id: findNextAction; text: qsTrId("hex_editor.find_next"); shortcut: "F3"; enabled: win.vm.findString !== ""; onTriggered: win.vm.run("findNext") }
    Action { id: findPrevAction; text: qsTrId("hex_editor.find_prev"); shortcut: "F2"; enabled: win.vm.findString !== ""; onTriggered: win.vm.run("findPrev") }
    Action { id: goToAction; text: qsTrId("hex_editor.go_to_address_dialog"); shortcut: "Ctrl+G"; onTriggered: goTo.open() }
    Action {
        id: watchAction; property string keysShown: "Ctrl+W"; text: qsTrId("hex_editor.add_to_ram_watch"); icon.source: "qrc:/chimera/icons/Find.png"
        enabled: win.anyHighlighted
        onTriggered: { if (win.vm.run("addToRamWatch") > 0 && win.showRamWatch) win.showRamWatch() }
    }
    // frozen addresses: written before every frame, for this session only
    Action {
        id: freezeAction; property string keysShown: "Space"
        text: win.vm.pickedFrozen ? qsTrId("hex_editor.unfreeze_address") : qsTrId("common.freeze_address")
        icon.source: win.vm.pickedFrozen ? "qrc:/chimera/icons/Unfreeze.png" : "qrc:/chimera/icons/Freeze.png"
        enabled: win.anyHighlighted && win.vm.writable
        onTriggered: win.vm.run("toggleFreeze")
    }
    Action { id: unfreezeAllAction; text: qsTrId("hex_editor.unfreeze_all"); icon.source: "qrc:/chimera/icons/Unfreeze.png"; shortcut: "Shift+Del"; enabled: win.vm.anyFrozen; onTriggered: win.vm.run("unfreezeAll") }
    Action {
        id: pokeAction; text: qsTrId("hex_editor.poke_address"); icon.source: "qrc:/chimera/icons/poke.png"; shortcut: "Ctrl+P"
        enabled: win.vm.writable && win.anyHighlighted
        onTriggered: poke.start(JSON.parse(win.vm.run("pokeInfo")))
    }

    menuBar: MenuBar {
        background: Rectangle { color: win.menuBarColour }
        Menu {
            title: qsTrId("common.file")
            delegate: shortcutItem
            Action { text: qsTrId("hex_editor.save_as_binary_dialog"); shortcut: "Ctrl+Shift+S"; onTriggered: binaryDialog.open() }
            Action { text: qsTrId("hex_editor.save_as_text_dialog"); onTriggered: textDialog.open() }
            Action { text: qsTrId("hex_editor.import_as_binary_dialog"); shortcut: "Ctrl+I"; onTriggered: importDialog.open() }
            MenuDivider {}
            Action { text: qsTrId("hex_editor.load_tbl_file_mnemonic"); onTriggered: tableDialog.open() }
            Action { text: qsTrId("hex_editor.close_tbl_file"); enabled: win.vm.tableFile !== ""; onTriggered: win.vm.run("closeTable") }
            Menu {
                delegate: MenuEntry {}
                id: recentMenu
                title: qsTrId("common.recent")
                MenuItem { text: qsTrId("common.none"); enabled: false; visible: win.vm.recentTables === ""; height: visible ? implicitHeight : 0 }
                Instantiator {
                    model: win.vm.recentTables === "" ? [] : win.vm.recentTables.split("\n")
                    delegate: MenuItem {
                        required property string modelData
                        text: modelData
                        onTriggered: win.vm.run("loadTable", [modelData])
                    }
                    onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => recentMenu.removeItem(object)
                }
            }
            MenuDivider {}
            Action { property string keysShown: "Alt+F4"; text: qsTrId("common.close_mnemonic"); onTriggered: win.close() }
        }
        Menu {
            title: qsTrId("common.edit")
            delegate: shortcutItem
            MenuItem { action: copyAction }
            MenuItem { action: exportAction }
            MenuItem { action: pasteAction }
            MenuDivider {}
            MenuItem { action: findAction }
            MenuItem { action: findNextAction }
            MenuItem { action: findPrevAction }
        }
        Menu {
            title: qsTrId("hex_editor.options")
            delegate: shortcutItem
            Menu {
                delegate: MenuEntry {}
                id: domainMenu
                title: qsTrId("hex_editor.memory_domains")
                Instantiator {
                    model: win.vm.domains === "" ? [] : win.vm.domains.split(",")
                    delegate: MenuItem {
                        required property string modelData
                        text: modelData
                        checkable: true
                        checked: win.vm.domain === modelData
                        onTriggered: win.vm.domain = modelData
                    }
                    onObjectAdded: (index, object) => domainMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => domainMenu.removeItem(object)
                }
            }
            Menu {
                delegate: MenuEntry {}
                title: qsTrId("hex_editor.data_size")
                MenuItem { text: qsTrId("common.1_byte"); checkable: true; checked: win.vm.size === 1; onTriggered: win.vm.size = 1 }
                MenuItem { text: qsTrId("common.2_byte"); checkable: true; checked: win.vm.size === 2; onTriggered: win.vm.size = 2 }
                MenuItem { text: qsTrId("common.4_byte"); checkable: true; checked: win.vm.size === 4; onTriggered: win.vm.size = 4 }
            }
            MenuItem { text: qsTrId("hex_editor.big_endian"); checkable: true; checked: win.vm.bigEndian; onTriggered: win.vm.bigEndian = checked }
            MenuDivider {}
            MenuItem { action: goToAction }
            MenuItem { action: watchAction }
            MenuItem { action: freezeAction }
            MenuItem { action: unfreezeAllAction }
            MenuItem { action: pokeAction }
        }
        Menu {
            delegate: MenuEntry {}
            title: qsTrId("common.settings")
            Menu {
                delegate: MenuEntry {}
                title: qsTrId("hex_editor.custom_colors")
                MenuItem { text: qsTrId("hex_editor.set_colors"); onTriggered: { colorsWindow.show(); colorsWindow.raise() } }
                MenuDivider {}
                MenuItem { text: qsTrId("hex_editor.reset_to_default"); onTriggered: win.vm.run("resetColors") }
            }
            MenuDivider {}
            MenuItem { text: qsTrId("common.save_window_position"); checkable: true; checked: windowOptions.savePosition; onTriggered: windowOptions.set("save", checked) }
            MenuItem {
                text: qsTrId("common.stay_on_top"); checkable: true; checked: windowOptions.onTop
                visible: windowOptions.canStayOnTop; height: visible ? implicitHeight : 0
                onTriggered: windowOptions.set("top", checked)
            }
            MenuItem { text: qsTrId("common.float_from_main_window"); checkable: true; checked: windowOptions.floating; onTriggered: windowOptions.set("float", checked) }
            MenuItem { text: qsTrId("common.restore_defaults_mnemonic"); onTriggered: { win.vm.run("resetColors"); win.vm.size = 1; windowOptions.restore() } }
        }
    }

    // the values' right-click menu
    Menu {
        id: viewerMenu
        delegate: shortcutItem
        MenuItem { action: copyAction; visible: win.anyHighlighted; height: visible ? implicitHeight : 0 }
        MenuItem { action: exportAction }
        MenuItem { action: pasteAction; visible: win.vm.writable; height: visible ? implicitHeight : 0 }
        MenuItem { action: freezeAction; visible: win.anyHighlighted && win.vm.writable; height: visible ? implicitHeight : 0 }
        MenuItem { action: unfreezeAllAction; visible: win.vm.anyFrozen; height: visible ? implicitHeight : 0 }
        Action { property string keysShown: "Ctrl+W"; text: qsTrId("hex_editor.add_to_ram_watch_mnemonic"); icon.source: "qrc:/chimera/icons/Find.png"; enabled: watchAction.enabled; onTriggered: watchAction.trigger() }
        Action { property string keysShown: "Ctrl+P"; text: qsTrId("hex_editor.poke_address"); icon.source: "qrc:/chimera/icons/poke.png"; enabled: pokeAction.enabled; onTriggered: pokeAction.trigger() }
        MenuDivider {}
        Action { property string keysShown: "+"; text: qsTrId("hex_editor.increment"); enabled: win.anyHighlighted && win.vm.writable; onTriggered: win.vm.run("increment") }
        Action { property string keysShown: "-"; text: qsTrId("hex_editor.decrement"); enabled: win.anyHighlighted && win.vm.writable; onTriggered: win.vm.run("decrement") }
        MenuDivider {}
        Action { property string keysShown: "Ctrl+G"; text: qsTrId("hex_editor.go_to_address_dialog"); onTriggered: goTo.open() }
    }

    GroupBox {
        id: box
        objectName: "hexBox"
        anchors.fill: parent
        anchors.margins: 4
        title: win.vm.boxTitle
        background: Rectangle {
            y: box.topPadding - box.bottomPadding
            width: parent.width
            height: parent.height - box.topPadding + box.bottomPadding
            color: win.backColour
            border.color: palette.mid
        }

        Item {
            id: view
            anchors.fill: parent
            focus: true
            clip: true
            onHeightChanged: win.vm.visibleLines = Math.max(1, Math.floor((height - win.lineHeight) / win.lineHeight))
            Component.onCompleted: win.vm.visibleLines = Math.max(1, Math.floor((height - win.lineHeight) / win.lineHeight))

            Keys.onPressed: (event) => {
                const shift = (event.modifiers & Qt.ShiftModifier) !== 0
                const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
                const keys = {[Qt.Key_Up]: "Up", [Qt.Key_Down]: "Down", [Qt.Key_Left]: "Left", [Qt.Key_Right]: "Right",
                              [Qt.Key_PageUp]: "PageUp", [Qt.Key_PageDown]: "PageDown", [Qt.Key_Home]: "Home", [Qt.Key_End]: "End",
                              [Qt.Key_Tab]: "Tab", [Qt.Key_Backtab]: "Tab", [Qt.Key_Escape]: "Escape"}
                if (keys[event.key] !== undefined) {
                    win.vm.run("key", [keys[event.key], shift || event.key === Qt.Key_Backtab])
                    event.accepted = true
                } else if (event.key === Qt.Key_Plus) { win.vm.run("increment"); event.accepted = true }
                else if (event.key === Qt.Key_Space) { freezeAction.trigger(); event.accepted = true }
                else if (event.key === Qt.Key_Delete) { win.vm.run(shift ? "unfreezeAll" : "unfreezeHighlighted"); event.accepted = true }
                else if (event.key === Qt.Key_Minus) { win.vm.run("decrement"); event.accepted = true }
                else if (ctrl && event.key === Qt.Key_W) { watchAction.trigger(); event.accepted = true }
                else if (!ctrl && !(event.modifiers & Qt.AltModifier) && /^[0-9a-fA-F]$/.test(event.text)) {
                    win.vm.run("type", [event.text])
                    event.accepted = true
                }
            }

            // the column numbers over the values
            Text {
                x: win.addressWidth
                text: win.vm.header
                color: win.textColour
                font.family: "monospace"
            }

            Column {
                y: win.lineHeight
                Repeater {
                    model: win.vm.rows("lines")
                    delegate: Row {
                        id: line
                        required property var model
                        required property int index
                        height: win.lineHeight
                        Text {
                            width: win.addressWidth
                            text: line.model.address + " |"
                            color: win.textColour
                            font.family: "monospace"
                        }
                        Repeater {
                            model: 16 / win.vm.size
                            delegate: Rectangle {
                                required property int index
                                readonly property int kind: line.model["h" + index] || 0
                                readonly property bool frozen: line.model["f" + index] === true
                                width: win.cellWidth
                                height: win.lineHeight
                                // as Chimera's: a frozen value in the Freeze colour, boxed; highlighted too, the Freeze Highlight colour
                                color: frozen ? (kind === 1 ? win.colour("HighlightFreeze", "HexHighlightFreeze", "#EE82EE") : win.colour("Freeze", "HexFreeze", "#ADD8E6"))
                                     : kind === 1 ? win.highlightColour : kind === 2 ? win.secondaryColour : "transparent"
                                border.color: kind !== 0 || frozen ? win.textColour : "transparent"
                                border.width: kind !== 0 || frozen ? 1 : 0
                                Text {
                                    x: 1
                                    text: line.model["v" + parent.index]
                                    color: win.vm.writable ? win.textColour : win.themeColor("MutedText", "gray")
                                    font.family: "monospace"
                                }
                            }
                        }
                        Text { text: "| "; color: win.textColour; font.family: "monospace" }
                        Repeater {
                            model: 16
                            delegate: Rectangle {
                                required property int index
                                readonly property int kind: line.model["th" + index] || 0
                                width: win.charWidth
                                height: win.lineHeight
                                color: kind === 1 ? win.highlightColour : kind === 2 ? win.secondaryColour : "transparent"
                                Text { text: line.model["t" + parent.index] || ""; color: win.textColour; font.family: "monospace" }
                            }
                        }
                    }
                }
            }

            // a click picks a value (Ctrl another beside it, Shift every one to it); a drag picks them all
            MouseArea {
                x: win.addressWidth
                y: win.lineHeight
                width: win.valuesWidth
                height: parent.height - y
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                function addressAt(mx, my) {
                    const row = Math.floor(my / win.lineHeight)
                    const col = Math.floor(mx / win.cellWidth)
                    if (row < 0 || col < 0 || col >= 16 / win.vm.size) return -1
                    return (win.vm.topLine + row) * 16 + col * win.vm.size
                }
                onPressed: (mouse) => {
                    view.forceActiveFocus()
                    if (mouse.button === Qt.RightButton) { viewerMenu.popup(); return }
                    const at = addressAt(mouse.x, mouse.y)
                    if (at < 0) return
                    win.vm.run("click", [at, (mouse.modifiers & Qt.ControlModifier) ? "toggle" : (mouse.modifiers & Qt.ShiftModifier) ? "range" : "only"])
                }
                onPositionChanged: (mouse) => {
                    if (!(mouse.buttons & Qt.LeftButton)) return
                    const at = addressAt(mouse.x, mouse.y)
                    if (at >= 0) win.vm.run("click", [at, "range"])
                }
            }
            WheelHandler {
                onWheel: (event) => {
                    const step = event.angleDelta.y > 0 ? -1 : 1
                    win.vm.topLine = Math.max(0, Math.min(win.vm.lineCount - win.vm.visibleLines, win.vm.topLine + step))
                }
            }
            ScrollBar {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                policy: ScrollBar.AlwaysOn
                size: win.vm.lineCount > 0 ? Math.min(1, win.vm.visibleLines / win.vm.lineCount) : 1
                position: win.vm.lineCount > 0 ? win.vm.topLine / win.vm.lineCount : 0
                onPositionChanged: if (pressed) win.vm.topLine = Math.round(position * win.vm.lineCount)
            }
        }
    }

    RamPoke { id: poke; vm: win.vm; tell: win.tell }

    // Go to Address: a hexadecimal value
    ApplicationWindow {
        id: goTo
        title: qsTrId("hex_editor.go_to_address")
        width: 280
        height: 120
        modality: Qt.ApplicationModal
        function open() { goToText.text = ""; goTo.show(); goTo.raise(); goToText.forceActiveFocus() }
        function accept() {
            if (/^[0-9a-fA-F]+$/.test(goToText.text)) win.vm.run("goTo", [parseInt(goToText.text, 16)])
            goTo.close()
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            Label { text: qsTrId("hex_editor.enter_a_hexadecimal_value") }
            TextField { id: goToText; Layout.fillWidth: true; font.family: "monospace"; onAccepted: goTo.accept() }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button { text: qsTrId("common.ok"); onClicked: goTo.accept() }
                Button { text: qsTrId("common.cancel"); onClicked: goTo.close() }
            }
        }
    }

    // Find: bytes in hex, or text, looked for after or before the address highlighted
    ApplicationWindow {
        id: find
        objectName: "hexFindWindow"
        title: qsTrId("hex_editor.find")
        width: 260
        height: 130
        function start() {
            findBox.text = win.vm.run("findValues")
            find.show()
            find.raise()
            findBox.forceActiveFocus()
        }
        function bytes() { return textRadio.checked ? win.vm.run("textBytes", [findBox.text]) : findBox.text }
        function next() { win.vm.run("findNext", [find.bytes()]); find.close() }
        function prev() { win.vm.run("findPrev", [find.bytes()]); find.close() }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            TextField {
                id: findBox
                Layout.fillWidth: true
                font.family: "monospace"
                font.capitalization: hexRadio.checked ? Font.AllUppercase : Font.MixedCase
                onAccepted: find.next()
                Keys.onEscapePressed: find.close()
            }
            RowLayout {
                RadioButton { id: hexRadio; text: qsTrId("hex_editor.hex"); checked: !win.vm.findText; onToggled: if (checked) win.vm.findText = false }
                RadioButton { id: textRadio; text: qsTrId("hex_editor.text"); checked: win.vm.findText; onToggled: if (checked) win.vm.findText = true }
            }
            RowLayout {
                Button { text: qsTrId("hex_editor.find_prev"); onClicked: find.prev() }
                Button { text: qsTrId("hex_editor.find_next"); highlighted: true; onClicked: find.next() }
            }
        }
    }

    // Colors: Chimera's six, each picked from a colour dialog
    ApplicationWindow {
        id: colorsWindow
        title: qsTrId("hex_editor.colors")
        width: 280
        height: 260
        modality: Qt.ApplicationModal
        property string role: ""
        ColorDialog {
            id: colorDialog
            onAccepted: win.vm.run("setColor", [colorsWindow.role, selectedColor.toString().toUpperCase()])
        }
        GridLayout {
            anchors.fill: parent
            anchors.margins: 10
            columns: 2
            Repeater {
                model: [{role: "Background", label: qsTrId("hex_editor.background_color"), shown: win.backColour},
                        {role: "MenuBar", label: qsTrId("hex_editor.menubar_color"), shown: win.menuBarColour},
                        {role: "Foreground", label: qsTrId("hex_editor.font_color"), shown: win.textColour},
                        {role: "Highlight", label: qsTrId("hex_editor.highlight_color"), shown: win.highlightColour},
                        {role: "Freeze", label: qsTrId("hex_editor.freeze_color"), shown: win.colour("Freeze", "HexFreeze", "#ADD8E6")},
                        {role: "HighlightFreeze", label: qsTrId("hex_editor.freeze_highlight_color"), shown: win.colour("HighlightFreeze", "HexHighlightFreeze", "#EE82EE")}]
                delegate: RowLayout {
                    required property var modelData
                    Layout.columnSpan: 2
                    Rectangle {
                        width: 40; height: 20
                        color: modelData.shown
                        border.color: palette.mid
                        MouseArea {
                            anchors.fill: parent
                            onClicked: { colorsWindow.role = modelData.role; colorDialog.selectedColor = modelData.shown; colorDialog.open() }
                        }
                    }
                    Label { text: modelData.label }
                }
            }
            Button { text: qsTrId("common.ok"); Layout.columnSpan: 2; Layout.alignment: Qt.AlignRight; onClicked: colorsWindow.close() }
        }
    }

    // a write the machine refused (a domain that cannot be written, an address
    // past its end) is said; Chimera's editor said nothing and wrote nothing
    footer: Label {
        visible: text !== ""
        text: win.vm.error
        color: win.themeColor ? win.themeColor("AccentWarning", "#b26a00") : "#b26a00"
        padding: 4
    }
}
