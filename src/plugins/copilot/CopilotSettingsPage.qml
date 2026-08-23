// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    AspectGroupBox {
        title: qsTr("Note")

        ColumnLayout {
            TextDisplayDelegate { aspect: aspects.Warning }
            TextDisplayDelegate { aspect: aspects.Help }
        }
    }

    ButtonDelegate { aspect: aspects.SignIn }
    TextDisplayDelegate { aspect: aspects.AuthStatus }

    BoolDelegate { aspect: aspects.EnableCopilot }
    StringDelegate { aspect: aspects.NodeJsPath }
    StringDelegate { aspect: aspects.DistPath }
    BoolDelegate { aspect: aspects.Autocomplete }
    StringDelegate { aspect: aspects.GithubEnterpriseUrl }

    StringDelegate { aspect: aspects.Proxy }
    BoolDelegate { aspect: aspects.ProxyRejectUnauthorized }
}
