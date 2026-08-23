// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    BoolDelegate { aspect: aspects.AutorunCMake }
    BoolDelegate { aspect: aspects.CleanOldOutput }
    BoolDelegate { aspect: aspects.PackageManagerAutoSetup }
    BoolDelegate { aspect: aspects.MaintenanceToolDependencyProvider }
    BoolDelegate { aspect: aspects.AskReConfigureInitialParams }
    BoolDelegate { aspect: aspects.AskBeforePresetsReload }
    BoolDelegate { aspect: aspects.ShowSourceSubFolders }
    BoolDelegate { aspect: aspects.ShowAdvancedOptionsByDefault }
    BoolDelegate { aspect: aspects.UseJunctionsForSourceAndBuildDirectories }
}
