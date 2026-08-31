// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// What several breakpoints have in common: when to stop, how many hits to
// skip, and which thread. Whether the engine can stop conditionally is the
// aspect's business, so the condition row is listed and may not be drawn.
AspectPage {
    id: root

    StringDelegate { aspect: root.aspects.Condition }
    IntegerDelegate { aspect: root.aspects.IgnoreCount }
    StringDelegate { aspect: root.aspects.ThreadSpec }
}
