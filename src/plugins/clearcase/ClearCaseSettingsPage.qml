// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

AspectPage {
    id: root

    GroupDelegate { aspect: root.aspects.Configuration }

    // Which diff to use, and - for the one that takes them - its arguments.
    // The warning below says why External may not be on offer.
    GroupDelegate { aspect: root.aspects.Diff }

    GroupDelegate { aspect: root.aspects.Misc }
}
