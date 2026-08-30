// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// Naming a recorded macro. The name becomes part of a file name, so what it
// may hold is narrow - the field says so rather than dropping the keystroke.
AspectPage {
    id: root

    StringDelegate { aspect: root.aspects.Name }
    StringDelegate { aspect: root.aspects.Description }
}
