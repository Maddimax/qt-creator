// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// Which other projects in the session this one depends on, and two settings
// that are the session's rather than the project's.
AspectPage {
    id: root

    contentFillsHeight: true

    TableDelegate {
        aspect: aspects.Dependencies
        Layout.fillHeight: true
    }

    BoolDelegate { aspect: aspects.CascadeSetActive }
    BoolDelegate { aspect: aspects.DeployDependencies }
}
