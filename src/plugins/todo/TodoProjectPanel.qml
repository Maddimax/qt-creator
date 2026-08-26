// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// To-do scanning for one project.
AspectPage {
    id: root

    readonly property var settings: AspectModels.named(aspects.Settings)

    BoolWithOwnLabelDelegate { aspect: aspects.UseGlobalSettings }

    AspectGroupBox {
        title: qsTr("Excluded Files")
        // The box follows what it holds. The settings are disabled as a whole
        // while the global list is in use, and the widget form kept the box in
        // step by hand because a QGroupBox is not one of the aspects.
        enabled: aspects.Settings?.enabled ?? true

        StringListEditorDelegate { aspect: root.settings.ExcludePatterns }
    }
}
