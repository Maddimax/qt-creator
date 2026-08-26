// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// How files are read and written. The global page and a project's Editor panel show
// the same things and differ only in whose container they are shown for.
AspectGroupBox {
    id: root

    // A NamedAspects for whichever container is being shown.
    required property var aspects

    title: qsTr("File Encodings")

    ColumnLayout {
        SelectionDelegate { aspect: root.aspects.DefaultFileEncoding }
        SelectionDelegate { aspect: root.aspects.Utf8BomBehavior }
        SelectionDelegate { aspect: root.aspects.LineEndingBehavior }
    }
}
