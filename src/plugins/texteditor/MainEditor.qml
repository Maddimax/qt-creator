// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui
import QtCreator.TextEditor

// A file being edited. The document is opened by the editor manager and handed
// over, so this shows one rather than owning one - see AdoptedSource.
Item {
    id: root

    required property CodeSource source
    // What a right click offers, assembled by the editor from the same place
    // the widget editor takes it.
    required property ActionModel contextActions

    CodeViewport {
        anchors.fill: parent

        source: root.source
        contextActions: root.contextActions
        showLineNumbers: true
        showFoldMarkers: true
        // A viewport is a view until told otherwise - which is right for a
        // settings preview and wrong for this. The file's own read-only state
        // is a separate question, and the viewport asks the document that one
        // itself.
        readOnly: false
    }
}
