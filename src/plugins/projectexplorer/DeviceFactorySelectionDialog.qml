// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick.Layouts
import QtCreator.Ui

// Which kind of device to start a wizard for. A kind chosen and meant - a
// double click, or Return - is the same as pressing Start Wizard.
AspectPage {
    id: root

    contentFillsHeight: true

    TableDelegate {
        id: types

        objectName: "deviceTypeTable"
        aspect: root.aspects.Types
        Layout.fillWidth: true
        Layout.fillHeight: true

        onCurrentRowChanged: root.aspects.Types.setCurrentRow(types.currentRow)
        onSelectedRowsChanged: root.aspects.Types.setSelectedRows(types.selectedRows)
        onRowActivated: (row) => root.aspects.Types.activateRow(row)
    }
}
