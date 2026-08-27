// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick.Layouts
import QtCreator.Ui

// What a document adds up to: three things about the file, then a count of
// every kind of tag in it.
AspectPage {
    id: root

    contentFillsHeight: true

    TextDisplayDelegate { aspect: root.aspects.FileName }
    TextDisplayDelegate { aspect: root.aspects.Time }
    TextDisplayDelegate { aspect: root.aspects.Levels }

    TreeDelegate {
        aspect: root.aspects.Counts
        Layout.fillHeight: true
    }
}
