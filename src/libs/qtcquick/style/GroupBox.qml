// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Templates as T
import QtCreator.Ui

T.GroupBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding,
                            implicitLabelWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: Spacing.PaddingHM
    topPadding: padding + (implicitLabelWidth > 0 ? implicitLabelHeight + Spacing.GapVS : 0)
    spacing: Spacing.GapVS

    label: Text {
        x: control.leftPadding
        width: control.availableWidth
        text: control.title
        font: Fonts.h6
        color: control.enabled ? Tokens.textDefault : Tokens.textSubtle
        elide: Text.ElideRight
    }

    background: Rectangle {
        y: control.topPadding - control.padding
        width: parent.width
        height: parent.height - control.topPadding + control.padding
        radius: Spacing.RadiusS
        color: "transparent"
        border.width: 1
        border.color: Tokens.strokeSubtle
    }
}
