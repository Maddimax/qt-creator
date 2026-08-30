// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// Which run configuration to run the tests with, when it could not be worked
// out. The three fields under the box describe whatever is picked, so the
// reader can tell two similarly named configurations apart.
AspectPage {
    id: root

    TextDisplayDelegate { aspect: root.aspects.Details }
    SelectionDelegate { aspect: root.aspects.RunConfiguration }
    BoolWithOwnLabelDelegate { aspect: root.aspects.Remember }

    StringDelegate { aspect: root.aspects.Executable }
    StringDelegate { aspect: root.aspects.Arguments }
    StringDelegate { aspect: root.aspects.WorkingDirectory }
}
