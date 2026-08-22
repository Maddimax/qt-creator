// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Templates as T
import QtCreator.Ui

T.ItemDelegate {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    horizontalPadding: Spacing.PaddingHS
    verticalPadding: Spacing.PaddingVXs
    spacing: Spacing.GapHS

    contentItem: Text {
        text: control.text
        font: Fonts.body2
        color: !control.enabled ? Tokens.textSubtle
             : control.highlighted ? Tokens.textOnAccent : Tokens.textDefault
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        implicitHeight: Fonts.body2LineHeight + 2 * Spacing.PaddingVXs
        color: control.down || control.highlighted ? Tokens.accentDefault
             : control.hovered ? Tokens.foregroundSubtle : "transparent"
    }
}
