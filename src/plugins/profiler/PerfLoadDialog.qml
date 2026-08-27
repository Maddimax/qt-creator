// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// A perf trace, and what to read it against. Both paths browse for themselves.
AspectPage {
    id: root

    StringDelegate { aspect: root.aspects.TraceFile }
    StringDelegate { aspect: root.aspects.ExecutableDir }
    SelectionDelegate { aspect: root.aspects.Kit }
}
