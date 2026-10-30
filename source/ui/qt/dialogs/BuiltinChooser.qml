// Chimera's own file and folder chooser, drawn inside the window that opens
// it: what FileDialog and FolderDialog show on Linux when the desktop has no
// chooser to ask through its portal (docs/file-picker.md). A path can be
// typed or pasted, hidden files shown, and the root, home, the person's
// folders, the desktop's bookmarks and the mounted drives are beside the
// list (filePlaces, file_places.cpp). A folder chooser picks the folder it
// shows.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.folderlistmodel

Dialog {
    id: chooser

    // "open", "openMany", "save" or "folder"
    property string mode: "open"
    property url folder
    // a save's file name, as the dialog opens
    property string name
    property var nameFilters: []
    property string defaultSuffix
    // a tick box the dialog carries, such as Include sub-folders: its label
    // (none when empty), and whether it is ticked
    property string checkBoxText
    property bool checkBoxChecked: false
    // files (or the one folder) picked; then the dialog closes
    signal chosen(var urls)
    signal cancelled()

    readonly property bool picksFolder: mode === "folder"
    readonly property bool saves: mode === "save"
    readonly property bool hasPlaces: typeof filePlaces !== "undefined" && filePlaces !== null
    property string note
    // a save over a file there, waiting for Replace
    property url replacing
    property bool answered: false
    readonly property string iconRoot: "qrc:/qt-project.org/imports/QtQuick/Dialogs/quickimpl/images/"

    modal: true
    focus: true
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 24 : 760, 760)
    height: Math.min(parent ? parent.height - 24 : 520, 520)
    padding: 8
    closePolicy: Popup.CloseOnEscape

    onAboutToShow: {
        answered = false
        note = ""
        replacing = ""
        placeList.clear()
        const all = hasPlaces ? filePlaces.all() : [{ name: "/", url: "file:///", kind: "root" }]
        // the list's sections: the places, then bookmarks, then drives
        for (const p of all)
            placeList.append({ name: p.name, url: p.url.toString(), kind: p.kind, group: p.kind === "bookmark" || p.kind === "drive" ? p.kind : "place" })
        go(folder)
        nameField.text = name
        location.forceActiveFocus()
    }
    onClosed: if (!answered) { answered = true; cancelled() }

    function pathOf(url) { return hasPlaces ? filePlaces.pathOf(url) : decodeURIComponent(url.toString().replace(/^file:\/\//, "")) }
    function parentOf(url) {
        if (hasPlaces) return filePlaces.parentOf(url)
        const s = url.toString().replace(/\/+$/, "")
        const at = s.lastIndexOf("/")
        return at <= "file://".length ? "file:///" : s.substring(0, at)
    }
    function resolve(text) {
        if (hasPlaces) return filePlaces.resolve(text, folder)
        // no places to ask (the theme test): what was typed, as it is
        const url = text.startsWith("/") ? "file://" + text : folder.toString().replace(/\/$/, "") + "/" + text
        return { url: url, exists: true, isFolder: false, parentExists: true }
    }
    function go(url) {
        if (!url || url.toString() === "") url = hasPlaces ? filePlaces.resolve("~", "").url : "file:///"
        folder = url
        location.text = pathOf(url)
        files.currentIndex = -1
        files.picked = []
        note = ""
        replacing = ""
    }
    function choose(urls) {
        answered = true
        chosen(urls)
        close()
    }
    // the patterns in a name filter's brackets, as Qt reads them
    function globsOf(filter) {
        const m = /\(([^()]*)\)\s*$/.exec(filter)
        return (m ? m[1] : filter).split(/[\s;]+/).filter(g => g.length > 0)
    }
    function withSuffix(url) {
        const suffix = defaultSuffix.replace(/^\.+/, "")
        const s = url.toString()
        if (suffix === "" || /\.[^\/.]+$/.test(s.substring(s.lastIndexOf("/") + 1))) return url
        return s + "." + suffix
    }
    // a save's file: asked about first when it is there
    function save(url, exists) {
        if (exists && replacing.toString() !== url.toString()) {
            replacing = url
            note = qsTrId("file_chooser.replace_question").arg(pathOf(url))
            return
        }
        choose([url])
    }
    // what was typed, in the location box or the name box
    function typed(text) {
        if (text.trim() === "") return false
        const r = resolve(text)
        if (r.isFolder) { go(r.url); if (!picksFolder) nameField.text = ""; return true }
        if (picksFolder) { note = qsTrId("file_chooser.no_such_folder").arg(pathOf(r.url)); return true }
        if (saves) {
            if (!r.parentExists) { note = qsTrId("file_chooser.no_such_folder").arg(pathOf(parentOf(r.url))); return true }
            const url = withSuffix(r.url)
            save(url, url.toString() === r.url.toString() ? r.exists : resolve(pathOf(url)).exists)
            return true
        }
        if (!r.exists) { note = qsTrId("file_chooser.no_such_file").arg(pathOf(r.url)); return true }
        choose([r.url])
        return true
    }
    function activate(index) {
        if (index < 0 || index >= entries.count) return
        if (entries.isFolder(index)) { go(entries.get(index, "fileUrl")); return }
        if (saves) { save(entries.get(index, "fileUrl"), true); return }
        if (mode === "openMany" && files.picked.length > 1) { chooseMany(); return }
        choose([entries.get(index, "fileUrl")])
    }
    function chooseMany() {
        const urls = files.picked.filter(i => !entries.isFolder(i)).map(i => entries.get(i, "fileUrl"))
        if (urls.length > 0) choose(urls)
    }
    function acceptPressed() {
        if (picksFolder) {
            // the folder shown, or another typed into the location box
            if (location.text !== pathOf(folder) && typed(location.text)) return
            choose([folder])
            return
        }
        if (location.text !== pathOf(folder) && typed(location.text)) return
        if (mode === "openMany" && files.picked.length > 1) { chooseMany(); return }
        if (typed(nameField.text)) return
        if (files.currentIndex >= 0) activate(files.currentIndex)
    }

    header: Label {
        text: chooser.title
        visible: text !== ""
        font.bold: true
        elide: Label.ElideRight
        horizontalAlignment: Label.AlignHCenter
        padding: 8
    }

    contentItem: ColumnLayout {
        spacing: 6
        Shortcut { sequences: ["Ctrl+L"]; enabled: chooser.opened; onActivated: { location.forceActiveFocus(); location.selectAll() } }
        Shortcut { sequences: ["Ctrl+H"]; enabled: chooser.opened; onActivated: hidden.checked = !hidden.checked }
        Shortcut { sequences: ["Alt+Up"]; enabled: chooser.opened; onActivated: chooser.go(chooser.parentOf(chooser.folder)) }

        RowLayout {
            Layout.fillWidth: true
            ToolButton {
                objectName: "chooserUp"
                icon.source: chooser.iconRoot + "up-icon-round.png"
                icon.width: 16
                icon.height: 16
                enabled: chooser.pathOf(chooser.parentOf(chooser.folder)) !== chooser.pathOf(chooser.folder)
                ToolTip.visible: hovered
                ToolTip.text: qsTrId("file_chooser.up")
                onClicked: chooser.go(chooser.parentOf(chooser.folder))
            }
            Label { text: qsTrId("file_chooser.location") }
            TextField {
                id: location
                objectName: "chooserLocation"
                Layout.fillWidth: true
                selectByMouse: true
                onAccepted: chooser.typed(text)
                Keys.onDownPressed: files.forceActiveFocus()
            }
            CheckBox {
                id: hidden
                objectName: "chooserHidden"
                text: qsTrId("file_chooser.show_hidden")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6

            Frame {
                Layout.preferredWidth: 180
                Layout.fillHeight: true
                padding: 1
                ListView {
                    id: places
                    anchors.fill: parent
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    model: ListModel { id: placeList }
                    section.property: "group"
                    section.delegate: Label {
                        required property string section
                        text: section === "drive" ? qsTrId("file_chooser.drives") : section === "bookmark" ? qsTrId("file_chooser.bookmarks") : qsTrId("file_chooser.places")
                        font.bold: true
                        leftPadding: 6
                        topPadding: 6
                        bottomPadding: 2
                    }
                    delegate: ItemDelegate {
                        required property string name
                        required property string url
                        required property string kind
                        objectName: "chooserPlace"
                        // on the window's colour, in its text: set on each row,
                        // as a row's label keeps the desktop's colour when it
                        // only inherits the theme (#389)
                        palette.text: chooser.palette.windowText
                        width: ListView.view.width
                        text: kind === "root" ? qsTrId("file_chooser.root") : kind === "home" ? qsTrId("file_chooser.home") : name
                        icon.source: chooser.iconRoot + "folder-icon-round.png"
                        icon.width: 16
                        icon.height: 16
                        highlighted: chooser.pathOf(url) === chooser.pathOf(chooser.folder)
                        ToolTip.visible: hovered && kind !== "root"
                        ToolTip.text: chooser.pathOf(url)
                        ToolTip.delay: 600
                        onClicked: chooser.go(url)
                    }
                }
            }

            Frame {
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: 1
                ListView {
                    id: files
                    objectName: "chooserFiles"
                    // the rows picked when several files are chosen at once
                    property var picked: []
                    anchors.fill: parent
                    clip: true
                    focus: true
                    boundsBehavior: Flickable.StopAtBounds
                    keyNavigationEnabled: true
                    ScrollBar.vertical: ScrollBar {}
                    model: FolderListModel {
                        id: entries
                        folder: chooser.folder
                        showDirsFirst: true
                        showDotAndDotDot: false
                        showHidden: hidden.checked
                        showFiles: !chooser.picksFolder
                        sortCaseSensitive: false
                        nameFilters: filter.currentIndex >= 0 && chooser.nameFilters.length > 0 ? chooser.globsOf(chooser.nameFilters[filter.currentIndex]) : ["*"]
                    }
                    onCurrentIndexChanged: {
                        if (currentIndex < 0 || entries.isFolder(currentIndex)) return
                        if (chooser.mode !== "openMany" || picked.length <= 1) picked = [currentIndex]
                        if (!chooser.picksFolder && picked.length <= 1) nameField.text = entries.get(currentIndex, "fileName")
                    }
                    Keys.onReturnPressed: chooser.activate(currentIndex)
                    Keys.onEnterPressed: chooser.activate(currentIndex)
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Backspace) { chooser.go(chooser.parentOf(chooser.folder)); event.accepted = true }
                    }
                    delegate: ItemDelegate {
                        required property int index
                        required property string fileName
                        required property bool fileIsDir
                        palette.text: chooser.palette.text
                        width: ListView.view.width
                        text: fileName
                        icon.source: chooser.iconRoot + (fileIsDir ? "folder" : "file") + "-icon-round.png"
                        icon.width: 16
                        icon.height: 16
                        highlighted: ListView.isCurrentItem || files.picked.indexOf(index) >= 0
                        MouseArea {
                            anchors.fill: parent
                            onClicked: mouse => {
                                files.forceActiveFocus()
                                if (chooser.mode === "openMany" && !fileIsDir && (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) {
                                    let picked = files.picked.slice()
                                    if (mouse.modifiers & Qt.ShiftModifier && files.currentIndex >= 0) {
                                        for (let i = Math.min(files.currentIndex, index); i <= Math.max(files.currentIndex, index); i++)
                                            if (!entries.isFolder(i) && picked.indexOf(i) < 0) picked.push(i)
                                    } else if (picked.indexOf(index) >= 0) picked.splice(picked.indexOf(index), 1)
                                    else picked.push(index)
                                    files.picked = picked
                                    nameField.text = picked.map(i => "\"" + entries.get(i, "fileName") + "\"").join(" ")
                                    return
                                }
                                files.picked = []
                                files.currentIndex = index
                            }
                            onDoubleClicked: chooser.activate(index)
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: !chooser.picksFolder
            Label { text: qsTrId("file_chooser.file_name") }
            TextField {
                id: nameField
                objectName: "chooserName"
                Layout.fillWidth: true
                selectByMouse: true
                onAccepted: chooser.acceptPressed()
                onTextEdited: { files.picked = []; chooser.replacing = "" }
            }
            ComboBox {
                id: filter
                Layout.preferredWidth: 220
                visible: chooser.nameFilters.length > 0
                model: chooser.nameFilters
            }
        }

        RowLayout {
            Layout.fillWidth: true
            CheckBox {
                objectName: "chooserCheckBox"
                visible: chooser.checkBoxText !== ""
                text: chooser.checkBoxText
                checked: chooser.checkBoxChecked
                onToggled: chooser.checkBoxChecked = checked
            }
            Label {
                Layout.fillWidth: true
                text: chooser.picksFolder && chooser.note === "" ? qsTrId("file_chooser.picks_folder_shown") : chooser.note
                wrapMode: Label.Wrap
                opacity: chooser.note === "" ? 0.7 : 1
            }
            Button {
                visible: chooser.replacing.toString() !== ""
                text: qsTrId("file_chooser.replace")
                onClicked: chooser.choose([chooser.replacing])
            }
            Button {
                objectName: "chooserAccept"
                highlighted: true
                text: chooser.picksFolder ? qsTrId("file_chooser.select_this_folder") : chooser.saves ? qsTrId("file_chooser.save") : qsTrId("file_chooser.open")
                onClicked: chooser.acceptPressed()
            }
            Button {
                text: qsTrId("common.cancel")
                onClicked: chooser.close()
            }
        }
    }
}
