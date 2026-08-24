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

    RowLayout {
        ButtonDelegate { aspect: aspects.ExportTools }
        ButtonDelegate { aspect: aspects.ImportTools }
    }

    // One row per registered tool: a check box, the tool's name and what it
    // does. The names live in the model, which is why a plain list of the
    // per-tool aspects was a column of nameless check boxes.
    TableDelegate { aspect: aspects.EnabledTools }
}
