// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick.Layouts
import QtCreator.Ui

// The files in the repository that version control does not know about, and
// which of them to delete. "Select All" and the rows follow each other; that
// is the container's business, not this one's.
AspectPage {
    id: root

    contentFillsHeight: true

    TextDisplayDelegate { aspect: root.aspects.Repository }
    BoolWithOwnLabelDelegate { aspect: root.aspects.SelectAll }

    TableDelegate {
        id: files

        objectName: "cleanFilesTable"
        aspect: root.aspects.Files
        Layout.fillWidth: true
        Layout.fillHeight: true

        onCurrentRowChanged: root.aspects.Files.setCurrentRow(files.currentRow)
        onRowActivated: (row) => root.aspects.Files.activateRow(row)
    }
}
