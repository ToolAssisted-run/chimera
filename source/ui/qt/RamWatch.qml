// RAM Watch, as Chimera's: its menus (File, Watches, Settings, Columns), its
// toolbar, the count of watches over the list, the list with its columns,
// and the status line with the warning for addresses past their domain's
// end. New, Edit and Duplicate Watch share one editor window; RAM Poke,
// Edit Separator and Add Game Properties are windows of their own. Layout
// and bindings only, over the ramWatch view model.
import QtQuick
import QtQuick.Controls
import ChimeraDialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "ramWatchWindow"
    required property QtObject vm
    // the main window's: Paths folders, the clipboard, theme colours, a game core's properties, the hex editor
    property var folderOf: null
    property QtObject clipboard: null
    property var themeColor: (role, fallback) => fallback
    property QtObject customize: null
    property QtObject toolWindows: null
    property Window mainWindow: null
    property QtObject gameProperties: null
    property var viewInHexEditor: null
    title: qsTrId("ram_watch.ram_watch")
    width: 560
    height: 480

    WindowOptions { id: windowOptions; window: win; tool: "ramWatch"; vm: win.toolWindows; mainWindow: win.mainWindow }
    onVisibleChanged: if (visible) windowOptions.placeBack(false)

    // Chimera asks before a list with changes is let go
    property var afterAsk: null
    property bool closingAsked: false
    function leaveList(then) {
        if (!win.vm.changed) { then(); return }
        win.afterAsk = then
        askSave.open()
    }
    MessageDialog {
        id: askSave
        title: qsTrId("common.closing_with_unsaved_changes")
        text: qsTrId("ram_watch.save_watch_file")
        buttons: MessageDialog.Yes | MessageDialog.No | MessageDialog.Cancel
        onButtonClicked: (button) => {
            const then = win.afterAsk
            win.afterAsk = null
            if (button === MessageDialog.Cancel) return
            if (button === MessageDialog.Yes) {
                if (win.vm.path === "") { saveDialog.afterSave = then; saveDialog.open(); return }
                win.vm.run("save")
            }
            if (then) then()
        }
    }
    onClosing: (close) => {
        if (win.closingAsked) { win.closingAsked = false; return }
        if (win.vm.changed) {
            close.accepted = false
            win.leaveList(() => { win.closingAsked = true; win.close() })
        }
    }

    FileDialog {
        id: openDialog
        title: qsTrId("ram_watch.open_a_watch_list")
        property bool append: false
        currentFolder: win.folderOf ? win.folderOf("Watch (.wch)") : ""
        nameFilters: [qsTrId("ram_watch.watch_files_wch"), qsTrId("common.all_files")]
        onAccepted: win.vm.run("open", [selectedFile, openDialog.append])
    }
    FileDialog {
        id: saveDialog
        title: qsTrId("ram_watch.save_the_watch_list_as")
        property var afterSave: null
        fileMode: FileDialog.SaveFile
        defaultSuffix: "wch"
        currentFolder: win.folderOf ? win.folderOf("Watch (.wch)") : ""
        nameFilters: [qsTrId("ram_watch.watch_files_wch")]
        onAccepted: {
            win.vm.run("save", [selectedFile])
            const then = saveDialog.afterSave
            saveDialog.afterSave = null
            if (then && !win.vm.changed) then()
        }
        onRejected: saveDialog.afterSave = null
    }
    MessageDialog {
        id: notice
        buttons: MessageDialog.Ok
    }
    function tell(title, text) { notice.title = title; notice.text = text; notice.open() }
    MessageDialog {
        id: purgeAsk
        title: qsTrId("ram_watch.purge_duplicates")
        text: qsTrId("ram_watch.this_will_go_through_each_watch")
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onButtonClicked: (button) => { if (button === MessageDialog.Ok) win.vm.run("purgeDuplicates") }
    }

    // what the rows picked allow, as Chimera's menus ask it
    readonly property bool anyRow: win.vm.selectedCount > 0
    readonly property bool anyWatch: win.vm.selectedWatches > 0
    function editWatch(duplicate) {
        const info = JSON.parse(win.vm.run("editInfo", [duplicate ? "duplicate" : "edit"]))
        if (win.vm.selectedWatches === 0) {
            // separators picked: their text, as Chimera's Edit Separator asks it
            if (!duplicate && win.vm.selectedCount > 0) separatorPrompt.open()
            return
        }
        if (info.property) {
            win.tell(qsTrId("ram_watch.game_property"), qsTrId("ram_watch.a_game_property_s_watch_reads"))
            return
        }
        editor.start(duplicate ? "duplicate" : "edit", info)
    }
    function pokeWatch() {
        if (!win.anyWatch) return
        const info = JSON.parse(win.vm.run("pokeInfo"))
        if (info.mixed) { win.tell(qsTrId("ram_watch.unsupported_options"), qsTrId("ram_watch.ram_poke_does_not_support_mixed")); return }
        poke.start(info)
    }
    // On Double-Clicking a Watch, and Enter
    function openWatch() { if (win.vm.doubleClickToPoke) win.pokeWatch(); else win.editWatch(false) }
    function copyWatches() { const t = win.vm.run("copySelected"); if (t && win.clipboard) win.clipboard.setText(t) }
    function pasteWatches() { if (win.clipboard) win.vm.run("paste", [win.clipboard.text()]) }

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

    Action { id: newListAction; text: qsTrId("ram_watch.new_list"); icon.source: "qrc:/chimera/icons/NewFile.png"; shortcut: "Ctrl+N"; onTriggered: win.leaveList(() => win.vm.run("newList")) }
    Action { id: openAction; text: qsTrId("ram_watch.open_dialog"); icon.source: "qrc:/chimera/icons/OpenFile.png"; shortcut: "Ctrl+O"; onTriggered: win.leaveList(() => { openDialog.append = false; openDialog.open() }) }
    Action {
        id: saveAction; text: qsTrId("common.save"); icon.source: "qrc:/chimera/icons/SaveAs.png"; shortcut: "Ctrl+S"
        onTriggered: win.vm.path === "" ? saveDialog.open() : win.vm.run("save")
    }
    Action { id: newWatchAction; text: qsTrId("ram_watch.new_watch_mnemonic"); icon.source: "qrc:/chimera/icons/addWatch.ico"; shortcut: "Ctrl+W"; onTriggered: editor.start("new", JSON.parse(win.vm.run("editInfo", ["new"]))) }
    Action { id: editAction; text: qsTrId("ram_watch.edit_watch_mnemonic"); icon.source: "qrc:/chimera/icons/Cut.png"; shortcut: "Ctrl+E"; enabled: win.anyRow; onTriggered: win.editWatch(false) }
    Action { id: removeAction; text: qsTrId("ram_watch.remove_watch"); icon.source: "qrc:/chimera/icons/Delete.png"; shortcut: "Ctrl+R"; enabled: win.anyRow; onTriggered: win.vm.run("removeSelected") }
    Action { id: duplicateAction; text: qsTrId("ram_watch.duplicate_watch_mnemonic"); icon.source: "qrc:/chimera/icons/Duplicate.png"; shortcut: "Ctrl+D"; enabled: win.anyWatch; onTriggered: win.editWatch(true) }
    Action { id: splitAction; text: qsTrId("ram_watch.split_watch_mnemonic"); icon.source: "qrc:/chimera/icons/Split.png"; shortcut: "Ctrl+L"; enabled: win.vm.canSplit; onTriggered: win.vm.run("splitSelected") }
    Action { id: pokeAction; text: qsTrId("ram_watch.poke_address"); icon.source: "qrc:/chimera/icons/poke.png"; shortcut: "Ctrl+P"; enabled: win.vm.canPoke; onTriggered: win.pokeWatch() }
    // frozen addresses: written before every frame, for this session only
    Action { id: freezeAction; text: qsTrId("ram_watch.freeze_address"); icon.source: "qrc:/chimera/icons/Freeze.png"; shortcut: "Ctrl+F"; enabled: win.vm.canPoke || win.vm.selectionFrozen; onTriggered: win.vm.run("toggleFreeze") }
    Action { id: separatorAction; text: qsTrId("ram_watch.insert_separator"); icon.source: "qrc:/chimera/icons/InsertSeparator.png"; shortcut: "Ctrl+I"; onTriggered: win.vm.run("insertSeparator") }
    Action { id: clearCountsAction; text: qsTrId("ram_watch.clear_change_counts_mnemonic"); shortcut: "Alt+C"; onTriggered: win.vm.run("clearChangeCounts") }
    Action { id: upAction; text: qsTrId("ram_watch.move_up_mnemonic"); icon.source: "qrc:/chimera/icons/MoveUp.png"; shortcut: "Ctrl+Up"; enabled: win.anyRow; onTriggered: win.vm.run("moveSelected", ["up"]) }
    Action { id: downAction; text: qsTrId("ram_watch.move_down_mnemonic"); icon.source: "qrc:/chimera/icons/MoveDown.png"; shortcut: "Ctrl+Down"; enabled: win.anyRow; onTriggered: win.vm.run("moveSelected", ["down"]) }
    Action { id: topAction; text: qsTrId("ram_watch.move_top"); icon.source: "qrc:/chimera/icons/MoveTop.png"; shortcut: "Ctrl+Shift+Up"; enabled: win.anyRow; onTriggered: win.vm.run("moveSelected", ["top"]) }
    Action { id: bottomAction; text: qsTrId("ram_watch.move_bottom"); icon.source: "qrc:/chimera/icons/MoveBottom.png"; shortcut: "Ctrl+Shift+Down"; enabled: win.anyRow; onTriggered: win.vm.run("moveSelected", ["bottom"]) }
    Action { id: selectAllAction; text: qsTrId("common.select_all"); shortcut: "Ctrl+A"; onTriggered: win.vm.run("selectAll") }

    menuBar: MenuBar {
        Menu {
            title: qsTrId("ram_watch.files")
            delegate: shortcutItem
            MenuItem { action: newListAction }
            MenuItem { action: openAction }
            MenuItem { action: saveAction; enabled: win.vm.changed }
            Action { text: qsTrId("ram_watch.save_as"); shortcut: "Ctrl+Shift+S"; onTriggered: saveDialog.open() }
            Action { text: qsTrId("ram_watch.append_file"); onTriggered: { openDialog.append = true; openDialog.open() } }
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
                        onTriggered: win.leaveList(() => win.vm.run("open", [modelData, false]))
                    }
                    onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => recentMenu.removeItem(object)
                }
            }
            MenuDivider {}
            Action { property string keysShown: "Alt+F4"; text: qsTrId("common.close_mnemonic"); onTriggered: win.close() }
        }
        Menu {
            title: qsTrId("ram_watch.watches")
            delegate: shortcutItem
            Menu {
                delegate: MenuEntry {}
                id: domainMenu
                title: qsTrId("ram_watch.default_domain")
                Instantiator {
                    model: win.vm.domains === "" ? [] : win.vm.domains.split(",")
                    delegate: MenuItem {
                        required property string modelData
                        text: modelData
                        checkable: true
                        checked: win.vm.defaultDomain === modelData
                        onTriggered: win.vm.defaultDomain = modelData
                    }
                    onObjectAdded: (index, object) => domainMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => domainMenu.removeItem(object)
                }
            }
            MenuDivider {}
            MenuItem { action: newWatchAction }
            // only a game core has properties to offer
            MenuItem {
                text: qsTrId("ram_watch.add_game_properties_dialog")
                visible: propertyPicker.names.length > 0; height: visible ? implicitHeight : 0
                onTriggered: propertyPicker.open()
            }
            MenuItem { action: editAction }
            MenuItem { action: removeAction }
            MenuItem { action: duplicateAction }
            MenuItem { action: splitAction }
            MenuItem { action: pokeAction }
            MenuItem { action: freezeAction }
            MenuItem { action: separatorAction }
            MenuItem { action: clearCountsAction }
            MenuItem { text: qsTrId("ram_watch.purge_duplicate_watches"); onTriggered: purgeAsk.open() }
            MenuDivider {}
            MenuItem { action: upAction }
            MenuItem { action: downAction }
            MenuItem { action: topAction }
            MenuItem { action: bottomAction }
            MenuItem { action: selectAllAction }
        }
        Menu {
            delegate: MenuEntry {}
            title: qsTrId("common.settings")
            Menu {
                delegate: MenuEntry {}
                title: qsTrId("ram_watch.define_previous_value")
                MenuItem { text: qsTrId("ram_watch.previous_frame"); checkable: true; checked: win.vm.previousType === 2; onTriggered: win.vm.previousType = 2 }
                MenuItem { text: qsTrId("ram_watch.last_change"); checkable: true; checked: win.vm.previousType === 3; onTriggered: win.vm.previousType = 3 }
                MenuItem { text: qsTrId("ram_watch.original"); checkable: true; checked: win.vm.previousType === 0; onTriggered: win.vm.previousType = 0 }
            }
            MenuItem { text: qsTrId("ram_watch.display_watches_on_screen"); checkable: true; checked: win.vm.showOnScreen; onTriggered: win.vm.showOnScreen = checked }
            Menu {
                delegate: MenuEntry {}
                title: qsTrId("ram_watch.on_double_clicking_a_watch")
                MenuItem { text: qsTrId("ram_watch.edit_watch"); checkable: true; checked: !win.vm.doubleClickToPoke; onTriggered: win.vm.doubleClickToPoke = false }
                MenuItem { text: qsTrId("ram_watch.poke_address"); checkable: true; checked: win.vm.doubleClickToPoke; onTriggered: win.vm.doubleClickToPoke = true }
            }
            MenuDivider {}
            MenuItem { text: qsTrId("common.save_window_position"); checkable: true; checked: windowOptions.savePosition; onTriggered: windowOptions.set("save", checked) }
            MenuItem {
                text: qsTrId("common.stay_on_top"); checkable: true; checked: windowOptions.onTop
                visible: windowOptions.canStayOnTop; height: visible ? implicitHeight : 0
                onTriggered: windowOptions.set("top", checked)
            }
            MenuItem { text: qsTrId("common.float_from_main_window"); checkable: true; checked: windowOptions.floating; onTriggered: windowOptions.set("float", checked) }
            MenuItem {
                text: qsTrId("common.restore_defaults_mnemonic")
                // RAM Watch's own: its columns as at first, watches shown on screen, a double click pokes
                onTriggered: {
                    win.vm.hiddenColumns = "Prev,Diff,Type"
                    win.vm.showOnScreen = true
                    win.vm.doubleClickToPoke = true
                    windowOptions.restore()
                }
            }
        }
        Menu {
            delegate: MenuEntry {}
            id: columnsMenu
            title: qsTrId("ram_watch.columns")
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
        {key: "OSD", name: "OnScreen", text: "OSD", width: 34, field: "onScreen"},
        {key: "Address", name: "Address", text: qsTrId("ram_columns.address"), width: 78, field: "address"},
        {key: "Value", name: "Value", text: qsTrId("ram_columns.value"), width: 77, field: "value"},
        {key: "Prev", name: "Prev", text: qsTrId("ram_columns.prev"), width: 77, field: "previous"},
        {key: "Changes", name: "Changes", text: qsTrId("ram_columns.changes"), width: 78, field: "changes"},
        {key: "Diff", name: "Diff", text: qsTrId("ram_columns.diff"), width: 77, field: "diff"},
        {key: "Type", name: "Type", text: qsTrId("ram_columns.type"), width: 72, field: "type"},
        {key: "Domain", name: "Domain", text: qsTrId("ram_columns.domain"), width: 72, field: "domain"},
        {key: "Notes", name: "Notes", text: qsTrId("ram_columns.notes"), width: 166, field: "note"}
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
                property string tip: action ? action.text.replace("&", "") : ""
                display: AbstractButton.IconOnly
                ToolTip.visible: hovered
                ToolTip.text: tip
                ToolTip.delay: 500
            }
            Tool { action: newListAction; tip: qsTrId("ram_watch.new") }
            Tool { action: openAction; tip: qsTrId("ram_watch.open") }
            Tool { action: saveAction; tip: qsTrId("ram_watch.save") }
            ToolSeparator {}
            Tool { action: newWatchAction; tip: qsTrId("ram_watch.new_watch") }
            Tool { action: editAction; tip: qsTrId("ram_watch.edit_watch") }
            Tool { action: removeAction; tip: qsTrId("common.cut") }
            // Chimera's own "C" button
            ToolButton {
                text: qsTrId("ram_watch.c"); onClicked: win.vm.run("clearChangeCounts")
                ToolTip.visible: hovered; ToolTip.text: qsTrId("ram_watch.clear_change_counts"); ToolTip.delay: 500
            }
            Tool { action: duplicateAction; tip: qsTrId("ram_watch.duplicate_watch") }
            Tool { action: splitAction; tip: qsTrId("ram_watch.split_watch") }
            Tool { action: pokeAction }
            Tool { action: freezeAction }
            Tool { action: separatorAction; tip: qsTrId("ram_watch.separator") }
            ToolSeparator {}
            Tool { action: upAction; tip: qsTrId("ram_watch.move_up") }
            Tool { action: downAction; tip: qsTrId("ram_watch.move_down") }
        }
    }

    footer: RowLayout {
        spacing: 4
        ToolButton {
            objectName: "ramWatchInvalid"
            visible: win.vm.invalidCount > 0
            icon.source: "qrc:/chimera/icons/ExclamationRed.png"
            display: AbstractButton.IconOnly
            ToolTip.visible: hovered
            ToolTip.text: qsTrId("ram_watch.warning_out_of_range_addresses_in")
            onClicked: win.vm.run("removeInvalid")
        }
        Label {
            Layout.fillWidth: true
            padding: 4
            text: win.vm.error !== "" ? win.vm.error : win.vm.status
            elide: Text.ElideRight
        }
    }

    // the list's right-click menu
    Menu {
        id: listMenu
        delegate: shortcutItem
        MenuItem { action: newWatchAction; visible: !win.anyRow; height: visible ? implicitHeight : 0 }
        Action { property string keysShown: "Ctrl+E"; text: qsTrId("common.edit"); icon.source: "qrc:/chimera/icons/Cut.png"; enabled: win.anyRow; onTriggered: win.editWatch(false) }
        Action { property string keysShown: "Ctrl+R"; text: qsTrId("common.remove_mnemonic"); icon.source: "qrc:/chimera/icons/Delete.png"; enabled: win.anyRow; onTriggered: win.vm.run("removeSelected") }
        Action { property string keysShown: "Ctrl+D"; text: qsTrId("ram_watch.duplicate"); icon.source: "qrc:/chimera/icons/Duplicate.png"; enabled: win.anyWatch; onTriggered: win.editWatch(true) }
        Action { property string keysShown: "Ctrl+L"; text: qsTrId("ram_watch.split"); icon.source: "qrc:/chimera/icons/Split.png"; enabled: win.vm.canSplit; onTriggered: win.vm.run("splitSelected") }
        Action { property string keysShown: "Ctrl+P"; text: qsTrId("ram_watch.poke"); icon.source: "qrc:/chimera/icons/poke.png"; enabled: win.vm.canPoke; onTriggered: win.pokeWatch() }
        MenuItem {
            action: Action {
                property string keysShown: "Ctrl+F"
                text: win.vm.selectionFrozen ? qsTrId("ram_watch.unfreeze_address") : qsTrId("common.freeze_address")
                icon.source: win.vm.selectionFrozen ? "qrc:/chimera/icons/Unfreeze.png" : "qrc:/chimera/icons/Freeze.png"
                onTriggered: win.vm.run("toggleFreeze")
            }
            visible: win.vm.canPoke || win.vm.selectionFrozen; height: visible ? implicitHeight : 0
        }
        MenuItem {
            text: qsTrId("ram_watch.unfreeze_all"); icon.source: "qrc:/chimera/icons/Unfreeze.png"
            visible: win.vm.anyFrozen; height: visible ? implicitHeight : 0
            onTriggered: win.vm.run("unfreezeAll")
        }
        MenuItem {
            text: qsTrId("ram_watch.view_in_hex_editor")
            visible: win.vm.selectedWatches === 1 && win.viewInHexEditor !== null; height: visible ? implicitHeight : 0
            onTriggered: {
                const info = JSON.parse(win.vm.run("editInfo", ["edit"]))
                win.viewInHexEditor(info.domain, parseInt(info.address, 16))
            }
        }
        MenuDivider {}
        Action { property string keysShown: "Ctrl+I"; text: qsTrId("ram_watch.insert_separator_mnemonic"); icon.source: "qrc:/chimera/icons/InsertSeparator.png"; onTriggered: win.vm.run("insertSeparator") }
        Action { property string keysShown: "Ctrl+Up"; text: qsTrId("ram_watch.move_up_mnemonic"); icon.source: "qrc:/chimera/icons/MoveUp.png"; enabled: win.anyRow; onTriggered: win.vm.run("moveSelected", ["up"]) }
        Action { property string keysShown: "Ctrl+Down"; text: qsTrId("ram_watch.move_down_mnemonic"); icon.source: "qrc:/chimera/icons/MoveDown.png"; enabled: win.anyRow; onTriggered: win.vm.run("moveSelected", ["down"]) }
        MenuItem { action: topAction }
        MenuItem { action: bottomAction }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Label { objectName: "ramWatchCount"; text: win.vm.countText; padding: 4 }
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
            objectName: "ramWatchList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            model: win.vm.rows("watches")
            ScrollBar.vertical: ScrollBar {}
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Delete) { win.vm.run("removeSelected"); event.accepted = true }
                else if (event.matches(StandardKey.Copy)) { win.copyWatches(); event.accepted = true }
                else if (event.matches(StandardKey.Paste)) { win.pasteWatches(); event.accepted = true }
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) { win.openWatch(); event.accepted = true }
            }
            delegate: Rectangle {
                id: row
                required property int index
                required property var model
                readonly property bool separator: row.model.separator === true
                width: list.width
                height: 22
                color: row.model.selected ? palette.highlight
                     : row.separator ? palette.window
                     : row.model.invalid ? win.themeColor("RowInvalid", "#FFDAB9")
                     : row.model.frozen ? win.themeColor("RowActive", "#E0FFFF")
                     : win.themeColor("RowDefault", palette.base)
                // the grid, as Chimera's list draws it: a line under every row and between the columns
                Repeater {
                    model: win.columnLines
                    delegate: Rectangle { required property real modelData; x: modelData; width: 1; height: row.height; color: win.gridColour }
                }
                Rectangle { y: row.height - 1; width: row.width; height: 1; color: win.gridColour }
                Row {
                    anchors.fill: parent
                    visible: !row.separator
                    Repeater {
                        model: win.visibleColumns
                        delegate: Item {
                            required property var modelData
                            required property int index
                            width: index === win.visibleColumns.length - 1 ? Math.max(modelData.width, list.width - x) : modelData.width
                            height: row.height
                            CheckBox {
                                visible: modelData.key === "OSD"
                                anchors.centerIn: parent
                                checked: row.model.onScreen === true
                                padding: 0
                                onClicked: win.vm.run("setOnScreen", [row.index, checked])
                            }
                            Label {
                                visible: modelData.key !== "OSD"
                                anchors.fill: parent
                                leftPadding: 3
                                verticalAlignment: Text.AlignVCenter
                                text: { const v = row.model[modelData.field]; return v === undefined || v === null ? "" : String(v) }
                                font.family: modelData.key === "Notes" || modelData.key === "Domain" ? Qt.application.font.family : "monospace"
                                elide: Text.ElideRight
                                color: row.model.selected ? palette.highlightedText : palette.text
                            }
                        }
                    }
                }
                // a separator's text, in the Address column and past it
                Label {
                    visible: row.separator
                    x: 3
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.model.note
                    font.bold: true
                    color: row.model.selected ? palette.highlightedText : palette.text
                }
                MouseArea {
                    anchors.fill: parent
                    // the OSD box takes its own clicks
                    anchors.leftMargin: win.shown("OSD") ? 34 : 0
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
                    onDoubleClicked: (mouse) => { if (mouse.button === Qt.LeftButton) win.openWatch() }
                }
            }
            // a click below the rows lets every row go; a right click there opens the menu
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

    // New Watch, Edit Watch(es), Duplicate Watch: Chimera's one editor
    ApplicationWindow {
        id: editor
        objectName: "watchEditorWindow"
        property string mode: "new"
        property var info: ({})
        property var displays: []
        title: editor.mode === "new" ? qsTrId("ram_watch.new_watch") : editor.mode === "duplicate" ? qsTrId("ram_watch.duplicate_watch")
             : editor.info.many ? qsTrId("ram_watch.edit_watches") : qsTrId("ram_watch.edit_watch")
        width: 300
        height: 260
        modality: Qt.ApplicationModal
        function start(mode, info) {
            editor.mode = mode
            editor.info = info
            domainBox.model = win.vm.domains === "" ? [] : win.vm.domains.split(",")
            domainBox.currentIndex = Math.max(0, domainBox.model.indexOf(info.domain))
            addressBox.text = info.address
            sizeBox.currentIndex = info.size === 4 ? 2 : info.size - 1
            editor.fillDisplays(info.display)
            bigEndianBox.checkState = info.bigEndian === 2 ? Qt.PartiallyChecked : info.bigEndian === 1 ? Qt.Checked : Qt.Unchecked
            notesBox.text = info.note
            editor.show()
            editor.raise()
            notesBox.forceActiveFocus()
        }
        function fillDisplays(keep) {
            editor.displays = JSON.parse(win.vm.run("displays", [[1, 2, 4][sizeBox.currentIndex]]))
            displayBox.model = editor.displays.map((d) => d.name)
            const at = editor.displays.findIndex((d) => d.display === keep)
            displayBox.currentIndex = at >= 0 ? at : 0
        }
        function accept() {
            const size = [1, 2, 4][sizeBox.currentIndex]
            const display = editor.displays[displayBox.currentIndex].display
            const endian = bigEndianBox.checkState === Qt.PartiallyChecked ? 2 : bigEndianBox.checked ? 1 : 0
            const ok = editor.mode === "new"
                ? win.vm.run("newWatch", [domainBox.currentText, addressBox.text, size, display, endian === 1, notesBox.text])
                : win.vm.run(editor.mode === "duplicate" ? "duplicateSelected" : "editSelected", [size, display, endian, editor.info.many ? "" : addressBox.text, notesBox.text])
            if (win.vm.error === "") editor.close()
        }
        GridLayout {
            anchors.fill: parent
            anchors.margins: 10
            columns: 2
            Label { text: qsTrId("ram_watch.mem_domain"); Layout.alignment: Qt.AlignRight }
            ComboBox { id: domainBox; Layout.fillWidth: true; enabled: editor.mode === "new" }
            Label { text: qsTrId("ram_watch.address"); Layout.alignment: Qt.AlignRight }
            RowLayout {
                Label { text: "0x" }
                TextField { id: addressBox; Layout.fillWidth: true; font.family: "monospace"; enabled: !editor.info.many }
            }
            Label { text: qsTrId("ram_watch.size"); Layout.alignment: Qt.AlignRight }
            ComboBox {
                id: sizeBox
                Layout.fillWidth: true
                model: [qsTrId("common.1_byte"), qsTrId("common.2_byte"), qsTrId("common.4_byte")]
                onActivated: editor.fillDisplays(editor.displays.length > 0 ? editor.displays[displayBox.currentIndex].display : 0)
            }
            Item { width: 1 }
            CheckBox { id: bigEndianBox; text: qsTrId("ram_watch.big_endian"); tristate: editor.info.many === true }
            Label { text: qsTrId("ram_watch.display_type"); Layout.alignment: Qt.AlignRight }
            ComboBox { id: displayBox; Layout.fillWidth: true; enabled: !(editor.info.sizesDiffer === true) }
            Label { text: qsTrId("ram_watch.notes"); Layout.alignment: Qt.AlignRight }
            TextField { id: notesBox; Layout.fillWidth: true; enabled: !editor.info.many; maximumLength: 256; onAccepted: editor.accept() }
            Label { Layout.columnSpan: 2; text: win.vm.error; color: win.themeColor("AccentError", "#b00020"); visible: text !== ""; wrapMode: Text.Wrap; Layout.fillWidth: true }
            RowLayout {
                Layout.columnSpan: 2
                Layout.alignment: Qt.AlignRight
                Button { text: qsTrId("common.ok"); highlighted: true; onClicked: editor.accept() }
                Button { text: qsTrId("common.cancel"); onClicked: editor.close() }
            }
        }
    }

    // RAM Poke: a value written into the watches picked
    RamPoke { id: poke; vm: win.vm; tell: win.tell }

    // Edit Separator: the text of the separators picked
    ApplicationWindow {
        id: separatorPrompt
        title: qsTrId("ram_watch.edit_separator")
        width: 300
        height: 120
        modality: Qt.ApplicationModal
        function open() { separatorText.text = ""; separatorPrompt.show(); separatorPrompt.raise(); separatorText.forceActiveFocus() }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            Label { text: qsTrId("ram_watch.separator_text") }
            TextField { id: separatorText; Layout.fillWidth: true; onAccepted: okSeparator.clicked() }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button { id: okSeparator; text: qsTrId("common.ok"); onClicked: { win.vm.run("editSeparators", [separatorText.text]); separatorPrompt.close() } }
                Button { text: qsTrId("common.cancel"); onClicked: separatorPrompt.close() }
            }
        }
    }

    // Add Game Properties: a game core's properties, each ticked to be watched
    ApplicationWindow {
        id: propertyPicker
        title: qsTrId("ram_watch.add_game_properties")
        width: 320
        height: 400
        modality: Qt.ApplicationModal
        property var names: []
        property var ticked: ({})
        Connections {
            target: win.vm
            function onDomainsChanged() { propertyPicker.names = win.gameProperties ? (win.gameProperties.run("list") || "").split("\n").filter((n) => n !== "") : [] }
        }
        Component.onCompleted: propertyPicker.names = win.gameProperties ? (win.gameProperties.run("list") || "").split("\n").filter((n) => n !== "") : []
        function open() { propertyPicker.ticked = ({}); propertyPicker.show(); propertyPicker.raise() }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            ListView {
                id: pickList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: propertyPicker.names
                ScrollBar.vertical: ScrollBar {}
                delegate: CheckBox {
                    required property string modelData
                    text: modelData
                    onToggled: { const t = propertyPicker.ticked; t[modelData] = checked; propertyPicker.ticked = t }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button {
                    text: qsTrId("common.ok")
                    onClicked: {
                        for (const n of propertyPicker.names) if (propertyPicker.ticked[n]) win.vm.run("addProperty", [n])
                        propertyPicker.close()
                    }
                }
                Button { text: qsTrId("common.cancel"); onClicked: propertyPicker.close() }
            }
        }
    }
}
