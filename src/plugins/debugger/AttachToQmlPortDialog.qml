// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// Which kit to debug with, and the port the QML engine is listening on.
AspectPage {
    id: root

    InlineGroupDelegate { aspect: root.aspects.Kit }
    IntegerDelegate { aspect: root.aspects.Port }
}
