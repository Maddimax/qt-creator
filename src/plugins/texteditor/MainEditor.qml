// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.TextEditor

// A file being edited. The document is opened by the editor manager and handed
// over, so this shows one rather than owning one - see AdoptedSource.
Item {
    id: root

    required property CodeSource source

    CodeViewport {
        anchors.fill: parent

        source: root.source
        showLineNumbers: true
        showFoldMarkers: true
    }
}
