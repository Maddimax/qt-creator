// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// Naming a new branch or tag. Which of the three below it shows depends on
// what is being named and on what it would track, so all four are listed and
// the container decides.
AspectPage {
    id: root

    StringDelegate { aspect: root.aspects.Name }
    BoolWithOwnLabelDelegate { aspect: root.aspects.Checkout }
    BoolWithOwnLabelDelegate { aspect: root.aspects.Tracking }
    StringDelegate { aspect: root.aspects.Annotation }
}
