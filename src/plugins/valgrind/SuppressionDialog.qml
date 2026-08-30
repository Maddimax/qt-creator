// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtCreator.Ui

// The suppression rule to append, and where to append it. The rule is read
// line by line against a stack trace, so its columns line up.
AspectPage {
    id: root

    contentFillsHeight: true

    StringDelegate { aspect: root.aspects.File }
    TextAreaDelegate { aspect: root.aspects.Suppression }
}
