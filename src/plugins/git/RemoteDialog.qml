// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// The remotes of a repository. Adding is always possible; everything else
// acts on the remote the reader is on, so the container turns those off
// until there is one.
AspectPage {
    id: root

    contentFillsHeight: true

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true

        TextDisplayDelegate {
            aspect: root.aspects.Repository
            Layout.fillWidth: true
        }

        ButtonDelegate { aspect: root.aspects.Refresh }
    }

    TableDelegate {
        id: remotes

        objectName: "remoteTable"
        aspect: root.aspects.Remotes
        Layout.fillWidth: true
        Layout.fillHeight: true

        onCurrentRowChanged: root.aspects.Remotes.setCurrentRow(remotes.currentRow)
        onSelectedRowsChanged: root.aspects.Remotes.setSelectedRows(remotes.selectedRows)
        onRowActivated: (row) => root.aspects.Remotes.activateRow(row)
    }

    RowLayout {
        spacing: Spacing.GapHS
        Layout.fillWidth: true

        Item { Layout.fillWidth: true }

        ButtonDelegate { aspect: root.aspects.Add }
        ButtonDelegate { aspect: root.aspects.Fetch }
        ButtonDelegate { aspect: root.aspects.Push }
        ButtonDelegate { aspect: root.aspects.Remove }
    }
}
