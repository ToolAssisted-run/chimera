// Chimera's Display Configuration (Config > Display), laid out as Chimera's
// DisplayConfig: Scaling & Filtering (User Prescale and Auto Prescale, the
// final filter, the aspect ratio, cropping, Defaults), Misc (Display
// Features, static window titles, the snowy null core) and
// Window (the chrome windowed and full screen, double-click full screen).
// Its boxes are a copy: OK keeps them, Cancel lets them go. Layout and
// bindings only, over the display view model and the session's switches.
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
    width: 690
    height: 440
    minimumWidth: 690
    minimumHeight: 440
    modality: Qt.ApplicationModal
    transientParent: win.mainWindow

    // the display view model's settings the window shows, and the session's two
    // the idle picture's: Chimera's Snowy null core
    readonly property var eyeNames: ["snowWhen", "snowIntensity", "snowBias", "snowFramerate"]
    readonly property var vmNames: ["finalFilter", "prescale", "autoPrescale", "scaleOsd", "osdFollowsWindow", "osdTextSize", "letterbox", "integerScale", "aspectMode", "customWidth", "customHeight", "ratioX",
        "ratioY", "cropLeft", "cropTop", "cropRight", "cropBottom", "minimalDisplay", "staticWindowTitles", "titleSuffix", "captionWindowed",
        "menuWindowed", "frameWindowed", "saveWindowPosition", "stayOnTop", "mouseCaptureForcesTop", "menuFullscreen", "statusBarFullscreen",
        "autohideMouseFullscreen", "fullscreenHacks", "allowDoubleClickFullscreen"]
    property var picked: ({})
    onVisibleChanged: if (visible) {
        tabs.currentIndex = 0
        const p = {}
        for (const n of win.vmNames) p[n] = win.vm[n]
        for (const n of win.eyeNames) p[n] = win.idleEye[n]
        p.displayNothing = win.session.displayNothing
        p.showStatusBar = win.session.showStatusBar
        win.picked = p
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

    component Box: CheckBox {
        required property string setting
        checked: win.picked[setting] === true
        onToggled: win.pick(setting, checked)
        padding: 2
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
        Layout.preferredWidth: 90
    }
    component NumberBox: SpinBox {
        required property string setting
        from: 0
        to: 10000
        editable: true
        value: win.picked[setting] || 0
        onValueModified: win.pick(setting, value)
        Layout.preferredWidth: 90
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 9
        spacing: 6
        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: qsTrId("display_config.scaling_and_filtering"); width: implicitWidth }
            TabButton { text: qsTrId("display_config.misc"); width: implicitWidth }
            TabButton { text: qsTrId("display_config.window"); width: implicitWidth }
        }
        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            StackLayout {
                anchors.fill: parent
                currentIndex: tabs.currentIndex
                // Scaling & Filtering
                RowLayout {
                    spacing: 8
                    ColumnLayout {
                        Layout.alignment: Qt.AlignTop
                        // Chimera's prescaling: grown pixel by pixel by whole steps before the final filter
                        RowLayout {
                            Label { text: qsTrId("display_config.user_prescale") }
                            NumberBox { setting: "prescale"; from: 1; to: 16 }
                        }
                        Box { setting: "autoPrescale"; text: qsTrId("display_config.auto_prescale") }
                        GroupBox {
                            title: qsTrId("display_config.final_filter")
                            ColumnLayout {
                                spacing: 0
                                RadioButton { text: qsTrId("common.none"); checked: win.picked.finalFilter === 0; onClicked: win.pick("finalFilter", 0); padding: 2 }
                                RadioButton { text: qsTrId("display_config.bilinear"); checked: win.picked.finalFilter === 1; onClicked: win.pick("finalFilter", 1); padding: 2 }
                            }
                        }
                        GroupBox {
                            title: qsTrId("display_config.cropping")
                            GridLayout {
                                columns: 2
                                Label { text: qsTrId("display_config.left") }
                                NumberBox { setting: "cropLeft"; to: 4000 }
                                Label { text: qsTrId("display_config.top") }
                                NumberBox { setting: "cropTop"; to: 4000 }
                                Label { text: qsTrId("display_config.right") }
                                NumberBox { setting: "cropRight"; to: 4000 }
                                Label { text: qsTrId("display_config.bottom") }
                                NumberBox { setting: "cropBottom"; to: 4000 }
                            }
                        }
                        // Chimera's Defaults: no user prescale, Auto Prescale, bilinear, letterboxed, the system's ratio, nothing cropped
                        Button {
                            text: qsTrId("display_config.defaults")
                            onClicked: {
                                const now = Object.assign({}, win.picked)
                                Object.assign(now, { prescale: 1, autoPrescale: true, finalFilter: 1, letterbox: true, aspectMode: 1,
                                    cropLeft: 0, cropTop: 0, cropRight: 0, cropBottom: 0 })
                                win.picked = now
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.alignment: Qt.AlignTop
                        Layout.fillWidth: true
                        Box { setting: "letterbox"; text: qsTrId("display_config.maintain_aspect_ratio_letterbox") }
                        // as Chimera's: the choices below only matter with the ratio kept
                        Box { setting: "integerScale"; text: qsTrId("display_config.expand_pixels_by_integers_only_e"); enabled: win.picked.letterbox === true }
                        GroupBox {
                            title: qsTrId("display_config.aspect_ratio_selection")
                            Layout.fillWidth: true
                            enabled: win.picked.letterbox === true
                            ColumnLayout {
                                spacing: 0
                                RadioButton { text: qsTrId("display_config.use_1_1_pixel_size_for"); checked: win.picked.aspectMode === 0; onClicked: win.pick("aspectMode", 0); padding: 2 }
                                RadioButton { text: qsTrId("display_config.use_system_s_recommendation"); checked: win.picked.aspectMode === 1; onClicked: win.pick("aspectMode", 1); padding: 2 }
                                Label { text: qsTrId("display_config.allow_pixel_distortion_e_g_2x1"); topPadding: 6 }
                                RowLayout {
                                    RadioButton { text: qsTrId("display_config.use_custom_size"); checked: win.picked.aspectMode === 2; onClicked: win.pick("aspectMode", 2); padding: 2 }
                                    NumberBox { setting: "customWidth" }
                                    Label { text: "x" }
                                    NumberBox { setting: "customHeight" }
                                }
                                RowLayout {
                                    RadioButton { text: qsTrId("display_config.use_custom_ar"); checked: win.picked.aspectMode === 3; onClicked: win.pick("aspectMode", 3); padding: 2 }
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
                                }
                            }
                        }
                        Box { setting: "scaleOsd"; text: qsTrId("display_config.scale_osd_text_with_system_scale") }
                        Box { setting: "osdFollowsWindow"; text: qsTrId("display_config.scale_osd_text_with_the_window") }
                        // the size it keeps when it does not grow with the window (issue 488)
                        RowLayout {
                            enabled: win.picked.osdFollowsWindow !== true
                            Label { text: qsTrId("display_config.osd_text_size_in_pixels") }
                            SpinBox {
                                objectName: "osdTextSize"
                                from: 8; to: 64; editable: true
                                value: win.picked.osdTextSize !== undefined ? win.picked.osdTextSize : 12
                                onValueModified: win.pick("osdTextSize", value)
                            }
                        }
                    }
                }
                // Misc
                ColumnLayout {
                    GroupBox {
                        title: qsTrId("display_config.display_features_for_speeding_up_replays")
                        ColumnLayout {
                            spacing: 0
                            RadioButton {
                                text: qsTrId("display_config.full_display_everything"); padding: 2
                                checked: !win.picked.displayNothing && !win.picked.minimalDisplay
                                onClicked: { win.pick("displayNothing", false); win.pick("minimalDisplay", false) }
                            }
                            RadioButton {
                                text: qsTrId("display_config.minimal_display_hud_only_tbd"); padding: 2
                                checked: !win.picked.displayNothing && win.picked.minimalDisplay === true
                                onClicked: { win.pick("displayNothing", false); win.pick("minimalDisplay", true) }
                            }
                            RadioButton {
                                text: qsTrId("display_config.absolute_zero_display_nothing"); padding: 2
                                checked: win.picked.displayNothing === true
                                onClicked: { win.pick("displayNothing", true); win.pick("minimalDisplay", false) }
                            }
                        }
                    }
                    RowLayout {
                        spacing: 16
                        Box { setting: "staticWindowTitles"; text: qsTrId("display_config.keep_window_titles_static") }
                        Label { text: qsTrId("display_config.window_title_suffix_static_and_not") }
                        TextField {
                            text: win.picked.titleSuffix || ""
                            placeholderText: qsTrId("display_config.chimera")
                            onTextEdited: win.pick("titleSuffix", text)
                            Layout.preferredWidth: 80
                        }
                    }
                    Label {
                        text: qsTrId("display_config.some_tools_put_filenames_status_etc")
                        Layout.leftMargin: 20
                    }
                    // Chimera's Snowy null core: the picture with nothing open
                    GroupBox {
                        title: qsTrId("display_config.snowy_null_core")
                        ColumnLayout {
                            spacing: 2
                            Label { text: qsTrId("display_config.when_no_rom_loaded_draw_snow") }
                            RowLayout {
                                RadioButton { text: qsTrId("display_config.always"); checked: win.picked.snowWhen === 1; onClicked: win.pick("snowWhen", 1); padding: 2 }
                                RadioButton { text: qsTrId("display_config.never"); checked: win.picked.snowWhen === 0; onClicked: win.pick("snowWhen", 0); padding: 2 }
                            }
                            RadioButton { text: qsTrId("display_config.during_christmas_dec_20th_through_26th"); checked: win.picked.snowWhen === 2; onClicked: win.pick("snowWhen", 2); padding: 2 }
                            RowLayout {
                                Label { text: qsTrId("display_config.brightness_multiplier") }
                                Tenths { setting: "snowIntensity"; from: 1; to: 10; places: 1 }
                                Label { text: qsTrId("display_config.rng_bias") }
                                Tenths { setting: "snowBias"; from: -8; to: 8; places: 2; unit: 0.25 }
                            }
                            RowLayout {
                                Label { text: qsTrId("display_config.framerate_hz").arg((60 / (win.picked.snowFramerate || 10)).toFixed(1)) }
                                Slider {
                                    from: 1; to: 20; stepSize: 1; snapMode: Slider.SnapAlways
                                    value: win.picked.snowFramerate || 10
                                    onMoved: win.pick("snowFramerate", Math.round(value))
                                    Layout.preferredWidth: 160
                                }
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
                // Window
                ColumnLayout {
                    RowLayout {
                        Layout.alignment: Qt.AlignTop
                        spacing: 8
                        GroupBox {
                            title: qsTrId("display_config.windowed")
                            Layout.alignment: Qt.AlignTop
                            ColumnLayout {
                                spacing: 0
                                Box { setting: "captionWindowed"; text: qsTrId("display_config.caption") }
                                Box { setting: "menuWindowed"; text: qsTrId("display_config.menu") }
                                Box { setting: "showStatusBar"; text: qsTrId("display_config.status_bar") }
                                RowLayout {
                                    Label { text: qsTrId("display_config.frame") }
                                    Slider {
                                        from: 0; to: 2; stepSize: 1; snapMode: Slider.SnapAlways
                                        value: win.picked.frameWindowed !== undefined ? win.picked.frameWindowed : 2
                                        onMoved: win.pick("frameWindowed", value)
                                        Layout.preferredWidth: 100
                                    }
                                    Label { text: [qsTrId("common.none"), qsTrId("display_config.thin"), qsTrId("display_config.thick")][win.picked.frameWindowed !== undefined ? win.picked.frameWindowed : 2] }
                                }
                                Box { setting: "saveWindowPosition"; text: qsTrId("display_config.save_window_position") }
                                // Chimera hides it where it does not work: everywhere but Windows
                                Box {
                                    setting: "stayOnTop"; text: qsTrId("display_config.stay_on_top")
                                    visible: Qt.platform.os === "windows"; height: visible ? implicitHeight : 0
                                }
                                Box {
                                    setting: "mouseCaptureForcesTop"; text: qsTrId("display_config.mouse_capture_forces_stay_on_top")
                                    visible: Qt.platform.os === "windows"; height: visible ? implicitHeight : 0
                                }
                            }
                        }
                        GroupBox {
                            title: qsTrId("display_config.fullscreen")
                            Layout.alignment: Qt.AlignTop
                            ColumnLayout {
                                spacing: 0
                                Box { setting: "menuFullscreen"; text: qsTrId("display_config.menu") }
                                Box { setting: "statusBarFullscreen"; text: qsTrId("display_config.status_bar") }
                                Box { setting: "autohideMouseFullscreen"; text: qsTrId("display_config.auto_hide_mouse_cursor") }
                                // Windows only, as Chimera's: a border kept round the window in full screen
                                Box { setting: "fullscreenHacks"; text: qsTrId("display_config.enable_windows_fullscreen_hacks"); enabled: Qt.platform.os === "windows" }
                                Label {
                                    text: qsTrId("display_config.for_windows_operating_systems_vista_with")
                                    enabled: Qt.platform.os === "windows"
                                    topPadding: 4
                                }
                            }
                        }
                    }
                    Box { setting: "allowDoubleClickFullscreen"; text: qsTrId("display_config.allow_double_click_fullscreen_hold_shift") }
                    Item { Layout.fillHeight: true }
                }
            }
        }
        RowLayout {
            Item { Layout.fillWidth: true }
            Button { text: qsTrId("common.ok"); onClicked: { win.keep(); win.close() } }
            Button { text: qsTrId("common.cancel"); onClicked: win.close() }
        }
    }
}
