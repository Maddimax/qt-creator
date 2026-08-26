// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// Copilot's page in a project's settings. Whether the project setting below can
// be changed at all is the container's answer, not this form's.
AspectPage {
    BoolWithOwnLabelDelegate { aspect: aspects.UseGlobalSettings }
    BoolDelegate { aspect: aspects.EnableCopilot }
}
