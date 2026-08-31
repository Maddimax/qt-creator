// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// A new virtual device. The ABI and the API go together - which images there
// are depends on the architecture - and so do the kind of device and the skin.
AspectPage {
    id: root

    StringDelegate { aspect: root.aspects.Name }

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true

        SelectionDelegate {
            aspect: root.aspects.Abi
            Layout.fillWidth: true
        }

        SelectionDelegate {
            aspect: root.aspects.TargetApi
            Layout.fillWidth: true
        }
    }

    TextDisplayDelegate { aspect: root.aspects.Warning }

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true

        SelectionDelegate {
            aspect: root.aspects.DeviceType
            Layout.fillWidth: true
        }

        SelectionDelegate {
            aspect: root.aspects.DeviceDefinition
            Layout.fillWidth: true
        }
    }

    IntegerDelegate { aspect: root.aspects.SdcardSize }
    BoolWithOwnLabelDelegate { aspect: root.aspects.Overwrite }
}
