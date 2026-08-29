// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

AspectPage {
    id: root

    StringDelegate { aspect: root.aspects.binary }
    BoolDelegate { aspect: root.aspects.developMode }
    BoolDelegate { aspect: root.aspects.showSource }
    BoolDelegate { aspect: root.aspects.showNode }
    ButtonDelegate { aspect: root.aspects.update }
}
