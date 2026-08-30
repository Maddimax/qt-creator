// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// Where the debugger keeps the symbols it downloads. One folder, which need
// not exist yet - the dialog makes it when it is accepted.
AspectPage {
    id: root

    StringDelegate { aspect: root.aspects.Path }
}
