// RAM Search, as Chimera's: its menus (File, Options, Search, Settings,
// Columns) and toolbar; the candidates on the left with their count and
// domain above them; on the right what a search compares with (Compare To /
// By), how (Comparison Operator), the size and display, Search and
// Auto-Search; the message line at the bottom. RAM Poke and Go to Address
// are windows of their own. Layout and bindings only, over the ramSearch
// view model.
import QtQuick
import QtQuick.Controls
import ChimeraDialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "ramSearchWindow"
    required property QtObject vm
    // the main window's: Paths folders, the clipboard, theme colours, RAM Watch and the hex editor
    property var folderOf: null
    property QtObject clipboard: null
    property var themeColor: (role, fallback) => fallback
    property QtObject customize: null
    property QtObject toolWindows: null
    property Window mainWindow: null
    property var showRamWatch: null
    property var viewInHexEditor: null
    title: qsTrId("ram_search.ram_search")
    width: 700
    height: 600

    WindowOptions { id: windowOptions; window: win; tool: "ramSearch"; vm: win.toolWindows; mainWindow: win.mainWindow }
    onVisibleChanged: if (visible) windowOptions.placeBack(false)

    MessageDialog { id: notice; buttons: MessageDialog.Ok }
    function tell(title, text) { notice.title = title; notice.text = text; notice.open() }
    // more rows than anyone picks by hand: asked first, as Chimera asks
    MessageDialog {
        id: manyAsk
        title: qsTrId("ram_search.ram_search")
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        property var then: null
        onButtonClicked: (button) => { const t = manyAsk.then; manyAsk.then = null; if (button === MessageDialog.Ok && t) t() }
    }
    function onPicked(action, then) {
        if (win.vm.selectedCount <= 1000) { then(); return }
        manyAsk.text = qsTrId("ram_search.addresses_are_selected_all_of_them").arg(Number(win.vm.selectedCount).toLocaleString(Qt.locale("en_US"), "f", 0)).arg(action)
        manyAsk.then = then
        manyAsk.open()
    }
    function addToRamWatch() {
        win.onPicked(qsTrId("ram_search.add_to_ram_watch"), () => { if (win.vm.run("addToRamWatch") > 0 && win.showRamWatch) win.showRamWatch() })
    }
    function pokeAddress() {
        if (!win.vm.canPoke) return
        win.onPicked(qsTrId("ram_search.poke"), () => poke.start(JSON.parse(win.vm.run("pokeInfo"))))
    }
    function copyRows() {
        win.onPicked(qsTrId("common.copy"), () => { const t = win.vm.run("selectedLines"); if (t && win.clipboard) win.clipboard.setText(t) })
    }

    FileDialog {
        id: openDialog
        title: qsTrId("ram_search.open_a_watch_list")
        property string how: "open"
        currentFolder: win.folderOf ? win.folderOf("Watch (.wch)") : ""
        nameFilters: [qsTrId("ram_search.watch_files_wch"), qsTrId("common.all_files")]
        onAccepted: win.vm.run("open", [selectedFile, openDialog.how])
    }
    FileDialog {
        id: saveDialog
        title: qsTrId("ram_search.save_the_search_as")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "wch"
        currentFolder: win.folderOf ? win.folderOf("Watch (.wch)") : ""
        nameFilters: [qsTrId("ram_search.watch_files_wch")]
        onAccepted: win.vm.run("save", [selectedFile])
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

    readonly property bool anyRow: win.vm.selectedCount > 0
    Action { id: searchAction; text: qsTrId("ram_search.search_2"); icon.source: "qrc:/chimera/icons/search.png"; enabled: win.vm.searching && !win.vm.autoSearch; onTriggered: win.vm.run("search") }
    Action { id: newSearchAction; text: qsTrId("ram_search.new_search"); icon.source: "qrc:/chimera/icons/restart.png"; enabled: win.vm.domains !== ""; onTriggered: win.vm.run("newSearch") }
    Action { id: undoAction; text: qsTrId("ram_search.undo"); icon.source: "qrc:/chimera/icons/undo.png"; shortcut: "Ctrl+Z"; enabled: win.vm.canUndo; onTriggered: win.vm.run("undo") }
    Action { id: redoAction; text: qsTrId("ram_search.redo_mnemonic"); icon.source: "qrc:/chimera/icons/redo.png"; shortcut: "Ctrl+Y"; enabled: win.vm.canRedo; onTriggered: win.vm.run("redo") }
    Action { id: copyPrevAction; text: qsTrId("ram_search.copy_value_to_prev"); icon.source: "qrc:/chimera/icons/Previous.png"; enabled: win.vm.searching; onTriggered: win.vm.run("copyValueToPrev") }
    Action { id: clearCountsAction; text: qsTrId("ram_search.clear_change_counts_mnemonic"); enabled: win.vm.searching && !win.vm.fast; onTriggered: win.vm.run("clearChangeCounts") }
    Action { id: removeAction; property string keysShown: "Delete"; text: qsTrId("ram_search.remove_selected_2"); icon.source: "qrc:/chimera/icons/Delete.png"; enabled: win.anyRow; onTriggered: win.vm.run("removeSelected") }
    Action { id: goToAction; text: qsTrId("ram_search.go_to_address_dialog"); shortcut: "Ctrl+G"; enabled: win.vm.searching; onTriggered: goTo.open() }
    Action { id: watchAction; text: qsTrId("ram_search.add_to_ram_watch_mnemonic"); icon.source: "qrc:/chimera/icons/Find.png"; shortcut: "Ctrl+W"; enabled: win.anyRow; onTriggered: win.addToRamWatch() }
    Action { id: pokeAction; text: qsTrId("ram_search.poke_address_mnemonic"); icon.source: "qrc:/chimera/icons/poke.png"; shortcut: "Ctrl+P"; enabled: win.vm.canPoke; onTriggered: win.pokeAddress() }
    // frozen addresses: written before every frame, for this session only
    Action {
        id: freezeAction; text: qsTrId("ram_search.freeze_address"); icon.source: "qrc:/chimera/icons/Freeze.png"; shortcut: "Ctrl+F"
        enabled: win.vm.canPoke
        onTriggered: win.onPicked(qsTrId("ram_search.freeze_or_unfreeze"), () => win.vm.run("toggleFreeze"))
    }
    Action { id: selectAllAction; text: qsTrId("ram_search.select_all"); shortcut: "Ctrl+A"; enabled: win.vm.count > 0; onTriggered: win.vm.run("selectAll") }

    menuBar: MenuBar {
        Menu {
            title: qsTrId("common.file")
            delegate: shortcutItem
            Action { text: qsTrId("ram_search.open"); shortcut: "Ctrl+O"; enabled: win.vm.searching; onTriggered: { openDialog.how = "open"; openDialog.open() } }
            Action { text: qsTrId("common.save"); shortcut: "Ctrl+S"; enabled: win.vm.currentFile !== ""; onTriggered: win.vm.run("save") }
            Action { text: qsTrId("ram_search.save_as"); shortcut: "Ctrl+Shift+S"; enabled: win.vm.searching; onTriggered: saveDialog.open() }
            Action { text: qsTrId("ram_search.append_file"); enabled: win.vm.searching; onTriggered: { openDialog.how = "append"; openDialog.open() } }
            Action { text: qsTrId("ram_search.truncate_from_file"); enabled: win.vm.searching; onTriggered: { openDialog.how = "truncate"; openDialog.open() } }
            Menu {
                delegate: MenuEntry {}
                id: recentMenu
                title: qsTrId("common.recent")
                icon.source: "qrc:/chimera/icons/Recent.png"
                MenuItem { text: qsTrId("common.none"); enabled: false; visible: win.vm.recentFiles === ""; height: visible ? implicitHeight : 0 }
                Instantiator {
                    model: win.vm.recentFiles === "" ? [] : win.vm.recentFiles.split("\n")
                    delegate: MenuItem {
                        required property string modelData
                        text: modelData
                        enabled: win.vm.searching
                        onTriggered: win.vm.run("open", [modelData, "open"])
                    }
                    onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => recentMenu.removeItem(object)
                }
            }
            MenuDivider {}
            Action { property string keysShown: "Alt+F4"; text: qsTrId("common.close_mnemonic"); onTriggered: win.close() }
        }
        Menu {
            delegate: MenuEntry {}
            title: qsTrId("ram_search.options")
            Menu {
                delegate: MenuEntry {}
                title: qsTrId("ram_search.mode")
                MenuItem { text: qsTrId("ram_search.detailed"); checkable: true; checked: !win.vm.fast; onTriggered: win.vm.fast = false }
                MenuItem { text: qsTrId("ram_search.fast"); checkable: true; checked: win.vm.fast; onTriggered: win.vm.fast = true }
            }
            Menu {
                delegate: MenuEntry {}
                id: domainMenu
                title: qsTrId("ram_search.memory_domains")
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
                title: qsTrId("ram_search.size")
                MenuItem { text: qsTrId("ram_search.1_byte"); checkable: true; checked: win.vm.size === 1; onTriggered: win.vm.size = 1 }
                MenuItem { text: qsTrId("ram_search.2_byte"); checkable: true; checked: win.vm.size === 2; onTriggered: win.vm.size = 2 }
                MenuItem { text: qsTrId("ram_search.4_byte"); checkable: true; checked: win.vm.size === 4; onTriggered: win.vm.size = 4 }
            }
            MenuItem { text: qsTrId("ram_search.check_mis_aligned"); checkable: true; checked: win.vm.checkMisaligned; onTriggered: win.vm.checkMisaligned = checked }
            MenuDivider {}
            MenuItem { text: qsTrId("ram_search.big_endian"); checkable: true; checked: win.vm.bigEndian; onTriggered: win.vm.bigEndian = checked }
            Menu {
                delegate: MenuEntry {}
                id: displayMenu
                title: qsTrId("ram_search.display_type")
                Instantiator {
                    model: win.vm.displays === "" ? [] : win.vm.displays.split("\n")
                    delegate: MenuItem {
                        required property string modelData
                        required property int index
                        text: modelData
                        checkable: true
                        checked: win.vm.display === index
                        onTriggered: win.vm.display = index
                    }
                    onObjectAdded: (index, object) => displayMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => displayMenu.removeItem(object)
                }
            }
            Menu {
                delegate: MenuEntry {}
                title: qsTrId("ram_search.define_previous_value")
                MenuItem { text: qsTrId("ram_search.last_search"); checkable: true; checked: win.vm.previousType === 1; onTriggered: win.vm.previousType = 1 }
                MenuItem { text: qsTrId("ram_search.previous_frame"); checkable: true; checked: win.vm.previousType === 2; enabled: !win.vm.fast; onTriggered: win.vm.previousType = 2 }
                MenuItem { text: qsTrId("ram_search.original"); checkable: true; checked: win.vm.previousType === 0; onTriggered: win.vm.previousType = 0 }
                MenuItem { text: qsTrId("ram_search.last_change"); checkable: true; checked: win.vm.previousType === 3; enabled: !win.vm.fast; onTriggered: win.vm.previousType = 3 }
            }
        }
        Menu {
            title: qsTrId("ram_search.search_2")
            delegate: shortcutItem
            MenuItem { action: newSearchAction }
            MenuDivider {}
            MenuItem { action: searchAction }
            MenuItem { action: undoAction }
            MenuItem { action: redoAction }
            MenuItem { action: copyPrevAction }
            MenuItem { action: clearCountsAction }
            MenuItem { action: removeAction }
            MenuDivider {}
            MenuItem { action: goToAction }
            MenuItem { action: watchAction }
            MenuItem { action: pokeAction }
            MenuItem { action: freezeAction }
            MenuItem { action: selectAllAction }
            MenuDivider {}
            MenuItem { text: qsTrId("ram_search.clear_undo_history"); enabled: win.vm.canUndo; onTriggered: win.vm.run("clearUndo") }
        }
        Menu {
            delegate: MenuEntry {}
            title: qsTrId("common.settings")
            MenuItem { text: qsTrId("ram_search.preview_mode"); checkable: true; checked: win.vm.previewMode; onTriggered: win.vm.previewMode = checked }
            MenuItem { text: qsTrId("ram_search.auto_search_2"); checkable: true; checked: win.vm.autoSearch; onTriggered: win.vm.autoSearch = checked }
            MenuItem { text: qsTrId("ram_search.auto_search_account_for_lag"); checkable: true; checked: win.vm.autoSearchAccountForLag; onTriggered: win.vm.autoSearchAccountForLag = checked }
            MenuDivider {}
            MenuItem { text: qsTrId("ram_search.always_exclude_ram_watch_list"); checkable: true; checked: win.vm.alwaysExcludeRamWatch; onTriggered: win.vm.alwaysExcludeRamWatch = checked }
            MenuItem { text: qsTrId("ram_search.use_undo_history"); checkable: true; checked: win.vm.useUndoHistory; onTriggered: win.vm.useUndoHistory = checked }
            MenuDivider {}
            MenuItem { text: qsTrId("common.save_window_position"); checkable: true; checked: windowOptions.savePosition; onTriggered: windowOptions.set("save", checked) }
            MenuItem {
                text: qsTrId("common.stay_on_top"); checkable: true; checked: windowOptions.onTop
                visible: windowOptions.canStayOnTop; height: visible ? implicitHeight : 0
                onTriggered: windowOptions.set("top", checked)
            }
            MenuItem { text: qsTrId("common.float_from_main_window"); checkable: true; checked: windowOptions.floating; onTriggered: windowOptions.set("float", checked) }
            MenuItem { text: qsTrId("common.restore_defaults_mnemonic"); onTriggered: { win.vm.run("restoreDefaults"); windowOptions.restore() } }
        }
        Menu {
            delegate: MenuEntry {}
            id: columnsMenu
            title: qsTrId("ram_search.columns")
            Instantiator {
                model: win.columns
                delegate: MenuItem {
                    required property var modelData
                    text: modelData.text + " (" + modelData.name + "Column)"
                    checkable: true
                    checked: win.shown(modelData.key)
                    onTriggered: win.showColumn(modelData.key, checked)
                }
                onObjectAdded: (index, object) => columnsMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => columnsMenu.removeItem(object)
            }
        }
    }

    // Chimera's columns: its key (the view model's sort name), heading, width and the row model's field
    readonly property var columns: [
        {key: "Address", name: "Address", text: qsTrId("ram_columns.address"), width: 78, field: "Address"},
        {key: "Value", name: "Value", text: qsTrId("ram_columns.value"), width: 77, field: "Value"},
        {key: "Prev", name: "Prev", text: qsTrId("ram_columns.prev"), width: 77, field: "Previous"},
        {key: "Changes", name: "Changes", text: qsTrId("ram_columns.changes"), width: 78, field: "Changes"},
        {key: "Diff", name: "Diff", text: qsTrId("ram_columns.diff"), width: 77, field: "Diff"}
    ]
    readonly property var hidden: win.vm.hiddenColumns === "" ? [] : win.vm.hiddenColumns.split(",")
    function shown(key) { return win.hidden.indexOf(key) < 0 }
    function showColumn(key, on) {
        const now = win.hidden.filter((k) => k !== key)
        if (!on) now.push(key)
        win.vm.hiddenColumns = now.join(",")
    }
    readonly property var visibleColumns: win.columns.filter((c) => win.shown(c.key))
    // the grid, as Chimera's list draws it, in the theme's colour: a line at the left of every column but the first
    readonly property color gridColour: win.themeColor("RollGridLines", String(Qt.darker(palette.base, 1.25)))
    readonly property var columnLines: {
        const at = []
        let x = 0
        for (let i = 0; i + 1 < win.visibleColumns.length; i++) { x += win.visibleColumns[i].width; at.push(x) }
        return at
    }

    header: ToolBar {
        RowLayout {
            spacing: 0
            component Tool: ToolButton {
                property string tip: action ? action.text.replace(/&/g, "") : ""
                display: AbstractButton.IconOnly
                ToolTip.visible: hovered
                ToolTip.text: tip
                ToolTip.delay: 500
            }
            Tool { action: searchAction; display: AbstractButton.TextBesideIcon; text: qsTrId("ram_search.search") }
            ToolSeparator {}
            Tool { action: newSearchAction; display: AbstractButton.TextBesideIcon; text: qsTrId("ram_search.new") }
            ToolSeparator {}
            Tool { action: copyPrevAction; tip: qsTrId("ram_search.copy_value_to_previous") }
            ToolButton {
                text: qsTrId("ram_search.c"); enabled: clearCountsAction.enabled; onClicked: clearCountsAction.trigger()
                ToolTip.visible: hovered; ToolTip.text: qsTrId("ram_search.clear_change_counts"); ToolTip.delay: 500
            }
            ToolSeparator {}
            Tool { action: removeAction; tip: qsTrId("common.cut") }
            Tool { action: watchAction; tip: qsTrId("ram_search.watch") }
            Tool { action: pokeAction; tip: qsTrId("ram_search.poke") }
            Tool { action: freezeAction; tip: qsTrId("ram_search.freeze") }
            ToolSeparator {}
            Tool { action: undoAction; tip: qsTrId("ram_search.undo_search") }
            Tool { action: redoAction; tip: qsTrId("ram_search.redo") }
            ToolSeparator { visible: win.vm.rebootNeeded }
            ToolButton {
                objectName: "ramSearchReboot"
                visible: win.vm.rebootNeeded
                icon.source: "qrc:/chimera/icons/reboot.png"
                display: AbstractButton.IconOnly
                ToolTip.visible: hovered
                ToolTip.text: qsTrId("ram_search.a_new_search_needs_to_be")
                onClicked: win.vm.run("newSearch")
            }
            ToolButton {
                objectName: "ramSearchOutOfRange"
                visible: win.vm.outOfRange > 0
                icon.source: "qrc:/chimera/icons/ExclamationRed.png"
                display: AbstractButton.IconOnly
                ToolTip.visible: hovered
                ToolTip.text: qsTrId("ram_search.warning_out_of_range_addresses_in")
                onClicked: win.vm.run("removeOutOfRange")
            }
        }
    }

    footer: Label {
        objectName: "ramSearchMessage"
        text: win.vm.error !== "" ? win.vm.error : win.vm.status
        padding: 4
        elide: Text.ElideRight
    }

    // the list's right-click menu
    Menu {
        id: listMenu
        delegate: shortcutItem
        MenuItem { action: searchAction }
        Action { text: qsTrId("ram_search.start_new_search"); onTriggered: win.vm.run("newSearch") }
        MenuDivider {}
        Action { property string keysShown: "Del"; text: qsTrId("ram_search.remove_selected"); enabled: win.anyRow; onTriggered: win.vm.run("removeSelected") }
        Action { property string keysShown: "Ctrl+W"; text: qsTrId("ram_search.add_to_ram_watch"); enabled: win.anyRow; onTriggered: win.addToRamWatch() }
        Action { property string keysShown: "Ctrl+P"; text: qsTrId("ram_search.poke_address"); enabled: win.vm.canPoke; onTriggered: win.pokeAddress() }
        MenuItem {
            action: Action {
                property string keysShown: "Ctrl+F"
                text: win.vm.selectionFrozen ? qsTrId("ram_search.unfreeze_address") : qsTrId("common.freeze_address")
                icon.source: win.vm.selectionFrozen ? "qrc:/chimera/icons/Unfreeze.png" : "qrc:/chimera/icons/Freeze.png"
                onTriggered: win.onPicked(qsTrId("ram_search.freeze_or_unfreeze"), () => win.vm.run("toggleFreeze"))
            }
            visible: win.vm.canPoke; height: visible ? implicitHeight : 0
        }
        MenuItem {
            text: qsTrId("ram_search.unfreeze_all"); icon.source: "qrc:/chimera/icons/Unfreeze.png"
            visible: win.vm.anyFrozen; height: visible ? implicitHeight : 0
            onTriggered: win.vm.run("unfreezeAll")
        }
        MenuDivider {}
        MenuItem {
            text: qsTrId("ram_search.view_in_hex_editor")
            visible: win.anyRow && win.viewInHexEditor !== null; height: visible ? implicitHeight : 0
            onTriggered: win.onPicked(qsTrId("ram_search.open_in_the_hex_editor"), () => {
                const info = JSON.parse(win.vm.run("pokeInfo"))
                win.viewInHexEditor(info.domain, parseInt(info.addresses.split(",")[0], 16))
            })
        }
        MenuDivider {}
        Action { text: qsTrId("ram_search.clear_preview"); onTriggered: win.vm.run("clearPreview") }
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        ColumnLayout {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 200
            spacing: 0
            RowLayout {
                Label { objectName: "ramSearchTotal"; text: win.vm.totalText; padding: 4 }
                Item { Layout.fillWidth: true }
                Label { text: win.vm.searchedDomain; padding: 4 }
            }
            // the headings: a click sorts by the column, again the other way round
            Row {
                Layout.fillWidth: true
                Repeater {
                    model: win.visibleColumns
                    delegate: Button {
                        required property var modelData
                        required property int index
                        text: modelData.text
                        flat: true
                        padding: 2
                        width: index === win.visibleColumns.length - 1 ? Math.max(modelData.width, list.width - x) : modelData.width
                        onClicked: win.vm.run("sort", [modelData.key])
                    }
                }
            }
            ListView {
                id: list
                objectName: "ramSearchList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                focus: true
                model: win.vm.rows("results")
                ScrollBar.vertical: ScrollBar {}
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Delete) { win.vm.run("removeSelected"); event.accepted = true }
                    else if (event.matches(StandardKey.Copy)) { win.copyRows(); event.accepted = true }
                }
                delegate: Rectangle {
                    id: row
                    required property int index
                    required property var model
                    width: list.width
                    height: 22
                    color: row.model.selected ? palette.highlight
                         : row.model.invalid ? win.themeColor("RowInvalid", "#FFDAB9")
                         : row.model.frozen ? (row.model.preview ? win.themeColor("RowExcludedActive", "#E6E6FA") : win.themeColor("RowActive", "#E0FFFF"))
                         : row.model.preview ? win.themeColor("RowExcluded", "#FFC0CB")
                         : win.themeColor("RowDefault", palette.base)
                    // the grid, as Chimera's list draws it: a line under every row and between the columns
                    Repeater {
                        model: win.columnLines
                        delegate: Rectangle { required property real modelData; x: modelData; width: 1; height: row.height; color: win.gridColour }
                    }
                    Rectangle { y: row.height - 1; width: row.width; height: 1; color: win.gridColour }
                    Row {
                        anchors.fill: parent
                        Repeater {
                            model: win.visibleColumns
                            delegate: Label {
                                required property var modelData
                                required property int index
                                width: index === win.visibleColumns.length - 1 ? Math.max(modelData.width, list.width - x) : modelData.width
                                height: row.height
                                leftPadding: 3
                                verticalAlignment: Text.AlignVCenter
                                text: { const v = row.model[modelData.field]; return v === undefined || v === null ? "" : String(v) }
                                font.family: "monospace"
                                elide: Text.ElideRight
                                color: row.model.selected ? palette.highlightedText : palette.text
                            }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onPressed: (mouse) => {
                            list.forceActiveFocus()
                            if (mouse.button === Qt.RightButton) {
                                if (!row.model.selected) win.vm.run("select", [row.index, "only"])
                                listMenu.popup()
                                return
                            }
                            win.vm.run("select", [row.index, (mouse.modifiers & Qt.ShiftModifier) ? "range"
                                : (mouse.modifiers & Qt.ControlModifier) ? "toggle" : "only"])
                        }
                    }
                }
                MouseArea {
                    z: -1
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPressed: (mouse) => {
                        list.forceActiveFocus()
                        win.vm.run("select", [-1, "none"])
                        if (mouse.button === Qt.RightButton) listMenu.popup()
                    }
                }
            }
        }

        ColumnLayout {
            SplitView.preferredWidth: 280
            SplitView.minimumWidth: 250
            GroupBox {
                title: qsTrId("ram_search.compare_to_by")
                Layout.fillWidth: true
                GridLayout {
                    columns: 2
                    rowSpacing: 0
                    RadioButton { text: qsTrId("ram_search.previous_value"); checked: win.vm.compare === 0; onClicked: win.vm.compare = 0; Layout.columnSpan: 2 }
                    RadioButton { text: qsTrId("ram_search.specific_value"); checked: win.vm.compare === 1; onClicked: win.vm.compare = 1 }
                    TextField {
                        objectName: "specificValueBox"; Layout.preferredWidth: 70; enabled: win.vm.compare === 1; font.family: "monospace"
                        text: win.vm.specificValue; onTextEdited: win.vm.specificValue = text
                    }
                    RadioButton { text: qsTrId("ram_search.specific_address"); checked: win.vm.compare === 2; onClicked: win.vm.compare = 2 }
                    TextField {
                        Layout.preferredWidth: 70; enabled: win.vm.compare === 2; font.family: "monospace"
                        text: win.vm.specificAddress; onTextEdited: win.vm.specificAddress = text
                    }
                    RadioButton { text: qsTrId("ram_search.specific_changes"); checked: win.vm.compare === 3; enabled: !win.vm.fast; onClicked: win.vm.compare = 3 }
                    TextField {
                        Layout.preferredWidth: 70; enabled: win.vm.compare === 3 && !win.vm.fast; font.family: "monospace"
                        text: win.vm.specificChanges; onTextEdited: win.vm.specificChanges = text
                    }
                    RadioButton { text: qsTrId("ram_search.difference_of"); checked: win.vm.compare === 4; enabled: !win.vm.fast; onClicked: win.vm.compare = 4 }
                    TextField {
                        Layout.preferredWidth: 70; enabled: win.vm.compare === 4; font.family: "monospace"
                        text: win.vm.difference; onTextEdited: win.vm.difference = text
                    }
                }
            }
            GroupBox {
                title: qsTrId("ram_search.comparison_operator")
                Layout.fillWidth: true
                GridLayout {
                    columns: 2
                    rowSpacing: 0
                    // Chimera's order, top to bottom
                    RadioButton { text: qsTrId("ram_search.equal_to"); checked: win.vm.op === 0; onClicked: win.vm.op = 0; Layout.columnSpan: 2 }
                    RadioButton { text: qsTrId("ram_search.not_equal_to"); checked: win.vm.op === 5; onClicked: win.vm.op = 5; Layout.columnSpan: 2 }
                    RadioButton { text: qsTrId("ram_search.less_than"); checked: win.vm.op === 3; onClicked: win.vm.op = 3; Layout.columnSpan: 2 }
                    RadioButton { text: qsTrId("ram_search.greater_than"); checked: win.vm.op === 1; onClicked: win.vm.op = 1; Layout.columnSpan: 2 }
                    RadioButton { text: qsTrId("ram_search.less_than_or_equal_to"); checked: win.vm.op === 4; onClicked: win.vm.op = 4; Layout.columnSpan: 2 }
                    RadioButton { text: qsTrId("ram_search.greater_than_or_equal_to"); checked: win.vm.op === 2; onClicked: win.vm.op = 2; Layout.columnSpan: 2 }
                    RadioButton { text: qsTrId("ram_search.different_by"); checked: win.vm.op === 6; onClicked: win.vm.op = 6 }
                    TextField {
                        Layout.preferredWidth: 70; enabled: win.vm.op === 6; font.family: "monospace"
                        text: win.vm.differentBy; onTextEdited: win.vm.differentBy = text
                    }
                }
            }
            GridLayout {
                columns: 2
                Label { text: qsTrId("common.size") }
                Label { text: qsTrId("ram_search.display") }
                ComboBox {
                    Layout.fillWidth: true
                    model: [qsTrId("common.1_byte"), qsTrId("common.2_byte"), qsTrId("common.4_byte")]
                    currentIndex: win.vm.size === 4 ? 2 : win.vm.size - 1
                    onActivated: (i) => win.vm.size = [1, 2, 4][i]
                }
                ComboBox {
                    Layout.fillWidth: true
                    model: win.vm.displays === "" ? [] : win.vm.displays.split("\n")
                    currentIndex: win.vm.display
                    onActivated: (i) => win.vm.display = i
                }
            }
            RowLayout {
                Button { text: qsTrId("ram_search.search_2"); highlighted: true; enabled: searchAction.enabled; onClicked: win.vm.run("search") }
                CheckBox { text: qsTrId("ram_search.auto_search"); checked: win.vm.autoSearch; onToggled: win.vm.autoSearch = checked }
            }
            Item { Layout.fillHeight: true }
        }
    }

    RamPoke { id: poke; vm: win.vm; tell: win.tell }

    // Go to Address: a candidate's row picked, asked again until one is found, as Chimera's
    ApplicationWindow {
        id: goTo
        title: qsTrId("ram_search.go_to_address")
        width: 280
        height: 120
        modality: Qt.ApplicationModal
        function open() { goToText.text = ""; goTo.show(); goTo.raise(); goToText.forceActiveFocus() }
        function accept() {
            const row = win.vm.run("goTo", [goToText.text])
            if (row >= 0) { list.positionViewAtIndex(row, ListView.Center); goTo.close() }
            else goToText.selectAll()
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            Label { text: qsTrId("ram_search.enter_a_hexadecimal_value") }
            TextField { id: goToText; Layout.fillWidth: true; font.family: "monospace"; onAccepted: goTo.accept() }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button { text: qsTrId("common.ok"); onClicked: goTo.accept() }
                Button { text: qsTrId("common.cancel"); onClicked: goTo.close() }
            }
        }
    }
}
