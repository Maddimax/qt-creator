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
        title: qsTr("Automatic Formatting on File Save")
        checkAspect: aspects.autoFormatOnSave

        ColumnLayout {
            SelectionDelegate { aspect: aspects.autoFormatTool }
            StringDelegate { aspect: aspects.autoFormatMime }
            BoolDelegate { aspect: aspects.autoFormatOnlyCurrentProject }
        }
    }
}
