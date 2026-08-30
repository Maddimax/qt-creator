// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// What to call a session. What is wrong with a name is the aspect's to say,
// and the field shows it under itself.
AspectPage {
    id: root

    TextDisplayDelegate { aspect: root.aspects.Prompt }
    StringDelegate { aspect: root.aspects.Name }
}
