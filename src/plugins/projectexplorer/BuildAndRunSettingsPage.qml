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

    AspectGroupBox {
        title: qsTr("Projects Directory")

        ColumnLayout {
            RadioDelegate { aspect: root.aspects.UseCurrentDirectory }

            RowLayout {
                RadioDelegate { aspect: root.aspects.UseProjectDirectory }
                StringDelegate { aspect: root.aspects.ProjectsDirectory }
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Closing Projects")

        BoolDelegate { aspect: root.aspects.CloseFilesWithProject }
    }

    AspectGroupBox {
        title: qsTr("Build and Run")

        ColumnLayout {
            BoolDelegate { aspect: root.aspects.SaveBeforeBuild }
            BoolDelegate { aspect: root.aspects.DeployBeforeRun }
            BoolDelegate { aspect: root.aspects.AddLibraryPathsToRunEnv }
            BoolDelegate { aspect: root.aspects.PromptToStopRunControl }
            BoolDelegate { aspect: root.aspects.PromptToStopOnCloseTab }
            BoolDelegate { aspect: root.aspects.AutomaticallyCreateRunConfigurations }
            BoolDelegate { aspect: root.aspects.ClearIssuesOnRebuild }
            BoolDelegate { aspect: root.aspects.AbortBuildAllOnError }
            BoolDelegate { aspect: root.aspects.LowBuildPriority }
            BoolDelegate { aspect: root.aspects.WarnAgainstNonAsciiBuildDir }

            SelectionDelegate { aspect: root.aspects.ShowAllKits }

            RowLayout {
                TextDisplayDelegate { aspect: root.aspects.AppEnvChangeDisplay }
                ButtonDelegate { aspect: root.aspects.ChangeAppEnv }
            }

            SelectionDelegate { aspect: root.aspects.BuildBeforeDeploy }
            SelectionDelegate { aspect: root.aspects.StopBeforeBuild }
            SelectionDelegate { aspect: root.aspects.TerminalMode }
            SelectionDelegate { aspect: root.aspects.SyncRunConfigurations }
            IntegerDelegate { aspect: root.aspects.ReaperTimeout }

            TextDisplayDelegate { aspect: root.aspects.JomNote }
            BoolDelegate { aspect: root.aspects.UseJom }
        }
    }
}
