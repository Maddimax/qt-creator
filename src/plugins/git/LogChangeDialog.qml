// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// A list of commits to pick from. Which rows are struck through or marked is
// the model's answer, so nothing here says it.
AspectPage {
    id: root

    contentFillsHeight: true

    TextDisplayDelegate { aspect: root.aspects.Prompt }
    TextDisplayDelegate { aspect: root.aspects.Hint }

    TableDelegate {
        id: commits

        objectName: "commitTable"
        aspect: root.aspects.Commits

        onCurrentRowChanged: root.aspects.Commits.setCurrentRow(commits.currentRow)
        onSelectedRowsChanged: root.aspects.Commits.setSelectedRows(commits.selectedRows)
        onRowActivated: (row) => root.aspects.Commits.activateRow(row)

        Connections {
            target: root.aspects.Commits

            function onSelectRowRequested(row: int): void {
                commits.selectRow(row)
            }
        }
    }

    SelectionDelegate { aspect: root.aspects.ResetType }
}
