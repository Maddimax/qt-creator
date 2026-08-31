// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// Asking for a keystore or certificate password. Whether the warning is there
// is the aspect's business - it appears once a password has been refused and
// goes again as soon as the next one is typed.
AspectPage {
    id: root

    TextDisplayDelegate { aspect: root.aspects.Prompt }
    StringDelegate { aspect: root.aspects.Password }
    TextDisplayDelegate { aspect: root.aspects.Warning }
}
