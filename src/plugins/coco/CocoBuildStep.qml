// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

AspectPage {
    id: root

    // One button, whose text says which way it will go. What it says and
    // whether it can be pressed is the step's business.
    ButtonDelegate { aspect: root.aspects.ToggleCoverage }
}
