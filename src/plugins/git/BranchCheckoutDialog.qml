// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// What to do with local changes when checking a branch out. Whether the stash
// can be popped follows the choice and whether the next branch has one; both
// are the aspects' own doing.
AspectPage {
    id: root

    RadioGroupDelegate { aspect: root.aspects.Action }
    BoolDelegate { aspect: root.aspects.PopStash }
}
