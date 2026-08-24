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

    contentFillsHeight: true

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true
        Layout.fillHeight: true

        TableDelegate {
            aspect: root.aspects.Parsers
            Layout.fillHeight: true

            // Add, Edit and Export act on what is selected, and only the page
            // knows which rows those are.
            onSelectedRowsChanged: root.aspects.Parsers.setSelectedRows(selectedRows)
        }

        ColumnLayout {
            spacing: Spacing.GapVS
            Layout.alignment: Qt.AlignTop

            ButtonDelegate { aspect: root.aspects.AddParser }
            ButtonDelegate { aspect: root.aspects.EditParser }
            ButtonDelegate { aspect: root.aspects.ExportParsers }
            ButtonDelegate { aspect: root.aspects.ImportParsers }
        }
    }

    TextDisplayDelegate { aspect: root.aspects.Hint }
}
