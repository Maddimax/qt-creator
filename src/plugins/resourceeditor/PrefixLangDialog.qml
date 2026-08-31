// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// The prefix a group of resources is reached under, and the language it is
// for. Both are free text: the resource system takes whatever is written.
AspectPage {
    id: root

    StringDelegate { aspect: root.aspects.Prefix }
    StringDelegate { aspect: root.aspects.Language }
}
