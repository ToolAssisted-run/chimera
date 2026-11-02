// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Fusion's menu bar item, its letter underlined only while the keyboard
// works the menus (Alt pressed), as the old window showed it; the letter
// opens the menu either way (issue 405).
import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls.impl
import QtQuick.Controls.Fusion
import QtQuick.Controls.Fusion.impl

T.MenuBarItem {
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

    contentItem: IconLabel {
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
        defaultIconColor: control.down || control.highlighted ? Fusion.highlightedText(control.palette) : control.palette.text
        color: defaultIconColor
    }

    background: Rectangle {
        implicitWidth: 20
        implicitHeight: 20

        color: Fusion.highlight(control.palette)
        visible: control.down || control.highlighted
    }
}
