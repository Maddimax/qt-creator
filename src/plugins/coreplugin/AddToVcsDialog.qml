// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick.Layouts
import QtCreator.Ui

// The files that are about to be put under version control. Read out rather
// than picked from - the answer is Yes or No below.
AspectPage {
    id: root

    contentFillsHeight: true

    TextDisplayDelegate { aspect: root.aspects.Question }

    StringListEditorDelegate {
        aspect: root.aspects.Files
        Layout.fillHeight: true
    }
}
