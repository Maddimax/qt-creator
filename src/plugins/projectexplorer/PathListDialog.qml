// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick.Layouts
import QtCreator.Ui

// A PATH-like variable, one directory per row. What the buttons beside it are
// - Add..., Edit..., Remove, and the two that move a row - is the aspect's
// answer, because the entries are paths and their order means something.
AspectPage {
    id: root

    contentFillsHeight: true

    StringListEditorDelegate {
        aspect: root.aspects.Paths
        Layout.fillHeight: true
    }
}
