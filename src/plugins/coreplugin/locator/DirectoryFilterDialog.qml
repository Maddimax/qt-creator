// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// Where a custom filter looks and what it looks for. A filter someone else
// registered shows only the patterns and the prefix: its name and its
// directories are not the reader's.
AspectPage {
    id: root

    contentFillsHeight: true

    StringDelegate { aspect: root.aspects.Name }
    StringListEditorDelegate { aspect: root.aspects.Directories }
    StringDelegate { aspect: root.aspects.FilePattern }
    StringDelegate { aspect: root.aspects.ExclusionPattern }

    LocatorFilterPrefixRow { aspects: root.aspects }
}
