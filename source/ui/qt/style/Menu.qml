// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Fusion's menu, with three changes:
// - as wide as its widest row: Fusion's list says no width of its own, so a
//   menu was always 200 pixels and a longer row was cut short ("Reproducible
//   Media Make...", issue 452);
// - below the window's menu bar: in a short window a menu was moved up over
//   the bar, hiding the menus there; it stays under the bar now, and its
//   rows scroll (issues 450, 463). A menu cannot go past its window's edges
//   (the owner kept menus inside the window);
// - closed when its window loses the keyboard to another one, as a
//   desktop's menus are (issue 449);
// - when its rows do not all fit, a strip at the top and the bottom with an
//   arrow, bright while there are rows past that end, that scrolls the rows
//   while the pointer rests on it, as a desktop's long menus do. Nothing
//   showed that rows were hidden before: the scroll indicator appears only
//   while the rows move (issue 588).
import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls.impl
import QtQuick.Controls.Fusion
import QtQuick.Controls.Fusion.impl
import QtQuick.Window

T.Menu {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    margins: 0
    topMargin: control.T.ApplicationWindow.menuBar ? control.T.ApplicationWindow.menuBar.height : 0
    padding: 1
    overlap: 2

    /* how tall each scroll strip is (issue 588) */
    readonly property int scrollStripHeight: 14

    delegate: MenuItem { }

    /* the strips' room is made once the rows are found not to fit, after
     * they were placed: an opened menu starts from its first row, below
     * the top strip, not under it */
    onOpened: list.contentY = list.originY - list.topMargin

    /* a scroll strip: at the top or the bottom of rows that do not all fit */
    component ScrollStrip: Rectangle {
        id: strip
        required property ListView view
        required property bool up
        /* rows past this end, still to scroll to */
        readonly property bool more: up ? !view.atYBeginning : !view.atYEnd
        visible: view.interactive
        enabled: more
        y: up ? 0 : view.height - height
        z: 2
        width: view.width
        height: control.scrollStripHeight
        color: control.palette.base

        ColorImage {
            anchors.centerIn: parent
            rotation: strip.up ? 180 : 0
            color: strip.enabled ? control.palette.text : control.palette.mid
            source: "qrc:/qt-project.org/imports/QtQuick/Controls/Fusion/images/arrow.png"
        }
        HoverHandler { id: hover }
        /* the rows move 4 pixels every 25 ms (160 a second) while the
         * pointer rests here, and stop at the end */
        Timer {
            interval: 25
            repeat: true
            running: hover.hovered && strip.more
            onTriggered: strip.view.contentY = strip.up
                ? Math.max(strip.view.originY - strip.view.topMargin, strip.view.contentY - 4)
                : Math.min(strip.view.originY + strip.view.contentHeight + strip.view.bottomMargin - strip.view.height, strip.view.contentY + 4)
        }
    }

    contentItem: ListView {
        id: list
        implicitHeight: contentHeight
        implicitWidth: {
            let widest = 0
            for (let i = 0; i < control.count; i++) {
                const row = control.itemAt(i)
                if (row) widest = Math.max(widest, row.implicitWidth)
            }
            return widest
        }
        model: control.contentModel
        interactive: Window.window
                     ? contentHeight + control.topPadding + control.bottomPadding > control.height
                     : false
        clip: true
        currentIndex: control.currentIndex
        /* room for the scroll strips, so they cover no row */
        topMargin: interactive ? control.scrollStripHeight : 0
        bottomMargin: interactive ? control.scrollStripHeight : 0
        onTopMarginChanged: contentY = originY - topMargin

        ScrollIndicator.vertical: ScrollIndicator {}

        ScrollStrip { objectName: "menuScrollUp"; parent: list; view: list; up: true }
        ScrollStrip { objectName: "menuScrollDown"; parent: list; view: list; up: false }
    }

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 20

        color: control.palette.base
        border.color: Fusion.outline(control.palette)

        Rectangle {
            z: -1
            x: 1; y: 1
            width: parent.width
            height: parent.height
            color: control.palette.shadow
            opacity: 0.2
        }
    }

    Connections {
        target: control.T.ApplicationWindow.window
        function onActiveChanged() {
            if (!control.T.ApplicationWindow.window.active) control.close()
        }
    }

    T.Overlay.modal: Rectangle {
        color: Fusion.topShadow
    }

    T.Overlay.modeless: Rectangle {
        color: Fusion.topShadow
    }
}
