// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Templates as T
import QtCreator.Ui

T.SpinBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentItem.implicitWidth + 2 * Spacing.PaddingHS
                            + up.implicitIndicatorWidth + down.implicitIndicatorWidth)
    implicitHeight: Math.max(implicitContentHeight + topPadding + bottomPadding,
                             implicitBackgroundHeight,
                             up.implicitIndicatorHeight + down.implicitIndicatorHeight)

    leftPadding: Spacing.PaddingHS
    rightPadding: Spacing.PaddingHS + up.indicator.width

    contentItem: TextInput {
        text: control.displayText
        font: Fonts.body2
        color: control.enabled ? Tokens.textDefault : Tokens.textSubtle
        selectionColor: Tokens.accentDefault
        selectedTextColor: Tokens.textOnAccent
        horizontalAlignment: Qt.AlignLeft
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: control.inputMethodHints
    }

    up.indicator: Rectangle {
        x: control.width - width
        height: parent.height / 2
        implicitWidth: Spacing.PrimitiveXl
        implicitHeight: Spacing.PrimitiveL
        color: control.up.pressed ? Tokens.foregroundMuted
             : control.up.hovered ? Tokens.foregroundSubtle : "transparent"

        Rectangle {
            anchors.centerIn: parent
            width: Spacing.PrimitiveS
            height: 1
            color: control.enabled ? Tokens.textMuted : Tokens.textSubtle
        }
        Rectangle {
            anchors.centerIn: parent
            width: 1
            height: Spacing.PrimitiveS
            color: control.enabled ? Tokens.textMuted : Tokens.textSubtle
        }
    }

    down.indicator: Rectangle {
        x: control.width - width
        y: parent.height / 2
        height: parent.height / 2
        implicitWidth: Spacing.PrimitiveXl
        implicitHeight: Spacing.PrimitiveL
        color: control.down.pressed ? Tokens.foregroundMuted
             : control.down.hovered ? Tokens.foregroundSubtle : "transparent"

        Rectangle {
            anchors.centerIn: parent
            width: Spacing.PrimitiveS
            height: 1
            color: control.enabled ? Tokens.textMuted : Tokens.textSubtle
        }
    }

    background: Rectangle {
        implicitWidth: 100
        radius: Spacing.RadiusS
        color: !control.enabled ? Tokens.foregroundSubtle
             : control.hovered ? Tokens.foregroundSubtle : Tokens.backgroundMuted
        border.width: 1
        border.color: !control.enabled ? Tokens.foregroundSubtle
                    : control.activeFocus ? Tokens.strokeStrong
                    : control.hovered ? Tokens.strokeMuted : Tokens.strokeSubtle
    }
}
