// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick.Layouts
import QtCreator.Ui
import QtCreator.TextEditor

// Directives that are compiled, so they are read as C++ rather than as prose:
// the same editor a snippet gets, with the language on. The group is where the
// C++ plugin registers how C++ is highlighted, indented and completed - see
// Constants::CPP_SNIPPETS_GROUP_ID, which this has to match.
AspectPage {
    id: root

    contentFillsHeight: true

    SnippetEditor {
        aspect: root.aspects.Directives
        mimeType: "text/x-c++src"
        snippetGroup: "C++"
        Layout.fillHeight: true
    }
}
