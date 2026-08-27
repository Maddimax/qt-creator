// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// What key to make and where to put it. Which sizes are on offer follows the
// algorithm, and the public key's path follows the private one; both are the
// aspects' own doing.
AspectPage {
    id: root

    AspectGroupBox {
        title: qsTr("Options")

        RadioGroupDelegate { aspect: root.aspects.Algorithm }
        SelectionDelegate { aspect: root.aspects.KeySize }
        StringDelegate { aspect: root.aspects.PrivateKeyFile }
        TextDisplayDelegate { aspect: root.aspects.PublicKeyFile }
    }
}
