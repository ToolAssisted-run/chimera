// Chimera's input roll as TAStudio's Branches and Markers boxes use it: the
// column titles on the roll's column colour, a grid over the rows, the text in
// bold, the whole row picked, and the theme's roll colours throughout.
// Layout and drawing only: what a cell says and its colour are the window's.
import QtQuick
import QtQuick.Controls

Rectangle {
    id: table
    // a rows model of a view model's; each row's roles reach the window's functions
    required property var model
    // [{title, width}], widths as Chimera's at 96 dpi; the last column is at least its width
    property var columns: []
    // the row picked, -1 for none
    property int current: -1
    // every row picked: the row clicked, and others picked with Ctrl (one
    // more or one less) or Shift (every row from the row clicked before),
    // as a Windows list picks them (issue 494)
    property var selection: []
    // where a Shift click's rows start
    property int anchor: -1
    // what a cell says and its background ("" leaves the roll's): (row's roles, row, column) => ...
    property var cellText: (roles, row, column) => ""
    property var cellColor: (roles, row, column) => ""
    // a roll colour by Chimera's role name ("RollGridLines"), "" when there is none
    property var rollColor: (role) => ""
    // Chimera's input roll font in these lists: Arial 8.25 bold, in pixels (11,
    // 8.25 points at Windows' 96 dots an inch): in points it was a quarter
    // smaller on a Mac, which counts 72 (issue 539)
    property font cellFont: Qt.font({ family: "Arial", pixelSize: 11, bold: true })
    // a row pressed by its first cell can be dragged onto another (branches are put in order so)
    property bool dragFirstColumn: false

    // a row clicked (-1: a Ctrl click let go of the last row picked)
    signal clicked(int row, int column)
    signal doubleClicked(int row)
    signal rightClicked(int row)
    // the pointer over a row (-1: over none)
    signal pointed(int row)
    signal dropped(int from, int to)

    // a row's roles, while it is drawn (the row picked always is)
    function rolesAt(row) { const it = list.itemAtIndex(row); return it ? it.model : ({}) }
    // a row scrolled into view, as TAStudio shows a branch or marker it adds (issue 487)
    function show(row) {
        if (row < 0 || row >= list.count) return
        list.positionViewAtIndex(row, ListView.Contain)
        // and not under the bar across the bottom
        const item = list.itemAtIndex(row)
        const room = list.height - list.bottomMargin
        if (item && item.y + item.height - list.contentY > room) list.contentY = item.y + item.height - room
    }
    // the row picked stays in view, the list made again or not; a row the
    // window picks is the only one picked
    onCurrentChanged: {
        if (table.selection.indexOf(table.current) < 0) {
            table.selection = table.current >= 0 ? [table.current] : []
            table.anchor = table.current
        }
        Qt.callLater(() => table.show(table.current))
    }
    Connections {
        target: table.model
        function onModelReset() {
            // the rows are numbered again: only the row picked stays picked
            table.selection = table.current >= 0 ? [table.current] : []
            table.anchor = table.current
            Qt.callLater(() => table.show(table.current))
        }
    }
    function colour(role, fallback) { const c = table.rollColor(role); return c !== "" ? c : fallback }
    readonly property color background: colour("RollBackground", palette.base)
    readonly property color textColour: colour("RollText", palette.text)
    readonly property color gridColour: colour("RollGridLines", Qt.darker(palette.base, 1.25))
    readonly property color titleBack: colour("RollColumnBackground", palette.button)
    readonly property color titleBorder: colour("RollColumnBorder", palette.dark)
    readonly property color pickColour: colour("RollSelection", palette.highlight)
    readonly property color pickText: colour("RollSelectionText", palette.highlightedText)
    readonly property int rowHeight: Math.ceil(metrics.height) + 1
    // every column's left edge, and the total width
    readonly property var lefts: {
        let at = 0, out = []
        for (let c = 0; c < columns.length; c++) { out.push(at); at += columns[c].width }
        out.push(at)
        return out
    }
    readonly property int columnsWidth: lefts[columns.length]
    // the last column takes what is left of the width
    function widthOf(c) { return c === columns.length - 1 ? Math.max(columns[c].width, list.width - lefts[c]) : columns[c].width }
    function columnAt(x) {
        for (let c = columns.length - 1; c >= 0; c--) if (x >= lefts[c]) return c
        return 0
    }
    // a selected cell as Chimera paints it: a third of the way to the selection colour
    function picked(base) { return Qt.tint(base, Qt.rgba(pickColour.r, pickColour.g, pickColour.b, 0.33)) }

    color: background
    border.color: titleBorder
    clip: true
    FontMetrics { id: metrics; font: table.cellFont }

    ListView {
        id: list
        anchors.fill: parent
        anchors.margins: 1
        clip: true
        model: table.model
        contentWidth: Math.max(table.columnsWidth, width)
        flickableDirection: Flickable.AutoFlickIfNeeded
        boundsBehavior: Flickable.StopAtBounds
        headerPositioning: ListView.OverlayHeader
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOn }
        ScrollBar.horizontal: ScrollBar { id: across; policy: table.columnsWidth > list.width ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
        // the bar across lies over the rows: the last can be scrolled above it
        bottomMargin: across.policy === ScrollBar.AlwaysOn ? across.height : 0
        // the column titles, which stay put while the rows scroll
        header: Rectangle {
            z: 2
            width: list.contentWidth
            height: table.rowHeight
            color: table.titleBack
            Repeater {
                model: table.columns.length
                delegate: Item {
                    required property int index
                    x: table.lefts[index]
                    width: table.widthOf(index)
                    height: table.rowHeight
                    Rectangle { width: 1; height: parent.height; color: table.titleBorder }
                    Text {
                        x: 4
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 6
                        text: table.columns[parent.index].title
                        font: table.cellFont
                        color: table.textColour
                        elide: Text.ElideRight
                    }
                }
            }
            Rectangle { y: parent.height - 1; width: parent.width; height: 1; color: table.titleBorder }
        }
        delegate: Item {
            id: row
            required property int index
            required property var model
            width: list.contentWidth
            height: table.rowHeight
            readonly property bool isPicked: table.selection.indexOf(row.index) >= 0
            Repeater {
                model: table.columns.length
                delegate: Rectangle {
                    id: cell
                    required property int index
                    readonly property string own: { const c = table.cellColor(row.model, row.index, index); return c ? String(c) : "" }
                    x: table.lefts[index]
                    width: table.widthOf(index)
                    height: table.rowHeight
                    color: row.isPicked ? table.picked(own !== "" ? own : table.background) : (own !== "" ? own : "transparent")
                    // the grid: a line at the left of every column and under every row
                    Rectangle { width: 1; height: parent.height; color: table.gridColour }
                    Rectangle { y: parent.height - 1; width: parent.width; height: 1; color: table.gridColour }
                    Text {
                        x: 4
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 6
                        text: { const t = table.cellText(row.model, row.index, cell.index); return t === undefined || t === null ? "" : String(t) }
                        font: table.cellFont
                        color: row.isPicked ? table.pickText : table.textColour
                        elide: Text.ElideRight
                    }
                }
            }
        }
        // the rows' mouse: a click picks (with Ctrl or Shift, more rows), a double click is the
        // window's, the right button its menu, a drag from a row's first cell another row's place
        MouseArea {
            id: mouse
            parent: list.contentItem
            z: 1
            // a drag from a row's number is the window's, not the list's to scroll
            preventStealing: mouse.from >= 0
            width: list.contentWidth
            height: Math.max(list.contentHeight, list.height)
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            hoverEnabled: true
            property int from: -1
            function rowAt(y) { return list.indexAt(1, y) }
            onPressed: (m) => {
                const r = rowAt(m.y)
                if (m.button === Qt.RightButton) { table.rightClicked(r); return }
                mouse.from = table.dragFirstColumn && r >= 0 && table.columnAt(m.x) === 0 && m.modifiers === Qt.NoModifier ? r : -1
                if (r < 0) return
                let picked = [r], anchor = r, told = r
                if (m.modifiers & Qt.ShiftModifier) {
                    anchor = table.anchor >= 0 && table.anchor < list.count ? table.anchor : r
                    picked = []
                    for (let i = Math.min(anchor, r); i <= Math.max(anchor, r); i++) picked.push(i)
                } else if (m.modifiers & Qt.ControlModifier) {
                    picked = table.selection.filter(i => i !== r)
                    if (picked.length === table.selection.length) picked.push(r)
                    // a row let go of: the window's row is the last one still picked
                    else told = picked.length > 0 ? picked[picked.length - 1] : -1
                }
                // told first: the window's row changing would pick it alone
                table.clicked(told, table.columnAt(m.x))
                table.selection = picked
                table.anchor = anchor
            }
            onReleased: (m) => {
                const r = rowAt(m.y)
                if (mouse.from >= 0 && r >= 0 && r !== mouse.from) table.dropped(mouse.from, r)
                mouse.from = -1
            }
            onDoubleClicked: (m) => { const r = rowAt(m.y); if (r >= 0 && m.button === Qt.LeftButton) table.doubleClicked(r) }
            onPositionChanged: (m) => table.pointed(rowAt(m.y))
            onExited: table.pointed(-1)
        }
    }
}
