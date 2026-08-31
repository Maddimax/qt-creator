// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick.Layouts
import QtCreator.Ui

// The documents with unsaved changes, and which of them to save. What the
// buttons below say follows the selection, so the list reports it.
AspectPage {
    id: root

    contentFillsHeight: true

    TextDisplayDelegate { aspect: root.aspects.Warning }
    TextDisplayDelegate { aspect: root.aspects.Message }

    TableDelegate {
        id: documents

        objectName: "saveItemsTable"
        aspect: root.aspects.Documents
        Layout.fillWidth: true
        Layout.fillHeight: true

        onCurrentRowChanged: root.aspects.Documents.setCurrentRow(documents.currentRow)
        onSelectedRowsChanged: root.aspects.Documents.setSelectedRows(documents.selectedRows)
    }

    BoolWithOwnLabelDelegate { aspect: root.aspects.AlwaysSave }
}
