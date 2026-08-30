// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// Which encoding to read the file as. Picking one is the whole dialog, so the
// row the reader is on and the row they meant both go back.
AspectPage {
    id: root

    contentFillsHeight: true

    TextDisplayDelegate { aspect: root.aspects.Message }

    TableDelegate {
        id: encodings

        objectName: "encodingTable"
        aspect: root.aspects.Encodings
        onCurrentRowChanged: root.aspects.Encodings.setCurrentRow(encodings.currentRow)
        onRowActivated: (row) => root.aspects.Encodings.activateRow(row)
    }
}
