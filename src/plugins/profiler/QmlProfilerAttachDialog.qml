// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// Attaching to an application that is already running with QML debugging on.
// The hint comes first because it says what has to have been done to the
// application before any of this is any use.
AspectPage {
    id: root

    TextDisplayDelegate { aspect: root.aspects.Hint }
    InlineGroupDelegate { aspect: root.aspects.Kit }
    IntegerDelegate { aspect: root.aspects.Port }
}
