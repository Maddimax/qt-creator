// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// Clang tools for one project: the same run options as the global page, and
// the diagnostics this project has suppressed. Whether the options can be
// changed at all is the container's answer.
AspectPage {
    id: root

    contentFillsHeight: true

    readonly property var settings: AspectModels.named(root.aspects.Settings)

    BoolWithOwnLabelDelegate { aspect: root.aspects.UseGlobalSettings }

    RowLayout {
        Layout.fillWidth: true

        ButtonDelegate { aspect: root.aspects.RestoreGlobal }
        Item { Layout.fillWidth: true }
        TextDisplayDelegate { aspect: root.aspects.GoToTools }
    }

    ClangToolsRunOptionsForm { aspects: root.settings }

    AspectGroupBox {
        title: qsTr("Suppressed diagnostics")
        Layout.fillHeight: true

        RowLayout {
            spacing: Spacing.GapHM

            TableDelegate {
                aspect: root.aspects.SuppressedDiagnostics
                Layout.fillHeight: true

                // Which row is current is the aspect's answer: the buttons
                // beside the table are enabled from the container.
                onCurrentRowChanged: root.aspects.SuppressedDiagnostics.setCurrentRow(currentRow)
            }

            ColumnLayout {
                Layout.alignment: Qt.AlignTop

                ButtonDelegate { aspect: root.aspects.RemoveSelected }
                ButtonDelegate { aspect: root.aspects.RemoveAll }
            }
        }
    }
}
