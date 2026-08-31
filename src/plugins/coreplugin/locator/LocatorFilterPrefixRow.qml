// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// What every locator filter's configuration dialog ends with: the prefix that
// restricts a search to this filter, and whether it is asked at all without
// one. A filter that adds settings of its own draws them above this.
RowLayout {
    id: root

    required property var aspects

    spacing: Spacing.GapHM
    Layout.fillWidth: true

    StringDelegate {
        aspect: root.aspects.Shortcut
        Layout.fillWidth: true
    }

    BoolWithOwnLabelDelegate { aspect: root.aspects.IncludedByDefault }
}
