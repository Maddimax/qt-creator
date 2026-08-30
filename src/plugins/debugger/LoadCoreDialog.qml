// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// A core file and what to read it against. Which fields are filled in is the
// dialog's doing: choosing a core file asks the debugger what produced it.
AspectPage {
    id: root

    InlineGroupDelegate { aspect: root.aspects.Kit }
    StringDelegate { aspect: root.aspects.CoreFile }
    StringDelegate { aspect: root.aspects.SymbolFile }
    StringDelegate { aspect: root.aspects.StartScript }
    StringDelegate { aspect: root.aspects.SysRoot }
}
