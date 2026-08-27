// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// Which change to describe. One field, and the range it accepts is the
// aspect's, so nothing here has to validate anything.
AspectPage {
    id: root

    IntegerDelegate { aspect: root.aspects.Number }
}
