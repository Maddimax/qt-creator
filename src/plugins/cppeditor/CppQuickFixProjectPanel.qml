// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// Quick fixes for one project: the same settings as the global page, the flag
// that decides whether they are the ones used, and the one button that acts on
// the file they are kept in. What that button says and whether it is there at
// all is the container's answer.
AspectPage {
    id: root

    readonly property var settings: AspectModels.named(root.aspects.Settings)

    BoolWithOwnLabelDelegate { aspect: root.aspects.UseGlobalSettings }
    ButtonDelegate { aspect: root.aspects.SettingsFileAction }

    CppQuickFixSettingsForm { aspects: root.settings }
}
