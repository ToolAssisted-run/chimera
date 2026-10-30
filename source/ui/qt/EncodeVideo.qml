// Chimera's File > Encode Video: everything asked at once - the file, the
// ffmpeg command or a preset's, the two markers to encode between, the size
// and the sound's sync - then Start Encode winds the run there, encodes it
// and gives the machine back. Closing the window stops it. What it refuses
// is said in the status line in the error colour, as Chimera's. Capture OSD
// and Capture Lua put what the window draws over the picture into the video,
// at the machine's own size. Layout and bindings only, over the encoder view
// model.
import QtQuick
import QtQuick.Controls
import ChimeraDialogs
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "encodeVideoWindow"
    required property QtObject vm
    required property QtObject session
    required property QtObject pianoRoll
    required property QtObject paths
    required property QtObject files
    // the theme's colour for a role (Main hands it its lookup)
    property var themeColor: (role, fallback) => fallback
    title: qsTrId("encode_video.encode_video")
    width: 600
    height: 440
    minimumWidth: 600
    minimumHeight: 420
    flags: Qt.Window | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMinimizeButtonHint | Qt.WindowCloseButtonHint
    property Window mainWindow: null
    transientParent: win.mainWindow
    readonly property bool running: win.vm.phase === "encoding"
    // what the window itself last refused, said over the encode's status until that changes
    property string said: ""
    Connections {
        target: win.vm
        function onValueChanged(key) { if (key === "status") win.said = "" }
    }
    // a count as Chimera's N0 writes it: 1,234
    function grouped(n) { return Number(n).toLocaleString(Qt.locale("en_US"), "f", 0) }
    function fileName(path) { return path.replace(/^.*[\\/]/, "") }
    readonly property string outputFolder: win.vm.output.trim().replace(/[\\/][^\\/]*$/, "")

    // the window opens on the whole run, a file named for the project in the A/V folder
    onVisibleChanged: {
        if (!visible) return
        fromBox.currentIndex = Math.max(0, fromBox.find(qsTrId("encode_video.run_start")))
        toBox.currentIndex = Math.max(0, toBox.find(qsTrId("encode_video.run_end")))
        // a file named for the project (else the game), until one is chosen
        const named = win.vm.output.replace(/^.*[\\/]/, "").replace(/\.[^.]*$/, "")
        if (named === "") {
            const project = win.session.projectPath.replace(/^.*[\\/]/, "").replace(/\.[^.]*$/, "")
            win.vm.run("suggest", [win.paths.run("resolve", ["A/V Dumps"]) || ".",
                                   project !== "" ? project : win.session.gameName !== "" ? win.session.gameName : "video"])
        }
        // not resizing: the size shown is the picture's own
        if (!win.vm.resize || win.vm.width === 0) useCurrentSize()
    }
    onClosing: if (win.running) win.vm.run("stop")
    function useCurrentSize() {
        win.vm.width = picture.pictureWidth > 0 ? picture.pictureWidth : 640
        win.vm.height = picture.pictureHeight > 0 ? picture.pictureHeight : 480
    }
    // the machine's own size now, from the main window's picture
    property Item picture: null
    Timer { interval: 200; repeat: true; running: win.running; onTriggered: win.vm.run("poll") }

    function start() {
        if (win.files.exists(win.vm.output)) replaceDialog.open()
        else win.begin()
    }
    function begin() {
        win.said = ""
        win.vm.run("start")
        if (win.vm.error !== "") win.said = win.vm.error
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        enabled: true

        GridLayout {
            columns: 3
            Layout.fillWidth: true
            enabled: !win.running
            Label { text: qsTrId("encode_video.output_file") }
            TextField { text: win.vm.output; Layout.fillWidth: true; onEditingFinished: win.vm.output = text }
            Button { text: qsTrId("common.browse"); onClicked: saveDialog.open() }
            Label { text: qsTrId("common.preset") }
            ComboBox {
                id: presetBox
                Layout.fillWidth: true
                Layout.columnSpan: 2
                model: win.vm.rows("presets")
                textRole: "key"
                // the preset the command is, or one's own (the language key of its name)
                currentIndex: find(win.vm.preset)
                displayText: qsTrId(win.vm.preset)
                delegate: ItemDelegate {
                    required property int index
                    required property string key
                    width: presetBox.width
                    text: qsTrId(key)
                    highlighted: presetBox.highlightedIndex === index
                }
                onActivated: (index) => win.vm.run("usePreset", [index])
            }
            Label { text: qsTrId("encode_video.ffmpeg_command") }
            TextField {
                Layout.fillWidth: true
                Layout.columnSpan: 2
                text: win.vm.command
                onTextEdited: win.vm.command = text
            }
        }
        RowLayout {
            enabled: !win.running
            Label { text: qsTrId("encode_video.from_marker") }
            ComboBox {
                id: fromBox
                Layout.fillWidth: true
                model: win.pianoRoll.rows("markers")
                textRole: "text"
                valueRole: "frame"
                displayText: currentIndex >= 0 ? currentValue + "  " + currentText : ""
                delegate: ItemDelegate { required property var model; width: fromBox.width; text: model.frame + "  " + model.text }
                onCurrentValueChanged: win.vm.fromFrame = currentValue === undefined ? 0 : currentValue
            }
            Label { text: qsTrId("encode_video.to_marker") }
            ComboBox {
                id: toBox
                Layout.fillWidth: true
                model: win.pianoRoll.rows("markers")
                textRole: "text"
                valueRole: "frame"
                displayText: currentIndex >= 0 ? currentValue + "  " + currentText : ""
                delegate: ItemDelegate { required property var model; width: toBox.width; text: model.frame + "  " + model.text }
                onCurrentValueChanged: win.vm.toFrame = currentValue === undefined ? 0 : currentValue
            }
        }
        Label {
            readonly property int frames: win.vm.toFrame - win.vm.fromFrame + 1
            text: frames > 0 ? qsTrId("encode_video.frames_to_frames").arg(win.vm.fromFrame).arg(win.vm.toFrame).arg(win.grouped(frames))
                             : qsTrId("encode_video.the_end_marker_is_before_the")
            color: frames > 0 ? palette.placeholderText : win.themeColor("AccentError", "#b00020")
        }
        RowLayout {
            enabled: !win.running
            CheckBox { text: qsTrId("encode_video.resize_to"); checked: win.vm.resize; onToggled: win.vm.resize = checked }
            SpinBox { from: 1; to: 8192; editable: true; value: win.vm.width; enabled: win.vm.resize; onValueModified: win.vm.width = value; Layout.preferredWidth: 100 }
            Label { text: "x" }
            SpinBox { from: 1; to: 8192; editable: true; value: win.vm.height; enabled: win.vm.resize; onValueModified: win.vm.height = value; Layout.preferredWidth: 100 }
            Button { text: qsTrId("encode_video.current_size"); enabled: win.vm.resize; onClicked: win.useCurrentSize() }
            Label { text: win.picture && win.picture.pictureWidth > 0 ? qsTrId("encode_video.now_x").arg(win.picture.pictureWidth).arg(win.picture.pictureHeight) : "" }
            CheckBox { text: qsTrId("encode_video.pad"); checked: win.vm.pad; enabled: win.vm.resize; onToggled: win.vm.pad = checked }
        }
        RowLayout {
            CheckBox { text: qsTrId("encode_video.sync_to_audio"); enabled: !win.running; checked: win.vm.audioSync; onToggled: win.vm.audioSync = checked }
            CheckBox { text: qsTrId("encode_video.capture_osd"); enabled: !win.running; checked: win.vm.captureOsd; onToggled: win.vm.captureOsd = checked }
            CheckBox { text: qsTrId("encode_video.capture_lua"); enabled: !win.running; checked: win.vm.captureLua; onToggled: win.vm.captureLua = checked }
        }

        ProgressBar {
            Layout.fillWidth: true
            from: 0
            to: Math.max(1, win.vm.frameCount)
            value: win.vm.framesDone
        }
        Label {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            objectName: "encodeStatus"
            text: win.said !== "" ? win.said : win.vm.status
            color: win.said !== "" || win.vm.phase === "failed" ? win.themeColor("AccentError", "#b00020") : palette.windowText
        }
        // the speakers are quiet while a video is written: these say sound is going in
        RowLayout {
            Label { text: qsTrId("encode_video.audio") }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                ProgressBar { Layout.fillWidth: true; from: 0; to: 1; value: win.vm.levelLeft }
                ProgressBar { Layout.fillWidth: true; from: 0; to: 1; value: win.vm.levelRight }
            }
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            Button {
                text: qsTrId("encode_video.open_video")
                // what the last encode wrote, done or stopped; nothing while another is being written
                enabled: !win.running && win.vm.written !== "" && (win.vm.phase === "done" || win.vm.phase === "stopped")
                onClicked: {
                    if (!win.files.exists(win.vm.written)) win.said = qsTrId("encode_video.is_no_longer_there").arg(win.fileName(win.vm.written))
                    else Qt.openUrlExternally(win.files.folderUrl(win.vm.written))
                }
            }
            Button {
                text: qsTrId("common.open_folder")
                enabled: win.outputFolder !== "" && win.files.exists(win.outputFolder)
                onClicked: {
                    if (!win.files.exists(win.outputFolder)) win.said = qsTrId("encode_video.the_output_file_s_folder_does")
                    else Qt.openUrlExternally(win.files.folderUrl(win.outputFolder))
                }
            }
            Button { text: qsTrId("common.restore_defaults"); enabled: !win.running; onClicked: win.vm.run("restoreDefaults") }
            Item { Layout.fillWidth: true }
            Button { text: qsTrId("encode_video.start_encode"); enabled: !win.running && win.vm.toFrame >= win.vm.fromFrame; onClicked: win.start() }
            Button { text: qsTrId("common.stop"); enabled: win.running; onClicked: win.vm.run("stop") }
            Button { text: qsTrId("common.close"); onClicked: win.close() }
        }
    }

    FileDialog {
        id: saveDialog
        title: qsTrId("encode_video.write_the_video_to")
        fileMode: FileDialog.SaveFile
        // where the file is now, and its name, as Chimera's opens
        currentFolder: win.outputFolder !== "" ? win.files.folderUrl(win.outputFolder) : ""
        selectedFile: win.vm.output !== "" ? win.files.folderUrl(win.vm.output) : ""
        defaultSuffix: "mp4"
        nameFilters: [qsTrId("encode_video.mp4_files_mp4"), qsTrId("encode_video.avi_files_avi"), qsTrId("encode_video.matroska_files_mkv"), qsTrId("encode_video.all_files")]
        onAccepted: win.vm.output = win.files.localPath(selectedFile)
    }
    // a video already at that name is not replaced without asking, as Chimera's
    Dialog {
        id: replaceDialog
        title: qsTrId("encode_video.replace_the_video")
        width: 420
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTrId("encode_video.already_exists_replace_it_with_this").arg(win.vm.output.replace(/^.*[\\/]/, ""))
        }
        onAccepted: win.begin()
        onRejected: win.said = qsTrId("encode_video.was_kept_pick_another_name_to").arg(win.fileName(win.vm.output))
    }
}
