// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// The stashes of a repository, with what can be done to them beside the list.
// Showing and restoring act on the one the reader is on, deleting on all they
// picked - which is why both are handed back.
AspectPage {
    id: root

    contentFillsHeight: true

    TextDisplayDelegate { aspect: root.aspects.Repository }

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true
        Layout.fillHeight: true

        TableDelegate {
            id: stashes

            objectName: "stashTable"
            aspect: root.aspects.Stashes
            Layout.fillWidth: true
            Layout.fillHeight: true

            onCurrentRowChanged: root.aspects.Stashes.setCurrentRow(stashes.currentRow)
            onSelectedRowsChanged: root.aspects.Stashes.setSelectedRows(stashes.selectedRows)
            onRowActivated: (row) => root.aspects.Stashes.activateRow(row)
        }

        ColumnLayout {
            spacing: Spacing.GapVS
            Layout.alignment: Qt.AlignTop

            ButtonDelegate { aspect: root.aspects.Show }
            ButtonDelegate { aspect: root.aspects.Refresh }
            ButtonDelegate { aspect: root.aspects.Restore }
            ButtonDelegate { aspect: root.aspects.RestoreInBranch }
            ButtonDelegate { aspect: root.aspects.DeleteSelection }
            ButtonDelegate { aspect: root.aspects.DeleteAll }
        }
    }
}
