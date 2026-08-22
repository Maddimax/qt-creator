// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Templates as T
import QtCreator.Ui

T.Switch {
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
        implicitWidth: Spacing.PrimitiveXxl
        implicitHeight: Spacing.PrimitiveL
        radius: height / 2
        color: !control.enabled ? Tokens.foregroundSubtle
             : control.down ? Tokens.foregroundDefault
             : control.hovered ? Tokens.foregroundMuted : Tokens.foregroundSubtle

        Rectangle {
            x: control.checked ? parent.width - width - 1 : 1
            y: (parent.height - height) / 2
            width: parent.height - 2
            height: width
            radius: width / 2
            color: control.checked && control.enabled ? Tokens.accentDefault : Tokens.textMuted

            Behavior on x {
                NumberAnimation { duration: 80; easing.type: Easing.OutCubic }
            }
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
