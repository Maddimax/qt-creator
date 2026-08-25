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

    contentFillsHeight: true

    readonly property var configurations: AspectModels.named(aspects.Configurations)

    AspectGroupBox {
        title: qsTr("Configuration")

        ColumnLayout {
            spacing: Spacing.GapVS

            StringDelegate { aspect: root.aspects.Command }
            StringDelegate { aspect: root.aspects.SupportedMimeTypes }
        }
    }

    AspectGroupBox {
        title: qsTr("Options")
        Layout.fillHeight: true

        ColumnLayout {
            spacing: Spacing.GapVS

            // One of two, and the aspects say which: a bool that is drawn as a
            // radio button is still one value, so the other button reads it
            // rather than grouping itself with the first.
            RadioDelegate { aspect: root.aspects.UsePredefinedStyle }

            SelectionDelegate { aspect: root.aspects.PredefinedStyle }
            SelectionDelegate { aspect: root.aspects.FallbackStyle }

            RadioButton {
                text: qsTr("Use customized style:")
                autoExclusive: false
                enabled: root.aspects.UsePredefinedStyle.enabled
                checked: !root.aspects.UsePredefinedStyle.value
                onToggled: root.aspects.UsePredefinedStyle.value = !checked
            }

            BeautifierConfigurations {
                configurations: root.configurations
                Layout.fillHeight: true
            }
        }
    }
}
