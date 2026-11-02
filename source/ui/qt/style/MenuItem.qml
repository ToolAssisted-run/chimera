// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Fusion's menu item, its letter underlined only while the keyboard works
// the menus (Alt pressed), as the old window showed it; the letter picks the
// item either way (issue 405).
import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls.impl
import QtQuick.Controls.Fusion
import QtQuick.Controls.Fusion.impl

T.MenuItem {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: 6
    spacing: 6

    icon.width: 16
    icon.height: 16

    contentItem: MenuItemIconLabel {
        readonly property real arrowPadding: control.subMenu && control.arrow ? control.arrow.width + control.spacing : 0
        readonly property real indicatorPadding: control.checkable && control.indicator ? control.indicator.width + control.spacing : 0
        leftPadding: !control.mirrored ? indicatorPadding : arrowPadding
        rightPadding: control.mirrored ? indicatorPadding : arrowPadding

        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        alignment: Qt.AlignLeft

        icon: control.icon
        // the text whole, its letter underlined only while Alt took the keyboard to
        // the menus: Qt's label is told whether to underline. Given the text without
        // its letter instead, the label drew nothing once Alt had come and gone, as
        // Alt-Tab does (issue 471): its underline changed and the text, read the
        // same, was never laid out again
        text: control.text
        readonly property bool lettersShown: typeof menuLetters === "undefined" || menuLetters.shown
        function underline() {
            if (typeof menuLetters === "undefined") return
            for (const c of children)
                if (c.objectName === "label") menuLetters.underline(c, lettersShown)
        }
        onLettersShownChanged: underline()
        onChildrenChanged: underline()
        Component.onCompleted: underline()
        font: control.font
        color: control.down || control.highlighted ? Fusion.highlightedText(control.palette) : control.palette.text
        menuItem: control
    }

    arrow: ColorImage {
        x: control.mirrored ? control.padding : control.width - width - control.padding
        y: control.topPadding + (control.availableHeight - height) / 2
        width: 20

        visible: control.subMenu
        rotation: control.mirrored ? 90 : -90
        color: control.down || control.hovered || control.highlighted ? Fusion.highlightedText(control.palette) : control.palette.text
        source: "qrc:/qt-project.org/imports/QtQuick/Controls/Fusion/images/arrow.png"
        fillMode: Image.Pad
    }

    indicator: CheckIndicator {
        x: control.mirrored ? control.width - width - control.rightPadding : control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2

        control: control
        visible: control.checkable
    }

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 20

        color: Fusion.highlight(control.palette)
        visible: control.down || control.highlighted
    }
}
