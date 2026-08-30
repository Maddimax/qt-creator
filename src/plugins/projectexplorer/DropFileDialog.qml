// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// What to do with files dragged from one project node to another. Which
// choices there are, and what they are called, is the container's answer -
// without somewhere to put files, only the reference-only ones are offered.
AspectPage {
    id: root

    TextDisplayDelegate { aspect: root.aspects.Question }
    RadioGroupDelegate { aspect: root.aspects.Action }
    StringDelegate { aspect: root.aspects.TargetDir }
}
