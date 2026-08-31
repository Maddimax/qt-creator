// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick.Layouts
import QtCreator.Ui

// Which of the files that are already there may be written over. The folder
// they are all in is said once, above, so each row is only a name.
AspectPage {
    id: root

    contentFillsHeight: true

    TextDisplayDelegate { aspect: root.aspects.Question }

    TableDelegate {
        objectName: "overwriteTable"
        aspect: root.aspects.Files
        Layout.fillWidth: true
        Layout.fillHeight: true
    }
}
