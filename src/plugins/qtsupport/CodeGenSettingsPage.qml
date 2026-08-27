// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    id: root

    AspectGroupBox {
        title: qsTr("Embedding of the UI Class")

        RadioGroupDelegate { aspect: root.aspects.Embedding }
    }

    AspectGroupBox {
        title: qsTr("Code Generation")

        ColumnLayout {
            BoolDelegate { aspect: root.aspects.RetranslationSupport }
            BoolDelegate { aspect: root.aspects.IncludeQtModule }
            BoolDelegate { aspect: root.aspects.AddQtVersionCheck }
        }
    }
}
