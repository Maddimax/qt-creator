// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Templates as T
import QtCreator.Ui

T.CheckBox {
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
        radius: Spacing.RadiusS
        color: control.checkState !== Qt.Unchecked && control.enabled
               ? (control.hovered ? Tokens.accentSubtle : Tokens.accentDefault)
               : Tokens.foregroundSubtle
        border.width: 1
        border.color: control.hovered ? Tokens.strokeMuted : Tokens.strokeSubtle

        // A dash for the partial state, a tick for the checked one.
        Rectangle {
            anchors.centerIn: parent
            width: Spacing.PrimitiveM
            height: Spacing.PrimitiveXxs
            radius: 1
            color: Tokens.textOnAccent
            visible: control.checkState === Qt.PartiallyChecked
        }

        Item {
            anchors.centerIn: parent
            width: Spacing.PrimitiveL
            height: Spacing.PrimitiveL
            visible: control.checkState === Qt.Checked

            Rectangle {
                x: parent.width * 0.16
                y: parent.height * 0.52
                width: parent.width * 0.34
                height: Spacing.PrimitiveXxs
                radius: 1
                color: Tokens.textOnAccent
                transformOrigin: Item.Left
                rotation: 45
            }
            Rectangle {
                x: parent.width * 0.34
                y: parent.height * 0.68
                width: parent.width * 0.56
                height: Spacing.PrimitiveXxs
                radius: 1
                color: Tokens.textOnAccent
                transformOrigin: Item.Left
                rotation: -55
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
