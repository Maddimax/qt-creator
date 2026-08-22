// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Templates as T
import QtCreator.Ui

T.RadioButton {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: Spacing.PaddingVXs
    spacing: Spacing.GapHS

    indicator: Rectangle {
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding
                                           : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: Spacing.PrimitiveXl
        implicitHeight: Spacing.PrimitiveXl
        radius: width / 2
        color: control.checked && control.enabled
               ? (control.hovered ? Tokens.accentSubtle : Tokens.accentDefault)
               : Tokens.foregroundSubtle
        border.width: 1
        border.color: control.hovered ? Tokens.strokeMuted : Tokens.strokeSubtle

        Rectangle {
            x: (parent.width - width) / 2
            y: (parent.height - height) / 2
            width: Spacing.PrimitiveS
            height: width
            radius: width / 2
            color: Tokens.textOnAccent
            visible: control.checked
        }
    }

    contentItem: Text {
        leftPadding: control.indicator && !control.mirrored
                     ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored
                      ? control.indicator.width + control.spacing : 0
        text: control.text
        font: Fonts.labelMedium
        color: control.enabled ? Tokens.textDefault : Tokens.textSubtle
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
