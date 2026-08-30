// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// The separators used for environment variables that hold a list but do not
// use the platform's path separator. Two editable columns, and the buttons
// beside them rather than under: what is being edited is the table.
AspectPage {
    id: root

    contentFillsHeight: true

    TextDisplayDelegate { aspect: root.aspects.Explanation }

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true
        Layout.fillHeight: true

        TableDelegate {
            objectName: "separatorsTable"

            aspect: root.aspects.Separators
            Layout.fillHeight: true

            // Remove acts on what is picked, and only the table knows which
            // rows those are.
            onSelectedRowsChanged: root.aspects.Separators.setSelectedRows(selectedRows)
        }

        ColumnLayout {
            spacing: Spacing.GapVS
            Layout.alignment: Qt.AlignTop

            ButtonDelegate { aspect: root.aspects.Add }
            ButtonDelegate { aspect: root.aspects.Remove }
        }
    }
}
