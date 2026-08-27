// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// What a Code Style page shows for a language that names no form of its own:
// which style is in use and what it does to code. Those two are the page's,
// not the language's, so they are all a language has to do nothing to get.
AspectPage {
    id: root

    contentFillsHeight: true

    CodeStyleSelector { aspects: root.aspects }

    CodeStylePreview { aspects: root.aspects }
}
