// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// A command to analyse on another machine, and the kit that says which one.
AspectPage {
    id: root

    InlineGroupDelegate { aspect: root.aspects.Kit }
    StringDelegate { aspect: root.aspects.Executable }
    StringDelegate { aspect: root.aspects.Arguments }
    StringDelegate { aspect: root.aspects.WorkingDirectory }
}
