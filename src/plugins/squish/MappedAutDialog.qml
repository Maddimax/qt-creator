// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// Which application to record against, and what to start it with. The list
// arrives from the Squish server while this is open, so it grows under the
// reader - the first entry stays "none" and nothing is picked for them.
AspectPage {
    id: root

    SelectionDelegate { aspect: root.aspects.Application }
    StringDelegate { aspect: root.aspects.Arguments }
}
