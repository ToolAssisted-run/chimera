// Chimera's Display Configuration (Config > Display): Scaling & Filtering
// (prescaling, the final filter, the aspect ratio, cropping, the text over the
// picture, Defaults), Misc (Display Features, window titles, the snowy null
// core) and Window (the chrome windowed and full screen, double-click full
// screen). Each tab is a grid of rows, as a core's settings are (issue 527):
// the option's name and its control side by side, under headings, and what
// the row under the pointer, or else the row last chosen, does and takes in a
// box under the grid. Its values are a copy: OK keeps them, Cancel lets them
// go. Layout and bindings only, over the display view model and the
// session's switches.
//
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "displayWindow"
    required property QtObject vm
    required property QtObject session
    required property QtObject idleEye
    property Window mainWindow: null
    title: qsTrId("display_config.display_configuration")
    width: 720
    height: 560
    minimumWidth: 520
    minimumHeight: 440
    modality: Qt.platform.os === "osx" ? Qt.WindowModal : Qt.ApplicationModal // a sheet on a Mac, which keeps an application-modal window over other apps (#541)
    transientParent: win.mainWindow

    // the display view model's settings the window shows, and the session's two
    // the idle picture's: Chimera's Snowy null core
    readonly property var eyeNames: ["snowWhen", "snowIntensity", "snowBias", "snowFramerate"]
    readonly property var vmNames: ["finalFilter", "prescale", "autoPrescale", "scaleOsd", "osdFollowsWindow", "osdTextSize", "letterbox", "integerScale", "aspectMode", "customWidth", "customHeight", "ratioX",
        "ratioY", "cropLeft", "cropTop", "cropRight", "cropBottom", "minimalDisplay", "staticWindowTitles", "titleSuffix", "captionWindowed",
        "menuWindowed", "frameWindowed", "saveWindowPosition", "stayOnTop", "mouseCaptureForcesTop", "menuFullscreen", "statusBarFullscreen",
        "autohideMouseFullscreen", "fullscreenHacks", "allowDoubleClickFullscreen"]
    readonly property bool onWindows: Qt.platform.os === "windows"
    property var picked: ({})
    // the row last chosen (clicked, or its control used) and the row under the pointer
    property Item chosen: null
    property Item hovered: null
    readonly property Item described: win.hovered !== null ? win.hovered : win.chosen
    onVisibleChanged: if (visible) {
        tabs.currentIndex = 0
        win.chosen = null
        const p = {}
        for (const n of win.vmNames) p[n] = win.vm[n]
        for (const n of win.eyeNames) p[n] = win.idleEye[n]
        p.displayNothing = win.session.displayNothing
        p.showStatusBar = win.session.showStatusBar
        win.picked = p
    }
    // a control given the keyboard chooses its row
    onActiveFocusItemChanged: {
        for (let item = win.activeFocusItem; item !== null; item = item.parent)
            if (item.isOptionRow === true) { win.chosen = item; return }
    }
    function pick(name, value) {
        const now = Object.assign({}, win.picked)
        now[name] = value
        win.picked = now
    }
    function keep() {
        for (const n of win.vmNames) win.vm[n] = win.picked[n]
        for (const n of win.eyeNames) win.idleEye[n] = win.picked[n]
        win.session.displayNothing = win.picked.displayNothing
        win.session.showStatusBar = win.picked.showStatusBar
    }

    FontMetrics { id: metrics }

    // one option: its name in the left half, its control in the right, a line under it
    component OptionRow: Item {
        id: row
        property string label
        property string about
        default property alias control: holder.data
        readonly property bool isChosen: win.chosen === row
        readonly property bool isOptionRow: true
        Layout.fillWidth: true
        implicitHeight: Math.max(name.implicitHeight + 10, holder.implicitHeight + 6) + 1
        HoverHandler {
            onHoveredChanged: if (hovered) win.hovered = row; else if (win.hovered === row) win.hovered = null
        }
        // a click anywhere on the row, or its control used, chooses it
        TapHandler { gesturePolicy: TapHandler.DragThreshold; grabPermissions: PointerHandler.ApprovesTakeOverByAnything; onTapped: win.chosen = row }
        Rectangle {
            id: nameCell
            width: Math.round(row.width * 0.52)
            height: parent.height - 1
            color: row.isChosen ? palette.highlight : "transparent"
            Label {
                id: name
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 4
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                text: row.label
                color: row.isChosen ? palette.highlightedText : palette.text
                opacity: row.enabled ? 1 : 0.5
            }
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: palette.mid }
        }
        RowLayout {
            id: holder
            anchors.left: nameCell.right
            anchors.right: parent.right
            anchors.leftMargin: 6
            anchors.rightMargin: 6
            anchors.verticalCenter: nameCell.verticalCenter
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: palette.mid }
    }
    // a heading over the rows that belong together
    component Heading: Rectangle {
        property alias text: heading.text
        Layout.fillWidth: true
        implicitHeight: heading.implicitHeight + 10
        color: palette.alternateBase
        Label { id: heading; anchors.fill: parent; anchors.leftMargin: 6; verticalAlignment: Text.AlignVCenter; font.bold: true }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: palette.mid }
    }
    // a tab's rows, scrolled when the window is short
    component OptionGrid: Rectangle {
        default property alias rows: column.data
        color: palette.base
        border.color: palette.mid
        ScrollView {
            id: scroller
            anchors.fill: parent
            anchors.margins: 1
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                id: column
                width: scroller.availableWidth
                spacing: 0
            }
        }
    }

    component Box: CheckBox {
        required property string setting
        checked: win.picked[setting] === true
        onToggled: win.pick(setting, checked)
        padding: 2
    }
    // one of a setting's values, by number
    component Choice: ComboBox {
        required property string setting
        currentIndex: win.picked[setting] !== undefined ? win.picked[setting] : -1
        onActivated: (i) => win.pick(setting, i)
        Layout.fillWidth: true
    }
    // a number with decimals, as Chimera's boxes: counted in units (a tenth, a quarter)
    component Tenths: SpinBox {
        id: tenths
        required property string setting
        property int places: 1
        property real unit: Math.pow(10, -places)
        editable: true
        value: Math.round((win.picked[setting] || 0) / unit)
        onValueModified: win.pick(setting, value * unit)
        // parseFloat, not Number(): a component named Number here once took that name (issue 518)
        textFromValue: (v, locale) => (v * tenths.unit).toFixed(tenths.places)
        valueFromText: (text, locale) => Math.round(parseFloat(text) / tenths.unit)
        validator: DoubleValidator { decimals: tenths.places }
        Layout.preferredWidth: 110
    }
    component NumberBox: SpinBox {
        required property string setting
        from: 0
        to: 10000
        editable: true
        value: win.picked[setting] || 0
        onValueModified: win.pick(setting, value)
        Layout.preferredWidth: 110
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 9
        spacing: 6
        TabBar {
            id: tabs
            Layout.fillWidth: true
            onCurrentIndexChanged: { win.chosen = null; win.hovered = null }
            TabButton { text: qsTrId("display_config.scaling_and_filtering"); width: implicitWidth }
            TabButton { text: qsTrId("display_config.misc"); width: implicitWidth }
            TabButton { text: qsTrId("display_config.window"); width: implicitWidth }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex
            // Scaling & Filtering
            OptionGrid {
                Heading { text: qsTrId("display_config.scaling") }
                OptionRow {
                    label: qsTrId("display_config.user_prescale_row"); about: qsTrId("display_config.prescale_about")
                    NumberBox { setting: "prescale"; from: 1; to: 16 }
                }
                OptionRow { label: qsTrId("display_config.auto_prescale"); about: qsTrId("display_config.auto_prescale_about"); Box { setting: "autoPrescale" } }
                OptionRow {
                    label: qsTrId("display_config.final_filter"); about: qsTrId("display_config.final_filter_about")
                    Choice { setting: "finalFilter"; model: [qsTrId("common.none"), qsTrId("display_config.bilinear")] }
                }
                Heading { text: qsTrId("display_config.aspect_ratio") }
                OptionRow {
                    label: qsTrId("display_config.maintain_aspect_ratio_letterbox"); about: qsTrId("display_config.letterbox_about")
                    Box { setting: "letterbox" }
                }
                // as Chimera's: the choices below only matter with the ratio kept
                OptionRow {
                    label: qsTrId("display_config.expand_pixels_by_integers_only_e"); about: qsTrId("display_config.integer_scale_about")
                    enabled: win.picked.letterbox === true
                    Box { setting: "integerScale" }
                }
                OptionRow {
                    label: qsTrId("display_config.aspect_ratio"); about: qsTrId("display_config.aspect_mode_about")
                    enabled: win.picked.letterbox === true
                    Choice {
                        setting: "aspectMode"
                        model: [qsTrId("display_config.use_1_1_pixel_size_for"), qsTrId("display_config.use_system_s_recommendation"),
                            qsTrId("display_config.custom_size"), qsTrId("display_config.custom_ratio")]
                    }
                }
                OptionRow {
                    label: qsTrId("display_config.custom_size"); about: qsTrId("display_config.custom_size_about")
                    enabled: win.picked.letterbox === true && win.picked.aspectMode === 2
                    NumberBox { setting: "customWidth" }
                    Label { text: "x" }
                    NumberBox { setting: "customHeight" }
                    Item { Layout.fillWidth: true }
                }
                OptionRow {
                    label: qsTrId("display_config.custom_ratio"); about: qsTrId("display_config.custom_ratio_about")
                    enabled: win.picked.letterbox === true && win.picked.aspectMode === 3
                    TextField {
                        text: win.picked.ratioX !== undefined ? String(win.picked.ratioX) : ""
                        validator: DoubleValidator { bottom: 0.01 }
                        onEditingFinished: win.pick("ratioX", parseFloat(text))
                        Layout.preferredWidth: 64
                    }
                    Label { text: ":" }
                    TextField {
                        text: win.picked.ratioY !== undefined ? String(win.picked.ratioY) : ""
                        validator: DoubleValidator { bottom: 0.01 }
                        onEditingFinished: win.pick("ratioY", parseFloat(text))
                        Layout.preferredWidth: 64
                    }
                    Item { Layout.fillWidth: true }
                }
                Heading { text: qsTrId("display_config.cropping") }
                OptionRow { label: qsTrId("display_config.crop_left"); about: qsTrId("display_config.crop_about"); NumberBox { setting: "cropLeft"; to: 4000 } }
                OptionRow { label: qsTrId("display_config.crop_top"); about: qsTrId("display_config.crop_about"); NumberBox { setting: "cropTop"; to: 4000 } }
                OptionRow { label: qsTrId("display_config.crop_right"); about: qsTrId("display_config.crop_about"); NumberBox { setting: "cropRight"; to: 4000 } }
                OptionRow { label: qsTrId("display_config.crop_bottom"); about: qsTrId("display_config.crop_about"); NumberBox { setting: "cropBottom"; to: 4000 } }
                Heading { text: qsTrId("display_config.text_over_the_picture") }
                OptionRow {
                    label: qsTrId("display_config.scale_osd_text_with_system_scale"); about: qsTrId("display_config.scale_osd_about")
                    Box { setting: "scaleOsd" }
                }
                OptionRow {
                    label: qsTrId("display_config.scale_osd_text_with_the_window"); about: qsTrId("display_config.osd_follows_window_about")
                    Box { setting: "osdFollowsWindow" }
                }
                // the size it keeps when it does not grow with the window (issue 488)
                OptionRow {
                    label: qsTrId("display_config.osd_text_size_in_pixels"); about: qsTrId("display_config.osd_text_size_about")
                    enabled: win.picked.osdFollowsWindow !== true
                    SpinBox {
                        objectName: "osdTextSize"
                        from: 8; to: 64; editable: true
                        value: win.picked.osdTextSize !== undefined ? win.picked.osdTextSize : 12
                        onValueModified: win.pick("osdTextSize", value)
                        Layout.preferredWidth: 110
                    }
                }
            }
            // Misc
            OptionGrid {
                Heading { text: qsTrId("display_config.replays") }
                OptionRow {
                    label: qsTrId("display_config.display_features"); about: qsTrId("display_config.display_features_about")
                    ComboBox {
                        model: [qsTrId("display_config.full_display_everything"), qsTrId("display_config.minimal_display_hud_only_tbd"),
                            qsTrId("display_config.absolute_zero_display_nothing")]
                        currentIndex: win.picked.displayNothing === true ? 2 : win.picked.minimalDisplay === true ? 1 : 0
                        onActivated: (i) => { win.pick("displayNothing", i === 2); win.pick("minimalDisplay", i === 1) }
                        Layout.fillWidth: true
                    }
                }
                Heading { text: qsTrId("display_config.window_titles") }
                OptionRow {
                    label: qsTrId("display_config.keep_window_titles_static"); about: qsTrId("display_config.static_window_titles_about")
                    Box { setting: "staticWindowTitles" }
                }
                OptionRow {
                    label: qsTrId("display_config.window_title_suffix"); about: qsTrId("display_config.title_suffix_about")
                    TextField {
                        text: win.picked.titleSuffix || ""
                        placeholderText: qsTrId("display_config.chimera")
                        onTextEdited: win.pick("titleSuffix", text)
                        Layout.fillWidth: true
                    }
                }
                // Chimera's Snowy null core: the picture with nothing open
                Heading { text: qsTrId("display_config.snowy_null_core") }
                OptionRow {
                    label: qsTrId("display_config.draw_snow"); about: qsTrId("display_config.snow_when_about")
                    ComboBox {
                        // the view model's numbers: 0 never, 1 always, 2 the week of Christmas
                        model: [qsTrId("display_config.never"), qsTrId("display_config.always"), qsTrId("display_config.during_christmas_dec_20th_through_26th")]
                        currentIndex: win.picked.snowWhen !== undefined ? win.picked.snowWhen : -1
                        onActivated: (i) => win.pick("snowWhen", i)
                        Layout.fillWidth: true
                    }
                }
                OptionRow {
                    label: qsTrId("display_config.snow_brightness"); about: qsTrId("display_config.snow_intensity_about")
                    enabled: win.picked.snowWhen !== 0
                    Tenths { setting: "snowIntensity"; from: 1; to: 10; places: 1 }
                }
                OptionRow {
                    label: qsTrId("display_config.snow_bias"); about: qsTrId("display_config.snow_bias_about")
                    enabled: win.picked.snowWhen !== 0
                    Tenths { setting: "snowBias"; from: -8; to: 8; places: 2; unit: 0.25 }
                }
                OptionRow {
                    label: qsTrId("display_config.snow_frame_rate"); about: qsTrId("display_config.snow_framerate_about")
                    enabled: win.picked.snowWhen !== 0
                    Slider {
                        from: 1; to: 20; stepSize: 1; snapMode: Slider.SnapAlways
                        value: win.picked.snowFramerate || 10
                        onMoved: win.pick("snowFramerate", Math.round(value))
                        Layout.fillWidth: true
                    }
                    Label { text: qsTrId("display_config.framerate_hz").arg((60 / (win.picked.snowFramerate || 10)).toFixed(1)) }
                }
            }
            // Window
            OptionGrid {
                Heading { text: qsTrId("display_config.windowed") }
                OptionRow { label: qsTrId("display_config.caption"); about: qsTrId("display_config.caption_windowed_about"); Box { setting: "captionWindowed" } }
                OptionRow { label: qsTrId("display_config.menu"); about: qsTrId("display_config.menu_windowed_about"); Box { setting: "menuWindowed" } }
                OptionRow { label: qsTrId("display_config.status_bar"); about: qsTrId("display_config.status_bar_windowed_about"); Box { setting: "showStatusBar" } }
                OptionRow {
                    label: qsTrId("display_config.frame_row"); about: qsTrId("display_config.frame_windowed_about")
                    Choice { setting: "frameWindowed"; model: [qsTrId("common.none"), qsTrId("display_config.thin"), qsTrId("display_config.thick")] }
                }
                OptionRow {
                    label: qsTrId("display_config.save_window_position"); about: qsTrId("display_config.save_window_position_about")
                    Box { setting: "saveWindowPosition" }
                }
                // Chimera hides these where they do not work: everywhere but Windows
                OptionRow {
                    label: qsTrId("display_config.stay_on_top"); about: qsTrId("display_config.stay_on_top_about")
                    visible: win.onWindows
                    Box { setting: "stayOnTop" }
                }
                OptionRow {
                    label: qsTrId("display_config.mouse_capture_forces_stay_on_top"); about: qsTrId("display_config.mouse_capture_forces_top_about")
                    visible: win.onWindows
                    Box { setting: "mouseCaptureForcesTop" }
                }
                Heading { text: qsTrId("display_config.fullscreen") }
                OptionRow { label: qsTrId("display_config.menu"); about: qsTrId("display_config.menu_fullscreen_about"); Box { setting: "menuFullscreen" } }
                OptionRow { label: qsTrId("display_config.status_bar"); about: qsTrId("display_config.status_bar_fullscreen_about"); Box { setting: "statusBarFullscreen" } }
                OptionRow {
                    label: qsTrId("display_config.auto_hide_mouse_cursor"); about: qsTrId("display_config.autohide_mouse_about")
                    Box { setting: "autohideMouseFullscreen" }
                }
                // Windows only, as Chimera's: a border kept round the window in full screen (issue 528)
                OptionRow {
                    label: qsTrId("display_config.enable_windows_fullscreen_hacks"); about: qsTrId("display_config.fullscreen_hacks_about")
                    visible: win.onWindows
                    Box { setting: "fullscreenHacks" }
                }
                OptionRow {
                    label: qsTrId("display_config.double_click_fullscreen"); about: qsTrId("display_config.double_click_fullscreen_about")
                    Box { setting: "allowDoubleClickFullscreen" }
                }
            }
        }
        // what the row under the pointer, or the row chosen, does: five lines tall, scrolled when longer
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: metrics.lineSpacing * 5 + 12
            TextArea {
                objectName: "displayDescription"
                text: win.described === null ? qsTrId("display_config.point_at_an_option")
                    : win.described.label + "\n\n" + win.described.about
                readOnly: true
                wrapMode: TextEdit.Wrap
                activeFocusOnTab: false
                background: Rectangle { color: palette.base; border.color: palette.mid }
            }
        }
        RowLayout {
            // Chimera's Defaults: no user prescale, Auto Prescale, bilinear, letterboxed, the system's ratio, nothing cropped
            Button {
                text: qsTrId("display_config.defaults")
                visible: tabs.currentIndex === 0
                onClicked: {
                    const now = Object.assign({}, win.picked)
                    Object.assign(now, { prescale: 1, autoPrescale: true, finalFilter: 1, letterbox: true, aspectMode: 1,
                        cropLeft: 0, cropTop: 0, cropRight: 0, cropBottom: 0 })
                    win.picked = now
                }
            }
            Item { Layout.fillWidth: true }
            Button { text: qsTrId("common.ok"); onClicked: { win.keep(); win.close() } }
            Button { text: qsTrId("common.cancel"); onClicked: win.close() }
        }
    }
}
