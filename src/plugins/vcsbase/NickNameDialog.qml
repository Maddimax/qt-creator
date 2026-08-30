// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// The names read from the mail map. Picking one is the whole dialog, so the
// row the reader is on and the row they meant both go back.
AspectPage {
    id: root

    contentFillsHeight: true

    TableDelegate {
        id: names

        objectName: "nickNameTable"
        aspect: root.aspects.Names
        onCurrentRowChanged: root.aspects.Names.setCurrentRow(names.currentRow)
        onRowActivated: (row) => root.aspects.Names.activateRow(row)
    }
}
