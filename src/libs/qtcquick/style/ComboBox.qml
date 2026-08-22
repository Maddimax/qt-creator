// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Templates as T
import QtCreator.Ui

T.ComboBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    leftPadding: Spacing.PaddingHS
    rightPadding: Spacing.PaddingHS + indicator.width + Spacing.GapHXs
    topPadding: Spacing.PaddingVXs
    bottomPadding: Spacing.PaddingVXs
    spacing: Spacing.GapHXs

    delegate: ItemDelegate {
        required property var model
        required property int index

        width: ListView.view.width
        text: model[control.textRole]
        highlighted: control.highlightedIndex === index
    }

    indicator: Item {
        x: control.mirrored ? control.padding : control.width - width - control.padding
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: Spacing.PrimitiveM
        implicitHeight: Spacing.PrimitiveM

        Canvas {
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.moveTo(0, height / 3)
                ctx.lineTo(width, height / 3)
                ctx.lineTo(width / 2, height * 2 / 3)
                ctx.closePath()
                ctx.fillStyle = control.enabled ? Tokens.textMuted : Tokens.textSubtle
                ctx.fill()
            }
        }
    }

    contentItem: Text {
        text: control.displayText
        font: Fonts.body2
        color: control.enabled ? Tokens.textDefault : Tokens.textSubtle
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        implicitWidth: 120
        implicitHeight: Fonts.body2LineHeight + 2 * Spacing.PaddingVXs
        radius: Spacing.RadiusS
        color: !control.enabled ? Tokens.foregroundSubtle
             : control.hovered ? Tokens.foregroundSubtle : Tokens.backgroundMuted
        border.width: 1
        border.color: !control.enabled ? Tokens.foregroundSubtle
                    : control.activeFocus ? Tokens.strokeStrong
                    : control.hovered ? Tokens.strokeMuted : Tokens.strokeSubtle
    }

    popup: T.Popup {
        y: control.height
        width: control.width
        height: Math.min(contentItem.implicitHeight, 320)
        padding: 1

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
        }

        background: Rectangle {
            radius: Spacing.RadiusS
            color: Tokens.backgroundDefault
            border.width: 1
            border.color: Tokens.strokeSubtle
        }
    }
}
