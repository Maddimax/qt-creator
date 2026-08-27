// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// One repository's settings, in the two groups the dialog has always had.
AspectPage {
    id: root

    AspectGroupBox {
        title: qsTr("Repository User")

        StringDelegate { aspect: root.aspects.User }
    }

    AspectGroupBox {
        title: qsTr("Repository Settings")

        StringDelegate { aspect: root.aspects.SslIdentityFile }
        BoolDelegate { aspect: root.aspects.DisableAutosync }
    }
}
