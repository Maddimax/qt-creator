// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// One path mapping: which dashboard project it is for, and where the analysed
// files are on this machine. Shown on its own in the dialog that asks for a
// missing mapping; on the settings page the list draws its items itself.
AspectPage {
    id: root

    StringDelegate { aspect: root.aspects.ProjectName }
    StringDelegate { aspect: root.aspects.AnalysisPath }
    StringDelegate { aspect: root.aspects.LocalPath }
}
