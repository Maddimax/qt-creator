// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// The web searches this filter offers. They are tried in the order they are
// listed, so the order is the reader's to set.
AspectPage {
    id: root

    contentFillsHeight: true

    StringDelegate { aspect: root.aspects.Name }
    StringListEditorDelegate { aspect: root.aspects.Urls }

    LocatorFilterPrefixRow { aspects: root.aspects }
}
