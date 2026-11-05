// Chimera's Greenzone budgets (from the Cache Manager), laid out as its
// Greenzone budgets: what a history of states may weigh for every
// project (memory, the stride behind the playhead, the kept pictures after
// a load) and, when the Cache Manager's row picked is a project's cache, a
// budget of that project's own, kept beside its cache; OK keeps them,
// Cancel lets them go. Layout and bindings only, over the session (every
// project's) and the cacheManager view model (a project's own).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    objectName: "greenzoneBudgetsWindow"
    required property QtObject session
    required property QtObject caches
    property Window mainWindow: null
    // the theme's colour for a role (Main hands it its lookup)
    property var themeColor: (role, fallback) => fallback
    // the project the Cache Manager's row picked is ("" for none), and its name
    property string projectId: ""
    property string projectLabelAsked: ""
    // Chimera shortens a long name to 40 characters
    readonly property string projectLabel: win.projectLabelAsked.length <= 40 ? win.projectLabelAsked : win.projectLabelAsked.substring(0, 37) + "..."
    readonly property bool hasProject: win.projectId !== ""
    title: win.hasProject && win.projectLabel !== "" ? qsTrId("greenzone_budgets.greenzone_budgets").arg(win.projectLabel) : qsTrId("greenzone_budgets.greenzone_budgets_2")
    width: 470
    height: win.hasProject ? 470 : 300
    minimumWidth: 470
    minimumHeight: win.hasProject ? 470 : 300
    modality: Qt.ApplicationModal
    transientParent: win.mainWindow

    // the boxes, read when the window opens
    onVisibleChanged: if (visible) {
        memory.value = win.session.greenzoneBudgetMb
        stride.value = win.session.greenzoneNearStride
        settle.value = win.session.pictureSettleFrames
        const known = win.hasProject ? JSON.parse(win.caches.run("budgetsOf", [win.projectId]) || "{}") : {}
        own.checked = (known.memoryMb || 0) !== 0 || (known.maxNearStride || 0) !== 0
        ownMemory.value = known.memoryMb ? known.memoryMb : win.session.greenzoneBudgetMb
        ownStride.value = known.maxNearStride ? known.maxNearStride : win.session.greenzoneNearStride
    }
    function keep() {
        win.session.greenzoneBudgetMb = memory.value
        win.session.greenzoneNearStride = stride.value
        win.session.pictureSettleFrames = settle.value
        if (!win.hasProject) return
        const asked = own.checked ? [ownMemory.value, ownStride.value] : [0, 0]
        // the project open's through the session, which shows them; another's beside its cache
        if (win.projectId === win.session.projectId) win.session.run("setProjectBudgets", asked)
        else win.caches.run("setBudgetsOf", [win.projectId].concat(asked))
    }

    component Note: Label { wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.columnSpan: 3 }
    component Name: Label { Layout.leftMargin: 12; Layout.preferredWidth: 218 }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        Note { text: qsTrId("greenzone_budgets.a_greenzone_is_kept_in_memory") }
        Label { text: qsTrId("greenzone_budgets.every_project_unless_it_says_otherwise"); font.bold: true; Layout.topMargin: 4 }
        GridLayout {
            columns: 3
            Name { text: qsTrId("greenzone_budgets.keep_in_memory") }
            SpinBox { id: memory; from: 64; to: 1048576; stepSize: 256; editable: true; Layout.preferredWidth: 110 }
            Label { text: qsTrId("greenzone_budgets.mb") }
            Name { text: qsTrId("greenzone_budgets.behind_the_playhead_1_frame_in") }
            SpinBox { id: stride; from: 1; to: 32; editable: true; Layout.preferredWidth: 110 }
            Item { width: 1 }
            Note { text: qsTrId("greenzone_budgets.1_keeps_every_frame_and_plays") }
            Name { text: qsTrId("greenzone_budgets.after_a_load_show_kept_pictures") }
            SpinBox { id: settle; from: 0; to: 10; editable: true; Layout.preferredWidth: 110 }
            Label { text: qsTrId("greenzone_budgets.frames") }
            Note { text: qsTrId("greenzone_budgets.on_a_gpu_core_the_picture") }
        }
        Label { text: qsTrId("greenzone_budgets.this_project"); font.bold: true; visible: win.hasProject; Layout.topMargin: 10 }
        CheckBox {
            id: own
            visible: win.hasProject
            text: qsTrId("greenzone_budgets.a_different_budget_for").arg(win.projectLabel)
            padding: 2
        }
        GridLayout {
            columns: 3
            visible: win.hasProject
            Name { text: qsTrId("greenzone_budgets.keep_in_memory") }
            SpinBox { id: ownMemory; enabled: own.checked; from: 64; to: 1048576; stepSize: 256; editable: true; Layout.preferredWidth: 110 }
            Label { text: qsTrId("greenzone_budgets.mb") }
            Name { text: qsTrId("greenzone_budgets.behind_the_playhead_1_frame_in") }
            SpinBox { id: ownStride; enabled: own.checked; from: 1; to: 32; editable: true; Layout.preferredWidth: 110 }
            Item { width: 1 }
            Note { text: qsTrId("greenzone_budgets.takes_effect_when_the_project_is") }
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            Label { text: win.session.error; color: win.themeColor("AccentError", "#b00020"); Layout.fillWidth: true; elide: Text.ElideRight }
            Button { text: qsTrId("common.ok"); Layout.preferredWidth: 90; onClicked: { win.keep(); win.close() } }
            Button { text: qsTrId("common.cancel"); Layout.preferredWidth: 90; onClicked: win.close() }
        }
    }
}
