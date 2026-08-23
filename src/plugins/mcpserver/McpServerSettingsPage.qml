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

    // The address is a container of a choice and a custom value.
    readonly property var listen: AspectModels.named(aspects.ListenAddress)

    BoolDelegate { aspect: aspects.Enabled }

    RowLayout {
        SelectionDelegate { aspect: root.listen.AddressType }
        StringDelegate { aspect: root.listen.CustomAddress }
    }

    IntegerDelegate { aspect: aspects.Port }
    BoolDelegate { aspect: aspects.EnableCors }
    TextDisplayDelegate { aspect: aspects.ServerStatus }

    AspectGroupBox {
        title: qsTr("Tools")

        // One check box per registered tool, named and described by the tool
        // itself. The widget editor drew this as a three-column table.
        AspectItems { model: AspectModels.container(aspects.EnabledTools) }

        RowLayout {
            ButtonDelegate { aspect: aspects.ExportTools }
            ButtonDelegate { aspect: aspects.ImportTools }
        }
    }
}
