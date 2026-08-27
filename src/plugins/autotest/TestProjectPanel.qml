// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// Testing for one project. What the global settings decide is disabled while
// they are in use; the path patterns are the project's either way, which is why
// they sit outside that. The container says which is which.
AspectPage {
    id: root

    contentFillsHeight: true

    readonly property var settings: AspectModels.named(root.aspects.Settings)

    BoolWithOwnLabelDelegate { aspect: root.aspects.UseGlobalSettings }

    AspectGroupBox {
        title: qsTr("Active Test Frameworks")

        TableDelegate { aspect: root.aspects.ActiveTestBases }
    }

    SelectionDelegate { aspect: root.settings.RunAfterBuild }

    AspectGroupBox {
        title: qsTr("Limit Files to Path Patterns")
        checkAspect: root.settings.LimitToFilter
        Layout.fillHeight: true

        StringListEditorDelegate { aspect: root.settings.PathFilters }
    }
}
