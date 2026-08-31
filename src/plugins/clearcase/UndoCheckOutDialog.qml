// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// Throwing away a check-out. The middle line is a warning because the file
// has changes in it that are about to go.
AspectPage {
    id: root

    TextDisplayDelegate { aspect: root.aspects.Question }
    TextDisplayDelegate { aspect: root.aspects.Modified }
    BoolWithOwnLabelDelegate { aspect: root.aspects.Keep }
}
