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

    // Text works out its own implicit size and will not be told one - assigning
    // implicitHeight here is an error that stops the whole type loading. The
    // design system's line height and padding are the same thing said in the
    // properties Text does own, so implicitHeight comes out where it should.
    lineHeight: root.labelLineHeight
    lineHeightMode: Text.FixedHeight
    topPadding: root.vPadding
    bottomPadding: root.vPadding

    Accessible.role: Accessible.StaticText
    Accessible.name: root.text
}
