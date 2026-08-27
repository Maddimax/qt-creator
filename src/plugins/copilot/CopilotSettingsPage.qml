// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    id: root

    AspectGroupBox {
        title: qsTr("Note")

        ColumnLayout {
            TextDisplayDelegate { aspect: root.aspects.Warning }
            TextDisplayDelegate { aspect: root.aspects.Help }
        }
    }

    ButtonDelegate { aspect: root.aspects.SignIn }
    TextDisplayDelegate { aspect: root.aspects.AuthStatus }

    BoolDelegate { aspect: root.aspects.EnableCopilot }
    StringDelegate { aspect: root.aspects.NodeJsPath }
    StringDelegate { aspect: root.aspects.DistPath }
    BoolDelegate { aspect: root.aspects.Autocomplete }
    StringDelegate { aspect: root.aspects.GithubEnterpriseUrl }

    StringDelegate { aspect: root.aspects.Proxy }
    BoolDelegate { aspect: root.aspects.ProxyRejectUnauthorized }
}
