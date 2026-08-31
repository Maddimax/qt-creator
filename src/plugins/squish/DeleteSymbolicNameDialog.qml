// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick.Layouts
import QtCreator.Ui

// What to do with the names that referenced the one being removed. Only the
// first answer needs a name to point them at, so the list follows the choice.
AspectPage {
    id: root

    contentFillsHeight: true

    TextDisplayDelegate { aspect: root.aspects.Details }
    RadioGroupDelegate { aspect: root.aspects.Action }

    TableDelegate {
        id: names

        objectName: "symbolicNamesTable"
        aspect: root.aspects.Names
        Layout.fillWidth: true
        Layout.fillHeight: true

        onCurrentRowChanged: root.aspects.Names.setCurrentRow(names.currentRow)
        onSelectedRowsChanged: root.aspects.Names.setSelectedRows(names.selectedRows)
    }
}
