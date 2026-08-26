// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// What typing does on its own. The global page and a project's Editor panel show
// the same things and differ only in whose container they are shown for.
AspectGroupBox {
    id: root

    // A NamedAspects for whichever container is being shown.
    required property var aspects

    title: qsTr("Typing")

    ColumnLayout {
        BoolDelegate { aspect: root.aspects.AutoIndent }
        SelectionDelegate { aspect: root.aspects.SmartBackspaceBehavior }
        SelectionDelegate { aspect: root.aspects.TabKeyBehavior }
        BoolDelegate { aspect: root.aspects.PreferSingleLineComments }
        SelectionDelegate { aspect: root.aspects.PreferAfterWhitespaceComments }
    }
}
