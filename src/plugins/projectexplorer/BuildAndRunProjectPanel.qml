// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// Building and running for one project. The settings container behind this
// holds every build-and-run setting there is - the global page shows them all -
// so this lists the ones a project owns.
AspectPage {
    id: root

    readonly property var settings: AspectModels.named(aspects.Settings)

    BoolWithOwnLabelDelegate { aspect: aspects.UseGlobalSettings }
    ButtonDelegate { aspect: aspects.RestoreGlobal }

    BoolDelegate { aspect: root.settings.AddLibraryPathsToRunEnv }
    BoolDelegate { aspect: root.settings.AutomaticallyCreateRunConfigurations }
    BoolDelegate { aspect: root.settings.LowBuildPriority }
    BoolDelegate { aspect: root.settings.WarnAgainstNonAsciiBuildDir }

    SelectionDelegate { aspect: root.settings.TerminalMode }
    SelectionDelegate { aspect: root.settings.SyncRunConfigurations }
    IntegerDelegate { aspect: root.settings.ReaperTimeout }
}
