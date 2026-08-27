// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// File naming for one project: the same settings as the global page, and the
// flag that decides whether they are the ones used.
AspectPage {
    id: root

    readonly property var settings: AspectModels.named(root.aspects.Settings)

    BoolWithOwnLabelDelegate { aspect: root.aspects.UseGlobalSettings }

    CppFileSettingsForm { aspects: root.settings }
}
