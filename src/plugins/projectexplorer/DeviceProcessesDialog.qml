// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// The processes on a device, and which device that is. Which row the reader is
// on is the table's to know and the dialog's to act on, so it is handed back.
AspectPage {
    id: root

    InlineGroupDelegate { aspect: root.aspects.Kit }

    TableDelegate {
        id: processes

        objectName: "processTable"
        aspect: root.aspects.Processes
        onCurrentRowChanged: root.aspects.Processes.setCurrentRow(processes.currentRow)
    }

    TextDisplayDelegate { aspect: root.aspects.Error }
}
