// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// Properties handed to qbs on the command line. Both columns are typed into,
// and the buttons sit beside the table because the table is what is edited.
AspectPage {
    id: root

    contentFillsHeight: true

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true
        Layout.fillHeight: true

        TableDelegate {
            objectName: "propertiesTable"

            aspect: root.aspects.Properties
            Layout.fillHeight: true

            // Remove acts on what is picked, and only the table knows which
            // rows those are.
            onSelectedRowsChanged: root.aspects.Properties.setSelectedRows(selectedRows)
        }

        ColumnLayout {
            spacing: Spacing.GapVS
            Layout.alignment: Qt.AlignTop

            ButtonDelegate { aspect: root.aspects.Add }
            ButtonDelegate { aspect: root.aspects.Remove }
        }
    }
}
