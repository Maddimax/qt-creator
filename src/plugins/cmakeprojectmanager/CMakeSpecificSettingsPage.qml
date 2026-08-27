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

    BoolDelegate { aspect: root.aspects.AutorunCMake }
    BoolDelegate { aspect: root.aspects.CleanOldOutput }
    BoolDelegate { aspect: root.aspects.PackageManagerAutoSetup }
    BoolDelegate { aspect: root.aspects.MaintenanceToolDependencyProvider }
    BoolDelegate { aspect: root.aspects.AskReConfigureInitialParams }
    BoolDelegate { aspect: root.aspects.AskBeforePresetsReload }
    BoolDelegate { aspect: root.aspects.ShowSourceSubFolders }
    BoolDelegate { aspect: root.aspects.ShowAdvancedOptionsByDefault }
    BoolDelegate { aspect: root.aspects.UseJunctionsForSourceAndBuildDirectories }
}
