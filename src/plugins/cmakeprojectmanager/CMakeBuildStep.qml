// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

AspectPage {
    id: root

    contentFillsHeight: true

    StringDelegate { aspect: root.aspects.CMakeArguments }
    StringDelegate { aspect: root.aspects.ToolArguments }

    BoolDelegate { aspect: root.aspects.UseStaging }
    StringDelegate { aspect: root.aspects.StagingDirectory }

    // Only where the kit is iOS with the Xcode generator, which the aspect
    // decides for itself.
    BoolDelegate { aspect: root.aspects.AutomaticProvisioningUpdates }

    // Which targets to build. The rows come from the model the step keeps and
    // the check states are the step's own.
    TableDelegate { aspect: root.aspects.Targets }

    // Only a preset build has an environment of its own to edit.
    BoolDelegate { aspect: root.aspects.ClearEnvironment }
    EnvironmentEditorDelegate { aspect: root.aspects.Environment }
}
