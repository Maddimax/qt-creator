// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// Which MIME types a language client is asked about. Several hundred of them,
// so the filter field above the list is the only way to find one.
AspectPage {
    id: root

    contentFillsHeight: true

    TableDelegate {
        objectName: "mimeTypeTable"
        aspect: root.aspects.MimeTypes
        Layout.fillWidth: true
        Layout.fillHeight: true
    }
}
