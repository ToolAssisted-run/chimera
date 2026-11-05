// Chimera's Customize window (Config > Customize), laid out as Chimera's
// GuiOptions: General (the context menu, the save prompts, listening in
// the background, single instance, how keys are named, the GameCube
// adapter, Startup Options) and Advanced (lag frames, Lua when turboing,
// Key Priority). The boxes are a copy: OK keeps them, Cancel lets them go.
// Layout and bindings only, over the customize view model and the
// session's and hotkeys' switches.
//
// Run in background and Pause when menu activated are gone, as from
// Chimera (owner, 2026-10-24): nothing pauses the machine but the person,
// whatever window is in front or menu open.
import QtQuick
import QtQuick.Controls
import ChimeraDialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "customizeWindow"
    required property QtObject vm
    required property QtObject session
    required property QtObject hotkeys
    required property QtObject controls
    property Window mainWindow: null
    title: qsTrId("customize.customization_options")
    // Chimera's is 418 wide for its 8-point font; wide enough here for the longest line
    width: 520
    height: 520
    minimumWidth: 520
    minimumHeight: 520
    modality: Qt.ApplicationModal
    transientParent: win.mainWindow

    // the copy, read when the window opens
    property var picked: ({})
    onVisibleChanged: if (visible) {
        tabs.currentIndex = 0
        win.picked = {
            showContextMenu: win.vm.showContextMenu, neverAskSave: win.vm.neverAskSave,
            acceptBackgroundInput: win.session.acceptBackgroundInput, startPaused: win.vm.startPaused,
            startFullscreen: win.vm.startFullscreen, skipLagFrames: win.session.skipLagFrames,
            luaDuringTurbo: win.vm.luaDuringTurbo, noMixedPriority: win.hotkeys.noMixedPriority,
            alternateLayouts: win.vm.alternateLayouts, mergeModifiers: win.vm.mergeModifiers,
            singleInstance: win.vm.singleInstance, gameCubeAdapter: win.vm.gameCubeAdapter
        }
    }
    function pick(name, value) {
        const now = Object.assign({}, win.picked)
        now[name] = value
        win.picked = now
    }
    function keep() {
        const p = win.picked
        win.vm.showContextMenu = p.showContextMenu
        win.vm.neverAskSave = p.neverAskSave
        win.session.acceptBackgroundInput = p.acceptBackgroundInput
        win.vm.startPaused = p.startPaused
        win.vm.startFullscreen = p.startFullscreen
        win.session.skipLagFrames = p.skipLagFrames
        win.vm.luaDuringTurbo = p.luaDuringTurbo
        win.hotkeys.noMixedPriority = p.noMixedPriority
        win.vm.alternateLayouts = p.alternateLayouts
        win.vm.mergeModifiers = p.mergeModifiers
        win.vm.singleInstance = p.singleInstance
        win.vm.gameCubeAdapter = p.gameCubeAdapter
    }
    // OK, as Chimera's: a change to merging the modifier keys first asks
    // whether the keys bound are rewritten for it (Cancel keeps the window open)
    function ok() {
        if (win.picked.mergeModifiers !== win.vm.mergeModifiers) { rewriteAsk.open(); return }
        win.keep()
        win.close()
    }
    MessageDialog {
        id: rewriteAsk
        title: qsTrId("customize.rewrite_keybinds_now")
        text: qsTrId("customize.would_you_like_to_replace_and")
            .arg(win.picked.mergeModifiers ? qsTrId("customize.lshift_and_rshift_with_shift") : qsTrId("customize.shift_with_lshift"))
        buttons: MessageDialog.Yes | MessageDialog.No | MessageDialog.Cancel
        onButtonClicked: (button, role) => {
            if (button === MessageDialog.Cancel) return
            const merging = win.picked.mergeModifiers
            win.keep()
            if (button === MessageDialog.Yes) {
                const names = win.vm.run("modifierKeys", [merging])
                win.hotkeys.run("replaceKeys", [names])
                win.controls.run("replaceKeys", [names])
            }
            win.close()
        }
    }

    component Box: CheckBox {
        required property string setting
        checked: win.picked[setting] === true
        onToggled: win.pick(setting, checked)
        padding: 2
    }
    component Note: Label { Layout.leftMargin: 20; wrapMode: Text.WordWrap; Layout.fillWidth: true }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 9
        spacing: 6
        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: qsTrId("customize.general"); width: implicitWidth }
            TabButton { text: qsTrId("customize.advanced"); width: implicitWidth }
        }
        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            StackLayout {
                anchors.fill: parent
                currentIndex: tabs.currentIndex
                ColumnLayout {
                    spacing: 2
                    Box { setting: "showContextMenu"; text: qsTrId("customize.enable_context_menu") }
                    Box { setting: "neverAskSave"; text: qsTrId("customize.when_chimera_is_closing_skip_unsaved") }
                    RowLayout {
                        spacing: 16
                        Box { setting: "acceptBackgroundInput"; text: qsTrId("customize.accept_background_input") }
                        // the keyboard is only ever heard by the window in front here: background input is the controllers'
                        CheckBox {
                            text: qsTrId("customize.from_controller_only")
                            checked: true
                            enabled: false
                            padding: 2
                            ToolTip.visible: hovered
                            ToolTip.text: qsTrId("customize.the_keyboard_is_only_ever_heard")
                        }
                    }
                    Note { text: qsTrId("customize.eavesdrops_on_keyboard_and_gamepad_input") }
                    // Windows only, as Chimera's: read when the program starts
                    Box { setting: "singleInstance"; text: qsTrId("customize.single_instance_mode"); enabled: Qt.platform.os === "windows"; Layout.topMargin: 8 }
                    Box { setting: "alternateLayouts"; text: qsTrId("customize.handle_alternate_keyboard_layouts_e_g"); Layout.topMargin: 8 }
                    Box { setting: "mergeModifiers"; text: qsTrId("customize.merge_l_r_modifier_keys_e") }
                    // read when the program starts: SDL is told before it starts
                    Box { setting: "gameCubeAdapter"; text: qsTrId("customize.enable_wii_u_switch_gamecube_adapter") }
                    GroupBox {
                        title: qsTrId("customize.startup_options")
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        ColumnLayout {
                            spacing: 2
                            Box { setting: "startPaused"; text: qsTrId("customize.start_paused") }
                            Box { setting: "startFullscreen"; text: qsTrId("customize.start_in_fullscreen") }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
                ColumnLayout {
                    spacing: 2
                    Box { setting: "skipLagFrames"; text: qsTrId("customize.frame_advance_button_skips_non_input") }
                    Note { text: qsTrId("customize.when_enabled_the_frame_advance_button") }
                    Box { setting: "luaDuringTurbo"; text: qsTrId("customize.run_lua_scripts_when_turboing"); Layout.topMargin: 8 }
                    Box { setting: "noMixedPriority"; text: qsTrId("customize.key_priority_toggle_remove_mixed_key"); Layout.topMargin: 8 }
                    Item { Layout.fillHeight: true }
                }
            }
        }
        RowLayout {
            Item { Layout.fillWidth: true }
            Button { text: qsTrId("common.ok_mnemonic"); onClicked: win.ok() }
            Button { text: qsTrId("common.cancel_mnemonic"); onClicked: win.close() }
        }
    }
}
