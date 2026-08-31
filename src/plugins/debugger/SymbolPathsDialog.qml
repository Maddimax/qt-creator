// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// Where the debugger looks for the symbols of the operating system's
// libraries. The explanation is most of the dialog, which is why it is read as
// markup rather than as one long line.
AspectPage {
    id: root

    TextDisplayDelegate { aspect: root.aspects.Explanation }
    BoolWithOwnLabelDelegate { aspect: root.aspects.UseSymbolCache }
    BoolWithOwnLabelDelegate { aspect: root.aspects.UseSymbolServer }
    StringDelegate { aspect: root.aspects.Path }
}
