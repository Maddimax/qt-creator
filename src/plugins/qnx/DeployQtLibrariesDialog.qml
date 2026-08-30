// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// Which Qt to put on the device and where. The log below it is the dialog's,
// not this form's.
AspectPage {
    id: root

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true

        SelectionDelegate {
            aspect: root.aspects.Library
            Layout.fillWidth: true
        }

        ButtonDelegate { aspect: root.aspects.Deploy }
    }

    TextDisplayDelegate { aspect: root.aspects.BasePath }
    StringDelegate { aspect: root.aspects.RemoteDirectory }
    ProgressDelegate { aspect: root.aspects.Progress }
}
