// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// clangd for one project: the same settings as the global page, without the
// sessions table and the note about configuration files, which are global.
AspectPage {
    id: root

    readonly property var settings: AspectModels.named(aspects.Settings)

    BoolWithOwnLabelDelegate { aspect: aspects.UseGlobalSettings }

    ClangdSettingsForm {
        settings: root.settings
        versionWarning: root.aspects.VersionWarning
    }
}
