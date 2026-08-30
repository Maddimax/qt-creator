// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// A remote to add: what to call it and where it is.
AspectPage {
    id: root

    StringDelegate { aspect: root.aspects.Name }
    StringDelegate { aspect: root.aspects.Url }
}
