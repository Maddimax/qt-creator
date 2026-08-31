// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// Which diagnostics to show. The three buttons pick three sets of rows;
// whether the middle one is worth offering is the aspect's answer.
AspectPage {
    id: root

    contentFillsHeight: true

    TextDisplayDelegate { aspect: root.aspects.Message }

    RowLayout {
        spacing: Spacing.GapHS
        Layout.fillWidth: true

        ButtonDelegate { aspect: root.aspects.SelectAll }
        ButtonDelegate { aspect: root.aspects.SelectWithFixits }
        ButtonDelegate { aspect: root.aspects.SelectNone }

        Item { Layout.fillWidth: true }
    }

    TableDelegate {
        id: checks

        objectName: "filterChecksTable"
        aspect: root.aspects.Checks
        Layout.fillWidth: true
        Layout.fillHeight: true

        onCurrentRowChanged: root.aspects.Checks.setCurrentRow(checks.currentRow)
        onSelectedRowsChanged: root.aspects.Checks.setSelectedRows(checks.selectedRows)
    }
}
