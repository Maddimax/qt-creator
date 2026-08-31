// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick.Layouts
import QtCreator.Ui

// Every process on the machine, narrowed by what is typed above the list. A
// process chosen and meant - a double click, or Return - is the same as
// pressing Attach.
AspectPage {
    id: root

    contentFillsHeight: true

    TableDelegate {
        id: processes

        objectName: "processTable"
        aspect: root.aspects.Processes
        Layout.fillWidth: true
        Layout.fillHeight: true

        onCurrentRowChanged: root.aspects.Processes.setCurrentRow(processes.currentRow)
        onRowActivated: (row) => root.aspects.Processes.activateRow(row)
    }
}
