// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// Browsing the file system from the locator. The one thing to decide is
// whether what is hidden is offered.
AspectPage {
    id: root

    BoolWithOwnLabelDelegate { aspect: root.aspects.IncludeHidden }

    LocatorFilterPrefixRow { aspects: root.aspects }
}
