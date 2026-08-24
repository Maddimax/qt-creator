// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui
import QtCreator.TextEditor

AspectPage {
    id: root

    contentFillsHeight: true

    SelectionDelegate { aspect: aspects.Group }

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true
        Layout.fillHeight: true

        ColumnLayout {
            spacing: Spacing.GapVS
            Layout.fillWidth: true
            Layout.fillHeight: true

            TableDelegate {
                id: table

                aspect: aspects.Snippets
                Layout.fillHeight: true

                // The content below is a view of whichever row is current, so
                // the aspect has to be told which that is.
                onCurrentRowChanged: root.aspects.Snippets.setCurrentRow(currentRow)
            }

            SnippetEditor {
                aspect: aspects.Content
                mimeType: root.aspects.Snippets.mimeType
                Layout.fillHeight: true
            }
        }

        ColumnLayout {
            spacing: Spacing.GapVS
            Layout.alignment: Qt.AlignTop

            ButtonDelegate { aspect: aspects.AddSnippet }
            ButtonDelegate { aspect: aspects.RemoveSnippet }
            ButtonDelegate { aspect: aspects.RevertBuiltIn }
            ButtonDelegate { aspect: aspects.RestoreRemovedBuiltIns }
            ButtonDelegate { aspect: aspects.ResetAll }
        }
    }
}
