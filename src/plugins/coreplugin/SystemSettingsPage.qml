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

    // The terminal command is a container of its own three fields.
    readonly property var terminal: AspectModels.named(aspects.Terminal)

    TextWithActionDelegate { aspect: aspects.EnvironmentChanges }
    TextWithActionDelegate { aspect: aspects.EnvVarSeparators }

    AspectGroupBox {
        title: qsTr("Terminal")
        visible: aspects.Terminal.visible

        ColumnLayout {
            StringDelegate { aspect: root.terminal.Command }
            StringDelegate { aspect: root.terminal.OpenOptions }
            StringDelegate { aspect: root.terminal.ExecuteOptions }
        }
    }

    StringDelegate { aspect: aspects.FileBrowser }
    BoolDelegate { aspect: aspects.SupportDbusFileManagers }
    StringDelegate { aspect: aspects.PatchCommand }
    IntegerDelegate { aspect: aspects.MaxRecentFiles }
    SelectionDelegate { aspect: aspects.ReloadBehavior }

    RowLayout {
        BoolDelegate { aspect: aspects.AutoSaveEnabled }
        IntegerDelegate { aspect: aspects.AutoSaveInterval }
    }

    BoolDelegate { aspect: aspects.AutoSaveAfterRefactoring }

    BoolDelegate { aspect: aspects.DisableAtomicSave }

    RowLayout {
        BoolDelegate { aspect: aspects.AutoSuspendEnabled }
        IntegerDelegate { aspect: aspects.AutoSuspendMinDocuments }
    }

    RowLayout {
        BoolDelegate { aspect: aspects.WarnBeforeOpeningBigTextFiles }
        IntegerDelegate { aspect: aspects.BigTextFileSizeLimitInMB }
    }

    BoolDelegate { aspect: aspects.AskBeforeExit }

    RowLayout {
        BoolDelegate { aspect: aspects.CrashReportingEnabled }
        ButtonDelegate { aspect: aspects.CrashNow }
    }
}
