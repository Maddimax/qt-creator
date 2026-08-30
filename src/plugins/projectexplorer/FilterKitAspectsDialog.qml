// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// Which kit settings are shown. No buttons beside it: the table is the whole
// dialog, and a setting the kit cannot do without has no box to untick.
AspectPage {
    id: root

    contentFillsHeight: true

    TableDelegate {
        objectName: "aspectsTable"

        aspect: root.aspects.Aspects
        Layout.fillWidth: true
        Layout.fillHeight: true
    }
}
