// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Fusion's menu, with three changes:
// - as wide as its widest row: Fusion's list says no width of its own, so a
//   menu was always 200 pixels and a longer row was cut short ("Reproducible
//   Media Make...", issue 452);
// - its own window, so it may reach past the edges of the window it opens
//   from, as the old WinForms menus did (user-decided, 2026-11-15, issue
//   593). It stays below the window's menu bar: in a short window a menu was
//   moved up over the bar, hiding the menus there (issues 450, 463). Where
//   the system cannot show a menu as a window, Qt draws it inside the window
//   as before, and the rows scroll;
// - closed when its window loses the keyboard to another one, as a
//   desktop's menus are (issue 449), and, as a window, closed by a click on
//   its name in the menu bar (see onOpened);
// - when its rows do not all fit, which as a window means taller than the
//   screen, a strip at the top and the bottom with an arrow, bright while
//   there are rows past that end, that scrolls the rows while the pointer
//   rests on it, as a desktop's long menus do. Nothing showed that rows were
//   hidden before: the scroll indicator appears only while the rows move
//   (issue 588).
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
    implicitHeight: Math.min(Math.max(implicitBackgroundHeight + topInset + bottomInset,
                                      implicitContentHeight + topPadding + bottomPadding),
                             control.screenRoom)

    /* as a window it is no taller than its screen (Qt does not hold a popup
     * window to the screen); its rows then scroll (issues 588, 593) */
    readonly property real screenRoom: {
        const w = control.T.ApplicationWindow.window
        if (!w || !w.screen) return 1e6
        return Math.max(100, Math.min(w.screen.height, w.screen.desktopAvailableHeight) - control.topMargin)
    }

    /* its own window, which may reach past its parent window (issue 593) */
    popupType: T.Popup.Window
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
    onOpened: {
        list.contentY = list.originY - list.topMargin
        /* the menu's own window, while it is one: once closed, its rows are
         * in no window, and the keyboard's return below needs it */
        control.ownWindow = control.contentItem ? control.contentItem.Window.window : null
        /* A menu bar's menu has Qt's rule "close on a press outside the bar
         * item", so that a press on the item does not close a menu the item's
         * click would then open again. As a window of its own, the menu gets
         * every press while it is open, and Qt keeps a press on the bar from
         * the bar so that it opens nothing; under Qt's rule the menu then
         * stayed open when its name was clicked again. As a window, a press
         * outside it, its name included, closes it, as a desktop's menus do.
         * Drawn inside the window, it keeps Qt's rule, and so does a submenu,
         * whose rule is another (its parent row, and closing those above). */
        if (control.ownWindow && control.ownWindow !== control.T.ApplicationWindow.window
                && control.closePolicy === (T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent
                                            | T.Popup.CloseOnReleaseOutsideParent))
            control.closePolicy = T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside
    }

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

    /* closed when the keyboard has gone to another window (issue 449). As a
     * window of its own (issue 593) the menu may hold the keyboard itself,
     * and Qt then tells only the window losing it, so both are watched: the
     * menu closes once neither the window it opened from nor its own has the
     * keyboard. A window counts as having it while a menu of its own does. */
    function keyboardGone() {
        Qt.callLater(() => {
            const from = control.T.ApplicationWindow.window
            const own = control.contentItem ? control.contentItem.Window.window : null
            if (control.opened && from && !from.active && !(own && own.active)) {
                control.leftForAnotherWindow = true
                control.close()
            }
        })
    }
    Connections {
        target: control.T.ApplicationWindow.window
        function onActiveChanged() { control.keyboardGone() }
    }
    Connections {
        target: control.contentItem ? control.contentItem.Window.window : null
        ignoreUnknownSignals: true
        function onActiveChanged() { control.keyboardGone() }
    }
    /* Where the system gives the menu's own window the keyboard, it may
     * leave it there once the menu is hidden. The window the menu opened
     * from then takes it back, unless the menu closed because another window
     * took it (above). A tool the menu opened has it by then, and keeps it.
     * Windows and X11 leave the keyboard with the window the menu opened from
     * all along, and nothing is done there. */
    property bool leftForAnotherWindow: false
    property Window ownWindow: null
    onAboutToShow: control.leftForAnotherWindow = false
    onClosed: Qt.callLater(() => {
        const from = control.T.ApplicationWindow.window
        const own = control.ownWindow
        if (!control.leftForAnotherWindow && from && own && own !== from && own.active)
            from.requestActivate()
    })

    T.Overlay.modal: Rectangle {
        color: Fusion.topShadow
    }

    T.Overlay.modeless: Rectangle {
        color: Fusion.topShadow
    }
}
