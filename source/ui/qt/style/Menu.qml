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
//   desktop's menus are (issue 449).
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

    delegate: MenuItem { }

    contentItem: ListView {
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

        ScrollIndicator.vertical: ScrollIndicator {}
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
