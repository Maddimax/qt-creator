// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// Revert to the default revision, or to one the user names. The field's
// enabling is the aspect's - it follows the flag - so nothing here says so.
AspectPage {
    id: root

    BoolWithOwnLabelDelegate { aspect: root.aspects.SpecifyRevision }
    StringDelegate { aspect: root.aspects.Revision }
}
