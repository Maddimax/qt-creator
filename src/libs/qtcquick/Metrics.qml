// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma Singleton
pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick

// Sizes that are not spacing, so they have no SpacingTokens equivalent, but
// which several components have to agree on.
QtObject {
    // Width of the label column in a settings form.
    readonly property int formLabelWidth: 200
    // Preferred width of a bounded editor in a settings form.
    readonly property int formControlWidth: 240
    // Side length of the color preview swatch in ColorDelegate.
    readonly property int colorSwatchSize: 24
}
