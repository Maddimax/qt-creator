// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// Fetching a paste: which service to ask, and which of its pastes. The list is
// what the service answered, and moving through it fills the field.
AspectPage {
    id: root

    contentFillsHeight: true

    SelectionDelegate { aspect: root.aspects.Protocol }
    StringDelegate { aspect: root.aspects.Paste }

    TableDelegate {
        id: pastes

        objectName: "pasteTable"
        aspect: root.aspects.Pastes
        Layout.fillWidth: true
        Layout.fillHeight: true

        onCurrentRowChanged: root.aspects.Pastes.setCurrentRow(pastes.currentRow)
        onRowActivated: (row) => root.aspects.Pastes.activateRow(row)
    }

    RowLayout {
        Layout.fillWidth: true

        Item { Layout.fillWidth: true }

        ButtonDelegate { aspect: root.aspects.Refresh }
    }
}
