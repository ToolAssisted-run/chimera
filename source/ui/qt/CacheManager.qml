// Chimera's Cache Manager (Tools > Cache Manager...), laid out as its
// The Cache Manager: what is cached and whether it is over its limit at the
// top, Select all and the limit (Keep the cache under N GB, and leave the
// disk at least N GB free) over the list, a row an entry with a box to tick
// and a padlock (What, Name, System, Core, Game, Project id, Project file,
// Cache location, Size, Last modified; a heading click sorts), what the row
// picked means under it, the line of what was last done, and Remove
// Ticked, Lock / Unlock, Select all orphans, Greenzone budgets..., Open
// Folder, Clean Now and Close. A project's cache can be made again:
// removing it costs waiting, never work; unsaved work a crash left is work.
// Layout and bindings only, over the cacheManager view model.
import QtQuick
import QtQuick.Controls
import ChimeraDialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "cacheManagerWindow"
    required property QtObject vm
    property var themeColor: (role, fallback) => fallback
    title: qsTrId("cache_manager.cache_manager")
    width: 1460
    height: 500
    // as wide as its widest row of buttons and boxes needs, in any language (issue 507)
    minimumWidth: Math.max(900, optionsRow.implicitWidth + 16, buttonRow.implicitWidth + 16)
    minimumHeight: 380
    // Chimera's Greenzone budgets button: the window is the main window's
    // the project row picked, when it is one, for its own budgets: (id, name), or ("", "")
    signal budgetsAsked(string projectId, string projectLabel)

    // the entries ticked and the one picked, by their place on disk (they survive a sort or a look again)
    property var ticked: ({})
    property string picked: ""
    readonly property var tickedFolders: Object.keys(win.ticked)
    readonly property string tickedPlaces: win.tickedFolders.length === 0 ? "" : (win.vm.run("placesOf", [JSON.stringify(win.tickedFolders)]) || "")
    readonly property var tickedCost: win.tickedPlaces === "" ? null : JSON.parse(win.vm.run("costs", [win.tickedPlaces]) || "null")
    readonly property var widths: [30, 100, 180, 70, 90, 200, 120, 230, 230, 70, 110]
    // a row picked at once, as Chimera's: the project open now, else the one used last
    onVisibleChanged: if (visible) {
        win.vm.run("refresh")
        win.ticked = {}
        win.picked = win.vm.run("firstPick") || ""
    }
    function setTicks(folders) {
        const now = {}
        for (const f of folders) now[f] = true
        win.ticked = now
    }

    MessageDialog {
        id: removeAsk
        title: qsTrId("cache_manager.remove_cached_data")
        property string places: ""
        property var cost: null
        text: removeAsk.cost ? qsTrId("cache_manager.remove_cached_item_s_freeing").arg(removeAsk.cost.count).arg(removeAsk.cost.size).arg(removeAsk.cost.costs) : ""
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: {
            if (removeAsk.places === "clean") win.vm.run("cleanNow")
            else win.vm.run("remove", [removeAsk.places])
            win.ticked = {}
        }
    }

    // Chimera's padlocks: red and open, the auto-clean may take it; green and shut, it may not
    component Padlock: Canvas {
        required property bool shut
        width: 16; height: 16
        onShutChanged: requestPaint()
        onPaint: {
            const g = getContext("2d")
            g.reset()
            const c = shut ? win.themeColor("GlyphGood", "#2e7d32") : win.themeColor("GlyphError", "#c62828")
            g.strokeStyle = c
            g.fillStyle = c
            g.lineWidth = 1.8
            g.beginPath()
            if (shut) g.arc(8, 8, 3.5, Math.PI, 0)
            else g.arc(10, 6.5, 3.5, Math.PI, Math.PI * 1.8)
            g.stroke()
            g.fillRect(3, 8, 10, 7)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4
        Label { text: win.vm.header; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.preferredHeight: 50 }
        RowLayout {
            id: optionsRow
            Layout.fillWidth: true
            CheckBox {
                text: !win.tickedCost ? qsTrId("cache_manager.select_all") : qsTrId("cache_manager.select_all_ticked").arg(win.tickedCost.count).arg(win.tickedCost.size)
                checked: false
                onClicked: win.setTicks(checked ? JSON.parse(win.vm.run("folders", ["free"]) || "[]") : [])
                padding: 2
            }
            Item { Layout.fillWidth: true }
            // the limit, kept at once; nothing is removed while it is typed
            CheckBox { text: qsTrId("cache_manager.keep_the_cache_under"); checked: win.vm.autoClean; onToggled: win.vm.autoClean = checked; padding: 2 }
            SpinBox { from: 1; to: 100000; stepSize: 5; editable: true; value: win.vm.limitGb; onValueModified: win.vm.limitGb = value; Layout.preferredWidth: 100 }
            Label { text: qsTrId("cache_manager.gb") }
            Label { text: qsTrId("cache_manager.and_leave_the_disk_at_least"); Layout.leftMargin: 12 }
            SpinBox { from: 0; to: 100000; stepSize: 5; editable: true; value: win.vm.floorGb; onValueModified: win.vm.floorGb = value; Layout.preferredWidth: 90 }
            Label { text: qsTrId("cache_manager.gb_free") }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: palette.base
            border.color: palette.mid
            ScrollView {
                id: scroller
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                contentWidth: 1700
                contentHeight: scroller.availableHeight
                ColumnLayout {
                    width: 1700
                    height: scroller.availableHeight
                    spacing: 0
                    // the headings: a click sorts by the column, again the other way
                    Row {
                        Repeater {
                            model: ["", qsTrId("cache_manager.what"), qsTrId("common.name"), qsTrId("cache_manager.system"), qsTrId("common.core"), qsTrId("cache_manager.game"), qsTrId("cache_manager.project_id"), qsTrId("cache_manager.project_file"),
                                    qsTrId("cache_manager.cache_location"), qsTrId("common.size"), qsTrId("cache_manager.last_modified")]
                            delegate: Label {
                                required property int index
                                required property string modelData
                                text: modelData + (win.vm.sortColumn === index ? (win.vm.sortAscending ? " ▲" : " ▼") : "")
                                width: win.widths[index] + (index === 0 ? 22 : 0)
                                leftPadding: 4; topPadding: 2; bottomPadding: 2
                                horizontalAlignment: index === 9 ? Text.AlignRight : Text.AlignLeft
                                rightPadding: index === 9 ? 4 : 0
                                background: Rectangle { color: palette.button; border.color: palette.mid }
                                TapHandler { onTapped: win.vm.run("sortBy", [index]) }
                            }
                        }
                    }
                    ListView {
                        boundsBehavior: Flickable.StopAtBounds
                        id: list
                        objectName: "cacheList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: win.vm.rows("caches")
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            id: row
                            required property int index
                            required property string title
                            required property string system
                            required property string core
                            required property string games
                            required property string sizeText
                            required property string folder
                            required property bool open
                            required property bool locked
                            required property string what
                            required property string projectId
                            required property string projectFile
                            required property string modified
                            required property string lockTip
                            width: list.width
                            height: 22
                            readonly property bool isPicked: row.folder === win.picked
                            color: isPicked ? palette.highlight : "transparent"
                            // an entry in use is greyed and cannot be ticked
                            readonly property color ink: isPicked ? palette.highlightedText : row.open ? palette.mid : palette.text
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                CheckBox {
                                    checked: win.ticked[row.folder] === true
                                    enabled: !row.open
                                    onClicked: { const now = Object.assign({}, win.ticked); if (checked) now[row.folder] = true; else delete now[row.folder]; win.ticked = now }
                                    padding: 0
                                    width: 22
                                }
                                Item {
                                    width: win.widths[0]; height: 16
                                    Padlock { anchors.centerIn: parent; shut: row.locked }
                                    ToolTip.visible: lockHover.hovered
                                    ToolTip.text: row.lockTip
                                    HoverHandler { id: lockHover }
                                }
                                Label { text: row.what; color: row.ink; width: win.widths[1]; leftPadding: 4; elide: Text.ElideRight }
                                Label { text: row.title; color: row.ink; width: win.widths[2]; leftPadding: 4; elide: Text.ElideRight }
                                Label { text: row.system; color: row.ink; width: win.widths[3]; leftPadding: 4; elide: Text.ElideRight }
                                Label { text: row.core; color: row.ink; width: win.widths[4]; leftPadding: 4; elide: Text.ElideRight }
                                Label { text: row.games; color: row.ink; width: win.widths[5]; leftPadding: 4; elide: Text.ElideRight }
                                Label { text: row.projectId; color: row.ink; width: win.widths[6]; leftPadding: 4; elide: Text.ElideRight }
                                Label { text: row.projectFile; color: row.ink; width: win.widths[7]; leftPadding: 4; elide: Text.ElideMiddle }
                                Label { text: row.folder; color: row.ink; width: win.widths[8]; leftPadding: 4; elide: Text.ElideMiddle }
                                Label { text: row.sizeText; color: row.ink; width: win.widths[9]; horizontalAlignment: Text.AlignRight; rightPadding: 4 }
                                Label { text: row.modified; color: row.ink; width: win.widths[10]; leftPadding: 4 }
                            }
                            TapHandler { onTapped: win.picked = row.folder }
                        }
                    }
                }
            }
        }
        // what the row picked means, in full
        Label {
            id: detail
            readonly property string place: win.picked === "" ? "" : (win.vm.run("placesOf", [JSON.stringify([win.picked])]) || "")
            text: detail.place === "" ? "" : (win.vm.run("detailOf", [parseInt(detail.place)]) || "")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.preferredHeight: 66
            clip: true
        }
        Label { text: win.vm.status; elide: Text.ElideRight; Layout.fillWidth: true }
        RowLayout {
            id: buttonRow
            Layout.fillWidth: true
            Button {
                text: qsTrId("cache_manager.remove_ticked"); Layout.preferredWidth: 160
                enabled: win.tickedPlaces !== ""
                onClicked: { removeAsk.places = win.tickedPlaces; removeAsk.cost = win.tickedCost; removeAsk.open() }
            }
            Button {
                text: qsTrId("cache_manager.lock_unlock"); Layout.preferredWidth: 160
                enabled: win.tickedPlaces !== ""
                onClicked: win.vm.run("toggleLock", [win.tickedPlaces])
            }
            Button {
                text: qsTrId("cache_manager.select_all_orphans"); Layout.preferredWidth: 160
                enabled: (JSON.parse(win.vm.run("folders", ["orphans"]) || "[]")).length > 0
                onClicked: {
                    const orphans = JSON.parse(win.vm.run("folders", ["orphans"]) || "[]")
                    win.setTicks(orphans)
                    win.vm.status = orphans.length === 0 ? qsTrId("cache_manager.no_cache_belongs_to_a_project")
                        : qsTrId("cache_manager.ticked_orphaned_item_s").arg(orphans.length).arg(win.tickedCost ? win.tickedCost.size : "")
                }
            }
            Button {
                text: qsTrId("cache_manager.greenzone_budgets"); Layout.preferredWidth: 160
                // a project row offers that project's own budgets as well as the defaults; with none picked, the defaults alone
                onClicked: {
                    const place = win.picked === "" ? "" : (win.vm.run("placesOf", [JSON.stringify([win.picked])]) || "")
                    const project = place === "" ? { id: "", title: "" } : JSON.parse(win.vm.run("projectOf", [parseInt(place)]) || "{}")
                    win.budgetsAsked(project.id || "", project.title || "")
                }
            }
            Button {
                text: qsTrId("common.open_folder"); Layout.preferredWidth: 160
                enabled: win.picked !== ""
                onClicked: win.vm.status = Qt.openUrlExternally(localFiles.folderUrl(win.picked)) ? qsTrId("cache_manager.opened").arg(win.picked) : qsTrId("cache_manager.could_not_open").arg(win.picked)
            }
            Button {
                text: qsTrId("cache_manager.clean_now"); Layout.preferredWidth: 160; Layout.leftMargin: 8
                enabled: win.vm.overLimit !== ""
                onClicked: {
                    const going = win.vm.run("wouldGo")
                    if (going === "") { win.vm.run("cleanNow"); return }
                    removeAsk.places = "clean"
                    removeAsk.cost = JSON.parse(win.vm.run("costs", [going]) || "null")
                    removeAsk.open()
                }
            }
            Item { Layout.fillWidth: true }
            Button { text: qsTrId("common.close"); Layout.preferredWidth: 90; onClicked: win.close() }
        }
    }
}
