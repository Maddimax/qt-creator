// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// Mirrors Utils::QtcLabel: a muted section heading in one of two sizes.
Text {
    id: root

    enum Role {
        Primary,
        Secondary
    }

    property int role: QtcLabel.Role.Primary

    readonly property int vPadding: root.role === QtcLabel.Role.Primary ? Spacing.PaddingVS
                                                                        : Spacing.PaddingVM
    readonly property font labelFont: root.role === QtcLabel.Role.Primary ? Fonts.h3
                                                                          : Fonts.h6Capital
    readonly property int labelLineHeight: root.role === QtcLabel.Role.Primary ? Fonts.h3LineHeight
                                                                               : Fonts.h6CapitalLineHeight

    font: root.labelFont
    color: root.enabled ? Tokens.textMuted : Tokens.textSubtle
    verticalAlignment: Text.AlignVCenter
    elide: Text.ElideRight
    implicitHeight: root.vPadding * 2 + root.labelLineHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: root.text
}
