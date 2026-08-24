// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Templates as T
import QtCreator.Ui

T.TextField {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    leftPadding: Spacing.PaddingHS
    rightPadding: Spacing.PaddingHS
    topPadding: Spacing.PaddingVXs
    bottomPadding: Spacing.PaddingVXs

    font: Fonts.body2
    color: enabled ? Tokens.textDefault : Tokens.textSubtle
    placeholderTextColor: Tokens.textMuted
    selectionColor: Tokens.accentDefault
    selectedTextColor: Tokens.textOnAccent
    verticalAlignment: T.TextField.AlignVCenter

    // A T.TextField draws its own content but no placeholder; a style has to
    // supply one, and this one had none - so no placeholder text showed
    // anywhere in the UI.
    Text {
        x: control.leftPadding
        y: control.topPadding
        width: control.width - (control.leftPadding + control.rightPadding)
        height: control.height - (control.topPadding + control.bottomPadding)
        text: control.placeholderText
        font: control.font
        color: control.placeholderTextColor
        verticalAlignment: control.verticalAlignment
        visible: control.length === 0 && control.preeditText.length === 0
        elide: Text.ElideRight
        renderType: control.renderType
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
}
