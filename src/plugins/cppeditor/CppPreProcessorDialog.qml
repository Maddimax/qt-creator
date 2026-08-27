// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick.Layouts
import QtCreator.Ui
import QtCreator.TextEditor

// Directives that are compiled, so they are read as C++ rather than as prose:
// the same editor a snippet gets, with the language's highlighting on.
AspectPage {
    id: root

    contentFillsHeight: true

    SnippetEditor {
        aspect: root.aspects.Directives
        mimeType: "text/x-c++src"
        Layout.fillHeight: true
    }
}
