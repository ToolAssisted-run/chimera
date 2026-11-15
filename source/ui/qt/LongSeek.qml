// A long seek (user-decided, 2026-11-15, issue 595): a seek the person asks
// for whose replay would take long is asked about first, then shows its
// progress - the frames remaining, the speed, the time left - with a Cancel
// that puts everything back as it was before it began (Escape cancels too).
// The session view model decides, estimates and puts back; this is layout
// and bindings only. Placed in TAStudio and in the main window: `shown` says
// which of the two shows it.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: longSeek
    required property QtObject session
    property bool shown: true
    // the dialogs follow the session: open while it asks or while a long seek runs
    function follow() {
        if (longSeek.shown && longSeek.session.longSeekQuestion !== "") question.open(); else question.close()
        if (longSeek.shown && longSeek.session.longSeekTo >= 0) progress.open(); else progress.close()
    }
    onShownChanged: longSeek.follow()
    Component.onCompleted: longSeek.follow()
    Connections {
        target: longSeek.session
        function onLongSeekQuestionChanged() { longSeek.follow() }
        function onLongSeekToChanged() { longSeek.follow() }
    }
    Dialog {
        id: question
        objectName: "longSeekQuestion"
        title: qsTrId("tastudio.long_seek_question_title")
        anchors.centerIn: parent
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            objectName: "longSeekQuestionText"
            width: Math.min(480, Math.max(240, longSeek.width - 64))
            wrapMode: Text.Wrap
            text: longSeek.session.longSeekQuestion
        }
        onAccepted: longSeek.session.run("answerLongSeek", [true])
        onRejected: longSeek.session.run("answerLongSeek", [false])
    }
    Dialog {
        id: progress
        objectName: "longSeekProgress"
        title: qsTrId("tastudio.long_seek_title").arg(longSeek.session.longSeekTo)
        anchors.centerIn: parent
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        onOpened: cancelButton.forceActiveFocus()
        ColumnLayout {
            spacing: 8
            ProgressBar {
                objectName: "longSeekBar"
                Layout.fillWidth: true
                Layout.preferredWidth: 360
                from: 0; to: 1
                value: longSeek.session.longSeekProgress
            }
            GridLayout {
                columns: 2
                columnSpacing: 12
                Label { text: qsTrId("tastudio.frames_remaining") }
                Label { objectName: "longSeekRemaining"; text: longSeek.session.longSeekRemainingText }
                Label { text: qsTrId("tastudio.speed") }
                Label { objectName: "longSeekSpeed"; text: longSeek.session.longSeekSpeedText }
                Label { text: qsTrId("tastudio.time_remaining") }
                Label { objectName: "longSeekTime"; text: longSeek.session.longSeekTimeText }
            }
        }
        footer: DialogButtonBox {
            Button {
                id: cancelButton
                objectName: "longSeekCancel"
                text: qsTrId("common.cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        // Cancel and Escape: back to where it started
        onRejected: longSeek.session.run("cancelLongSeek")
    }
}
