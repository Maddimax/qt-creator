// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// Writing one image out at several sizes. The name says where the width and
// the height go; the sizes are a list, with the ways of filling it in beside
// the field rather than inside it.
AspectPage {
    id: root

    StringDelegate { aspect: root.aspects.File }

    RowLayout {
        spacing: Spacing.GapHS
        Layout.fillWidth: true

        StringDelegate {
            aspect: root.aspects.Sizes
            Layout.fillWidth: true
        }

        ButtonDelegate { aspect: root.aspects.SizeOptions }
    }
}
