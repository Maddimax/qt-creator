// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// The library the custom widgets are packed into. The three collection fields
// are closed where there is only one widget, which is collected into nothing.
AspectPage {
    id: root

    TextDisplayDelegate { aspect: root.aspects.Intro }

    StringDelegate { aspect: root.aspects.CollectionClass }
    StringDelegate { aspect: root.aspects.CollectionHeader }
    StringDelegate { aspect: root.aspects.CollectionSource }
    StringDelegate { aspect: root.aspects.PluginName }
    StringDelegate { aspect: root.aspects.ResourceFile }
}
