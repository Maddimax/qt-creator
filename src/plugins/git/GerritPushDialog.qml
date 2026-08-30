// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// What to push where. The commits marked in the list are the ones that go;
// which those are is the model's answer, so nothing here says it.
AspectPage {
    id: root

    contentFillsHeight: true

    SelectionDelegate { aspect: root.aspects.LocalBranch }
    InlineGroupDelegate { aspect: root.aspects.Remote }
    SelectionDelegate { aspect: root.aspects.TargetBranch }

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

    TextDisplayDelegate { aspect: root.aspects.Info }

    StringDelegate { aspect: root.aspects.Topic }
    TriStateDelegate { aspect: root.aspects.Draft }
    TriStateDelegate { aspect: root.aspects.Wip }
    StringDelegate { aspect: root.aspects.Reviewers }
}
