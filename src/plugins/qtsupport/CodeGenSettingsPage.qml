// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    AspectGroupBox {
        title: qsTr("Embedding of the UI Class")

        RadioGroupDelegate { aspect: aspects.Embedding }
    }

    AspectGroupBox {
        title: qsTr("Code Generation")

        ColumnLayout {
            BoolDelegate { aspect: aspects.RetranslationSupport }
            BoolDelegate { aspect: aspects.IncludeQtModule }
            BoolDelegate { aspect: aspects.AddQtVersionCheck }
        }
    }
}
