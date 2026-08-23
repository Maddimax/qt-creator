// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    AspectGroupBox {
        title: qsTr("Projects Directory")

        ColumnLayout {
            RadioDelegate { aspect: aspects.UseCurrentDirectory }

            RowLayout {
                RadioDelegate { aspect: aspects.UseProjectDirectory }
                StringDelegate { aspect: aspects.ProjectsDirectory }
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Closing Projects")

        BoolDelegate { aspect: aspects.CloseFilesWithProject }
    }

    AspectGroupBox {
        title: qsTr("Build and Run")

        ColumnLayout {
            BoolDelegate { aspect: aspects.SaveBeforeBuild }
            BoolDelegate { aspect: aspects.DeployBeforeRun }
            BoolDelegate { aspect: aspects.AddLibraryPathsToRunEnv }
            BoolDelegate { aspect: aspects.PromptToStopRunControl }
            BoolDelegate { aspect: aspects.PromptToStopOnCloseTab }
            BoolDelegate { aspect: aspects.AutomaticallyCreateRunConfigurations }
            BoolDelegate { aspect: aspects.ClearIssuesOnRebuild }
            BoolDelegate { aspect: aspects.AbortBuildAllOnError }
            BoolDelegate { aspect: aspects.LowBuildPriority }
            BoolDelegate { aspect: aspects.WarnAgainstNonAsciiBuildDir }

            SelectionDelegate { aspect: aspects.ShowAllKits }

            RowLayout {
                TextDisplayDelegate { aspect: aspects.AppEnvChangeDisplay }
                ButtonDelegate { aspect: aspects.ChangeAppEnv }
            }

            SelectionDelegate { aspect: aspects.BuildBeforeDeploy }
            SelectionDelegate { aspect: aspects.StopBeforeBuild }
            SelectionDelegate { aspect: aspects.TerminalMode }
            SelectionDelegate { aspect: aspects.SyncRunConfigurations }
            IntegerDelegate { aspect: aspects.ReaperTimeout }

            TextDisplayDelegate { aspect: aspects.JomNote }
            BoolDelegate { aspect: aspects.UseJom }
        }
    }
}
