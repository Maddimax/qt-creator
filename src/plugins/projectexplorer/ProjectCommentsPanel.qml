// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// Documentation comments for one project. Whether any of it can be changed is
// the container's answer: the settings below are disabled as a whole while the
// global ones are in use.
AspectPage {
    id: root

    // The settings the flag turns on are a container of their own, so they are
    // reached by name through it rather than through the panel.
    readonly property var settings: AspectModels.named(root.aspects.Settings)

    BoolWithOwnLabelDelegate { aspect: root.aspects.UseGlobalSettings }

    BoolDelegate { aspect: root.settings.EnableDoxygenBlocks }

    RowLayout {
        Item { Layout.preferredWidth: Spacing.GapHL }
        BoolDelegate { aspect: root.settings.GenerateBrief }
    }

    BoolDelegate { aspect: root.settings.AddLeadingAsterisks }
    SelectionDelegate { aspect: root.settings.CommandPrefix }
}
