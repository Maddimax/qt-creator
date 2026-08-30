// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
// A set of environment changes, in the two surfaces the widget form had: the
// result as a table, and the changes as text. Which buttons can be pressed
// depends on the row that is current, which the container decides.
//
// Takes the names an EnvironmentEditorAspect registers, so any page that holds
// one can draw it: AspectModels.named(<the aspect>).
ColumnLayout {
    id: root

    required property var editor

    spacing: Spacing.GapVXs

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillHeight: true

        TableDelegate {
            aspect: root.editor.Variables
            Layout.fillHeight: true

            onCurrentRowChanged: root.editor.Variables.setCurrentRow(currentRow)
        }

        ColumnLayout {
            spacing: Spacing.GapVXs
            Layout.alignment: Qt.AlignTop

            ButtonDelegate { aspect: root.editor.Edit }
            ButtonDelegate { aspect: root.editor.Add }
            ButtonDelegate { aspect: root.editor.Reset }
            ButtonDelegate { aspect: root.editor.Unset }
            ButtonDelegate { aspect: root.editor.Toggle }
            ButtonDelegate { aspect: root.editor.AppendPath }
            ButtonDelegate { aspect: root.editor.PrependPath }
            ButtonDelegate { aspect: root.editor.OpenTerminal }
        }
    }

    TextAreaDelegate { aspect: root.editor.Changes }
}
