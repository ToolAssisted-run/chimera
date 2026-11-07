// RAM Poke, as Chimera's: a value written into the addresses picked, read
// as they are shown; a game property's watch takes the text the engine
// reads. RAM Watch and RAM Search both open it. Layout and bindings only,
// over the view model given (its pokeInfo and pokeSelected).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: poke
    objectName: "ramPokeWindow"
    // a view model with pokeInfo and pokeSelected; how to tell of a game property refused
    property QtObject vm: null
    property var tell: null
    property var info: ({})
    title: qsTrId("ram_poke.ram_poke").arg(poke.info.domain || "")
    width: 300
    height: 250
    minimumWidth: 300
    minimumHeight: 250
    modality: Qt.platform.os === "osx" ? Qt.WindowModal : Qt.ApplicationModal // a sheet on a Mac, which keeps an application-modal window over other apps (#541)
    property string said: qsTrId("ram_poke.enter_an_address_to_poke")
    function start(info) {
        poke.info = info
        poke.said = qsTrId("ram_poke.enter_an_address_to_poke")
        valueBox.text = info.value
        poke.show()
        poke.raise()
        valueBox.forceActiveFocus()
        valueBox.selectAll()
    }
    function accept() {
        const ok = poke.vm.run("pokeSelected", [valueBox.text])
        if (ok === true) { poke.close(); return }
        if (poke.info.property) { if (poke.tell) poke.tell(qsTrId("ram_poke.not_set"), poke.vm.error); return }
        poke.said = qsTrId("ram_poke.an_error_occurred_when_writing_value")
    }
    GridLayout {
        anchors.fill: parent
        anchors.margins: 10
        columns: 2
        Label { Layout.columnSpan: 2; text: poke.said }
        Label { text: poke.info.property ? qsTrId("ram_poke.property") : qsTrId("ram_poke.address_0x"); Layout.alignment: Qt.AlignRight }
        Label { text: (poke.info.property ? poke.info.name : poke.info.addresses) || ""; font.family: "monospace"; elide: Text.ElideRight; Layout.fillWidth: true }
        Label { text: qsTrId("ram_poke.domain"); Layout.alignment: Qt.AlignRight }
        Label { text: poke.info.domain || "" }
        Label { text: qsTrId("ram_poke.size"); Layout.alignment: Qt.AlignRight; visible: !poke.info.property }
        Label { text: poke.info.size || ""; visible: !poke.info.property }
        Label { text: qsTrId("ram_poke.display_type"); Layout.alignment: Qt.AlignRight; visible: !poke.info.property }
        Label { text: poke.info.display || ""; visible: !poke.info.property }
        Label { text: qsTrId("ram_poke.endian"); Layout.alignment: Qt.AlignRight; visible: !poke.info.property }
        Label { text: poke.info.endian || ""; visible: !poke.info.property }
        Label { text: qsTrId("ram_poke.value"); Layout.alignment: Qt.AlignRight }
        RowLayout {
            Label { text: "0x"; visible: poke.info.hex === true }
            TextField { id: valueBox; Layout.fillWidth: true; font.family: "monospace"; onAccepted: poke.accept() }
        }
        RowLayout {
            Layout.columnSpan: 2
            Layout.alignment: Qt.AlignRight
            Button { text: qsTrId("ram_poke.poke"); highlighted: true; onClicked: poke.accept() }
            Button { text: qsTrId("common.close_mnemonic"); onClicked: poke.close() }
        }
    }
}
