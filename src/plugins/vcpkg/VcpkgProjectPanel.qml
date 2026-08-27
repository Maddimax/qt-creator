// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// Vcpkg for one project. The installation below is disabled while the global
// setting is in use; the container says so, not this form.
AspectPage {
    id: root

    readonly property var settings: AspectModels.named(root.aspects.Settings)

    BoolWithOwnLabelDelegate { aspect: root.aspects.UseGlobalSettings }

    AspectGroupBox {
        title: qsTr("Vcpkg installation")

        RowLayout {
            StringDelegate { aspect: root.settings.VcpkgRoot }
            ButtonDelegate { aspect: root.settings.OpenWebsite }
        }
    }
}
