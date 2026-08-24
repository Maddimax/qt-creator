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
        title: qsTr("When Zen Mode or Distraction Free Mode Is Active")

        ColumnLayout {
            IntegerDelegate { aspect: aspects.EditorContentWidth }

            RowLayout {
                TextDisplayDelegate { aspect: aspects.ModeSelectorNote }
                RadioGroupDelegate { aspect: aspects.ModesBarState }
            }
        }
    }
}
