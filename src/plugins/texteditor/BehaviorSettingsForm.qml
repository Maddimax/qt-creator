// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// What the mouse and the keyboard do. The global page and a project's Editor panel show
// the same things and differ only in whose container they are shown for.
AspectGroupBox {
    id: root

    // A NamedAspects for whichever container is being shown.
    required property var aspects

    title: qsTr("Mouse and Keyboard")

    ColumnLayout {
        BoolDelegate { aspect: root.aspects.MouseHiding }
        BoolDelegate { aspect: root.aspects.MouseNavigation }
        BoolDelegate { aspect: root.aspects.ScrollWheelZooming }
        BoolDelegate { aspect: root.aspects.CamelCaseNavigation }
        BoolDelegate { aspect: root.aspects.SmartSelectionChanging }
        BoolDelegate { aspect: root.aspects.KeyboardTooltips }
        SelectionDelegate { aspect: root.aspects.ConstrainTooltips }
    }
}
