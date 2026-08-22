// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// Mirrors Utils::QtcPageIndicator: a row of dots, the current one accented.
// Utils::QtcPageIndicator only fades the dot that was just vacated, driven
// by a QVariantAnimation that interpolates its colour from active to
// inactive. A plain "Behavior on color" fades every dot uniformly instead,
// which also lets the newly active dot fade in rather than snap - visually
// close to the original for a single-step change, and simpler than
// reproducing the one-sided interpolation exactly.
//
// The widget takes no pointer input (it is set programmatically), so this
// stays a non-interactive indicator rather than a button strip.
Item {
    id: root

    property int pagesCount: 5
    property int currentPage: 0

    implicitWidth: dotsRow.implicitWidth
    implicitHeight: dotsRow.implicitHeight

    Accessible.role: Accessible.Indicator
    Accessible.name: qsTr("Page %1 of %2").arg(root.currentPage + 1).arg(root.pagesCount)

    Row {
        id: dotsRow
        spacing: Spacing.PrimitiveS

        Repeater {
            model: root.pagesCount

            delegate: Rectangle {
                id: dot
                required property int index

                width: Spacing.PrimitiveM
                height: Spacing.PrimitiveM
                radius: width / 2
                color: dot.index === root.currentPage
                       ? (root.enabled ? Tokens.accentDefault : Tokens.foregroundDefault)
                       : (root.enabled ? Tokens.foregroundDefault : Tokens.foregroundSubtle)

                Behavior on color {
                    ColorAnimation { duration: 400; easing.type: Easing.OutQuad }
                }
            }
        }
    }
}
