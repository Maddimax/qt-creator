// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// CMake's settings for one project. Whether any of them can be changed is the
// settings container's answer: it is disabled as a whole while the global ones
// are in use, which is why the flag is not one of them.
AspectPage {
    id: root

    // The settings the flag turns on are a container of their own, so they are
    // reached by name through it rather than through the panel.
    readonly property var settings: AspectModels.named(root.aspects.Settings)

    BoolWithOwnLabelDelegate { aspect: root.aspects.UseGlobalSettings }

    // The same settings the General page shows, in the same order: a project
    // overrides the global ones rather than having a different set.
    BoolDelegate { aspect: root.settings.AutorunCMake }
    BoolDelegate { aspect: root.settings.CleanOldOutput }
    BoolDelegate { aspect: root.settings.PackageManagerAutoSetup }
    BoolDelegate { aspect: root.settings.MaintenanceToolDependencyProvider }
    BoolDelegate { aspect: root.settings.AskReConfigureInitialParams }
    BoolDelegate { aspect: root.settings.AskBeforePresetsReload }
    BoolDelegate { aspect: root.settings.ShowSourceSubFolders }
    BoolDelegate { aspect: root.settings.ShowAdvancedOptionsByDefault }
    BoolDelegate { aspect: root.settings.UseJunctionsForSourceAndBuildDirectories }
}
