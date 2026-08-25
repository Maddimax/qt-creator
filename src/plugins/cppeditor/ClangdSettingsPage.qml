// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    id: root

    contentFillsHeight: true

    readonly property var settings: AspectModels.named(aspects.Settings)

    BoolDelegate { aspect: root.settings.UseClangd }

    ColumnLayout {
        spacing: Spacing.GapVS
        Layout.fillWidth: true
        enabled: root.settings.UseClangd.value

        StringDelegate { aspect: root.settings.ClangdPath }
        // Whether the clangd that was named is one Qt Creator can use. Found
        // out by running it, so it is not there until it is.
        TextDisplayDelegate { aspect: root.aspects.VersionWarning }

        SelectionDelegate { aspect: root.settings.IndexingPriority }
        StringDelegate { aspect: root.settings.ProjectIndexPathTemplate }
        StringDelegate { aspect: root.settings.SessionIndexPathTemplate }
        SelectionDelegate { aspect: root.settings.HeaderSourceSwitchMode }
        IntegerDelegate { aspect: root.settings.WorkerThreadLimit }

        BoolDelegate { aspect: root.settings.AutoIncludeHeaders }
        BoolDelegate { aspect: root.settings.UpdateDependentSources }
        BoolDelegate { aspect: root.settings.UseExternalCompilationDb }

        IntegerDelegate { aspect: root.settings.CompletionResults }
        SelectionDelegate { aspect: root.settings.CompletionRankingModel }
        SelectionDelegate { aspect: root.settings.CompletionStyle }
        IntegerDelegate { aspect: root.settings.DocumentUpdateThreshold }

        RowLayout {
            spacing: Spacing.GapHM
            Layout.fillWidth: true

            BoolDelegate {
                aspect: root.settings.SizeThresholdEnabled
                Layout.fillWidth: false
            }

            IntegerDelegate {
                aspect: root.settings.SizeThresholdInKb
                enabled: root.settings.SizeThresholdEnabled.value
            }
        }

        // A name and the button that changes it: the configuration is edited
        // in a dialog of its own.
        TextWithActionDelegate { aspect: root.settings.DiagnosticConfigId }
    }

    AspectGroupBox {
        title: qsTr("Sessions with a Single Clangd Instance")
        Layout.fillHeight: true

        TableDelegate { aspect: root.aspects.Sessions }
    }

    TextDisplayDelegate { aspect: root.aspects.ConfigFilesHelp }
}
