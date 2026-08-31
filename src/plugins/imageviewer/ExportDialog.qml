// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// Writing the image out: where to, and how big. The two sides follow each
// other, so they sit on one row with the way back to the image's own size
// beside them.
AspectPage {
    id: root

    StringDelegate { aspect: root.aspects.File }

    RowLayout {
        spacing: Spacing.GapHS
        Layout.fillWidth: true

        IntegerDelegate { aspect: root.aspects.Width }
        IntegerDelegate { aspect: root.aspects.Height }
        ButtonDelegate { aspect: root.aspects.Reset }

        Item { Layout.fillWidth: true }
    }
}
