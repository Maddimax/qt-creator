// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick.Layouts
import QtCreator.Ui

// Which page a keyword should open. Type to narrow, arrow to pick, Return to
// take it - the arrows work from the filter field, which is the table's own
// business.
AspectPage {
    id: root

    contentFillsHeight: true

    TextDisplayDelegate { aspect: root.aspects.Question }

    TableDelegate {
        id: topics

        objectName: "topicsTable"
        aspect: root.aspects.Topics
        Layout.fillWidth: true
        Layout.fillHeight: true

        onCurrentRowChanged: root.aspects.Topics.setCurrentRow(topics.currentRow)
        onRowActivated: (row) => root.aspects.Topics.activateRow(row)
    }
}
