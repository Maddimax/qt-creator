// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    id: root

    BoolDelegate { aspect: root.aspects.EnableDoxygenBlocks }

    RowLayout {
        Item { Layout.preferredWidth: Spacing.GapHL }
        BoolDelegate { aspect: root.aspects.GenerateBrief }
    }

    BoolDelegate { aspect: root.aspects.AddLeadingAsterisks }
    SelectionDelegate { aspect: root.aspects.CommandPrefix }
}
