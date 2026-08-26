// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// The clangd settings themselves, without a page around them: the global page
// and a project's panel show the same things and differ only in whose
// container they are shown for. What a project does not have is the sessions
// table and the note about configuration files, which are global.
ColumnLayout {
    id: root

    // A NamedAspects for whichever ClangdSettings is being shown.
    required property var settings
    // Whether the clangd that was named can be used. Not one of the settings:
    // it is found out by running it, so whoever shows the settings owns it.
    required property var versionWarning

    spacing: Spacing.GapVS
    Layout.fillWidth: true

    BoolDelegate { aspect: root.settings.UseClangd }

    ColumnLayout {
        spacing: Spacing.GapVS
        Layout.fillWidth: true
        enabled: root.settings.UseClangd.value

        StringDelegate { aspect: root.settings.ClangdPath }
        // Whether the clangd that was named is one Qt Creator can use. Found
        // out by running it, so it is not there until it is.
        TextDisplayDelegate { aspect: root.versionWarning }

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
}
