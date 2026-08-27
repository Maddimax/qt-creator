// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// Credentials for a remote repository. Nothing is stored here - the dialog
// hands what was typed back to whoever asked, and it goes no further.
AspectPage {
    id: root

    StringDelegate { aspect: root.aspects.Username }
    // A StringDelegate too: what makes it a password is the aspect's display
    // style, which the delegate reads. SecretDelegate is for a value that
    // lives in the keychain rather than in the aspect.
    StringDelegate { aspect: root.aspects.Password }
}
