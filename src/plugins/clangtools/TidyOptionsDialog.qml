// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// The options of one clang-tidy check: a name and a value per row, with the
// two things that can be done to the list beside it.
AspectPage {
    id: root

    contentFillsHeight: true

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true
        Layout.fillHeight: true

        TableDelegate {
            id: options

            objectName: "tidyOptionsTable"
            aspect: root.aspects.Options
            Layout.fillWidth: true
            Layout.fillHeight: true

            onCurrentRowChanged: root.aspects.Options.setCurrentRow(options.currentRow)
            onSelectedRowsChanged: root.aspects.Options.setSelectedRows(options.selectedRows)
        }

        ColumnLayout {
            spacing: Spacing.GapVS
            Layout.alignment: Qt.AlignTop

            ButtonDelegate { aspect: root.aspects.Add }
            ButtonDelegate { aspect: root.aspects.Remove }
        }
    }
}
