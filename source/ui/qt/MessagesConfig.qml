// Chimera's Configure On Screen Messages (Config > Messages), laid out as
// Chimera's Messages window: Message Type (a row each, with where it is) and
// Message Colors (a swatch to click and its hex) down the left; Position on
// the right (a small screen to click or drag the mark on, a corner each to
// count from, x and y); how long messages stay, Stack Messages, Restore
// Defaults, OK and Cancel. Changes show at once; Cancel, or closing the
// window, puts back what was there when it opened. Layout and bindings
// only, over the messages view model.
import QtQuick
import QtQuick.Controls
import ChimeraDialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "messagesWindow"
    required property QtObject vm
    property Window mainWindow: null
    // the theme's colour for a role (Main hands it its lookup)
    property var themeColor: (role, fallback) => fallback
    title: qsTrId("messages_config.configure_on_screen_messages")
    width: 560
    height: 470
    modality: Qt.ApplicationModal
    transientParent: win.mainWindow
    readonly property var names: ["Fps", "Frame Counter", "Lag Counter", "Input Display", "Watches", "Messages", "Rerecords", "Autohold", "Game Time", "Real Time"]
    readonly property var places: JSON.parse(win.vm.positions)
    property string picked: "Fps"
    readonly property var place: win.places[win.picked] || ({ "x": 0, "y": 0, "anchor": 0 })
    function setPlace(x, y, anchor) { win.vm.run("setPosition", [win.picked, x, y, anchor]) }
    // the editor's small screen, as Chimera's: x from 0 to 244, y from 0 to 180
    readonly property int maxX: 244
    readonly property int maxY: 180

    // what was there when the window opened, put back unless OK is pressed
    property var before: null
    property bool keeping: false
    readonly property var colorProps: ["messagesColor", "alertColor", "lastInputColor", "movieInputColor"]
    readonly property var colorWhich: ["messages", "alert", "lastInput", "movieInput"]
    onVisibleChanged: {
        if (visible) {
            win.keeping = false
            win.picked = "Fps"
            win.before = {
                positions: JSON.parse(win.vm.positions), seconds: win.vm.messageSeconds, stack: win.vm.stackMessages,
                colors: win.colorProps.map(p => win.vm[p])
            }
        } else if (!win.keeping && win.before) {
            for (const n of Object.keys(win.before.positions)) {
                const p = win.before.positions[n]
                win.vm.run("setPosition", [n, p.x, p.y, p.anchor])
            }
            win.colorProps.forEach((p, i) => win.vm.run("setColor", [win.colorWhich[i], win.before.colors[i]]))
            win.vm.messageSeconds = win.before.seconds
            win.vm.stackMessages = win.before.stack
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6
            ColumnLayout {
                Layout.preferredWidth: 190
                Layout.fillHeight: true
                GroupBox {
                    title: qsTrId("messages_config.message_type")
                    Layout.fillWidth: true
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0
                        Repeater {
                            model: win.names
                            delegate: RowLayout {
                                required property string modelData
                                readonly property var p: win.places[modelData] || ({ "x": 0, "y": 0, "anchor": 0 })
                                RadioButton {
                                    text: modelData
                                    checked: win.picked === modelData
                                    onClicked: win.picked = modelData
                                    padding: 1
                                    Layout.fillWidth: true
                                }
                                Label { text: p.x + ", " + p.y }
                            }
                        }
                    }
                }
                GroupBox {
                    title: qsTrId("messages_config.message_colors")
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ColumnLayout {
                        spacing: 2
                        Repeater {
                            model: [qsTrId("messages_config.main_messages"), qsTrId("messages_config.alert_messages"), qsTrId("messages_config.previous_frame_input"), qsTrId("messages_config.movie_input")]
                            delegate: ColumnLayout {
                                required property int index
                                required property string modelData
                                spacing: 1
                                Label { text: modelData }
                                RowLayout {
                                    // a click on the swatch picks another colour, as Chimera's
                                    Rectangle {
                                        width: 46; height: 20
                                        color: win.vm[win.colorProps[index]]
                                        border.color: palette.mid
                                        TapHandler { onTapped: { colorDialog.which = win.colorWhich[index]; colorDialog.selectedColor = win.vm[win.colorProps[index]]; colorDialog.open() } }
                                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                                    }
                                    Label { text: "0x" }
                                    TextField {
                                        readOnly: true
                                        text: win.vm[win.colorProps[index]].replace("#", "")
                                        font.family: "monospace"
                                        Layout.preferredWidth: 80
                                        padding: 2
                                    }
                                }
                            }
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                GroupBox {
                    title: qsTrId("messages_config.position")
                    Layout.alignment: Qt.AlignTop
                    // the corners at the screen's corners, the screen between them, x and y under it
                    GridLayout {
                        columns: 3
                        columnSpacing: 2
                        rowSpacing: 2
                        RadioButton { checked: win.place.anchor === 0; onClicked: win.setPlace(win.place.x, win.place.y, 0); padding: 0; Layout.alignment: Qt.AlignTop }
                        Rectangle {
                            id: screen
                            Layout.rowSpan: 2
                            width: 256; height: 192
                            color: palette.base
                            border.color: palette.mid
                            // the mark where the message goes, counted from its corner
                            readonly property int markX: (win.place.anchor === 1 || win.place.anchor === 3) ? win.maxX - win.place.x : win.place.x
                            readonly property int markY: (win.place.anchor === 2 || win.place.anchor === 3) ? win.maxY - win.place.y : win.place.y
                            Canvas {
                                anchors.fill: parent
                                property int mx: screen.markX
                                property int my: screen.markY
                                onMxChanged: requestPaint()
                                onMyChanged: requestPaint()
                                onPaint: {
                                    const g = getContext("2d")
                                    g.reset()
                                    g.strokeStyle = palette.windowText
                                    g.lineWidth = 1
                                    g.strokeRect(mx + 0.5, my + 0.5, 8, 8)
                                    g.beginPath()
                                    g.moveTo(mx, my); g.lineTo(mx + 8, my + 8)
                                    g.moveTo(mx + 8, my); g.lineTo(mx, my + 8)
                                    g.stroke()
                                }
                            }
                            // a press or a drag puts it there, counted from the corner picked
                            function placeAt(px, py) {
                                let x = Math.max(0, Math.min(win.maxX, Math.round(px)))
                                let y = Math.max(0, Math.min(win.maxY, Math.round(py)))
                                const a = win.place.anchor
                                if (a === 1 || a === 3) x = win.maxX - x
                                if (a === 2 || a === 3) y = win.maxY - y
                                win.setPlace(x, y, a)
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: pressed ? Qt.ArrowCursor : Qt.PointingHandCursor
                                onPressed: (mouse) => screen.placeAt(mouse.x, mouse.y)
                                onPositionChanged: (mouse) => { if (pressed) screen.placeAt(mouse.x, mouse.y) }
                            }
                        }
                        RadioButton { checked: win.place.anchor === 1; onClicked: win.setPlace(win.place.x, win.place.y, 1); padding: 0; Layout.alignment: Qt.AlignTop }
                        RadioButton { checked: win.place.anchor === 2; onClicked: win.setPlace(win.place.x, win.place.y, 2); padding: 0; Layout.alignment: Qt.AlignBottom }
                        RadioButton { checked: win.place.anchor === 3; onClicked: win.setPlace(win.place.x, win.place.y, 3); padding: 0; Layout.alignment: Qt.AlignBottom }
                        RowLayout {
                            Layout.row: 2
                            Layout.column: 1
                            Layout.columnSpan: 2
                            Label { text: "x" }
                            SpinBox { from: 0; to: win.maxX; editable: true; value: win.place.x; onValueModified: win.setPlace(value, win.place.y, win.place.anchor); Layout.preferredWidth: 90 }
                            Label { text: "y" }
                            SpinBox { from: 0; to: win.maxY; editable: true; value: win.place.y; onValueModified: win.setPlace(win.place.x, value, win.place.anchor); Layout.preferredWidth: 90 }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                RowLayout {
                    Label { text: qsTrId("messages_config.messages_fade_after") }
                    SpinBox { from: 1; to: 10; editable: true; value: win.vm.messageSeconds; onValueModified: win.vm.messageSeconds = value; Layout.preferredWidth: 80 }
                    Label { text: qsTrId("messages_config.seconds") }
                }
                CheckBox { text: qsTrId("messages_config.stack_messages"); checked: win.vm.stackMessages; onToggled: win.vm.stackMessages = checked; padding: 2 }
                RowLayout {
                    Button { text: qsTrId("common.restore_defaults"); onClicked: win.vm.run("resetDefaults") }
                    Item { Layout.fillWidth: true }
                    Button { text: qsTrId("common.ok_mnemonic"); onClicked: { win.keeping = true; win.close() } }
                    Button { text: qsTrId("common.cancel_mnemonic"); onClicked: win.close() }
                }
                Label { text: win.vm.error; color: win.themeColor("AccentError", "#b00020"); visible: text !== ""; elide: Text.ElideRight; Layout.fillWidth: true }
            }
        }
    }
    ColorDialog {
        id: colorDialog
        property string which: ""
        options: ColorDialog.ShowAlphaChannel
        onAccepted: {
            const c = selectedColor
            const hex = (v) => ("0" + Math.round(v * 255).toString(16)).slice(-2)
            win.vm.run("setColor", [colorDialog.which, "#" + hex(c.a) + hex(c.r) + hex(c.g) + hex(c.b)])
        }
    }
}
