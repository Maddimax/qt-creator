// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Templates as T
import QtCreator.Ui

T.Button {
    id: control

    // highlighted maps to the primary role, flat to ghost, the default to secondary.
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    horizontalPadding: Spacing.PaddingHXl
    verticalPadding: Spacing.PaddingVM
    spacing: Spacing.GapHS

    contentItem: Text {
        text: control.text
        font: Fonts.buttonMedium
        color: !control.enabled ? Tokens.textSubtle
             : control.highlighted ? Tokens.textOnAccent
             : Tokens.textDefault
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        implicitHeight: Fonts.buttonMediumLineHeight + 2 * Spacing.PaddingVM
        radius: Spacing.RadiusS
        color: {
            if (!control.enabled)
                return control.flat ? "transparent" : Tokens.foregroundSubtle
            if (control.highlighted)
                return control.down ? Tokens.accentSubtle
                     : control.hovered ? Tokens.accentMuted : Tokens.accentDefault
            if (control.down)
                return Tokens.foregroundSubtle
            if (control.flat)
                return control.hovered ? Tokens.foregroundSubtle : "transparent"
            return "transparent"
        }
        border.width: control.highlighted || control.flat ? 0 : (control.hovered ? 2 : 1)
        border.color: control.enabled ? Tokens.strokeStrong : Tokens.strokeSubtle
    }
}
