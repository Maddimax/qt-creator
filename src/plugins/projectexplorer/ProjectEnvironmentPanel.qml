// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// The environment a project adds, in the two surfaces the widget form had: the
// result as a table, and the changes as text. Which buttons can be pressed
// depends on the row that is current, which the container decides.
AspectPage {
    id: root

    contentFillsHeight: true

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillHeight: true

        TableDelegate {
            aspect: aspects.Variables
            Layout.fillHeight: true

            onCurrentRowChanged: root.aspects.Variables.setCurrentRow(currentRow)
        }

        ColumnLayout {
            spacing: Spacing.GapVXs
            Layout.alignment: Qt.AlignTop

            ButtonDelegate { aspect: aspects.Edit }
            ButtonDelegate { aspect: aspects.Add }
            ButtonDelegate { aspect: aspects.Reset }
            ButtonDelegate { aspect: aspects.Unset }
            ButtonDelegate { aspect: aspects.Toggle }
            ButtonDelegate { aspect: aspects.AppendPath }
            ButtonDelegate { aspect: aspects.PrependPath }
        }
    }

    TextAreaDelegate { aspect: aspects.Changes }
}
