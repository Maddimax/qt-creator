// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
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

            BoolDelegate { aspect: root.aspects.UseOtherFiles }

            RowLayout {
                spacing: Spacing.GapHM
                Layout.fillWidth: true

                BoolDelegate {
                    aspect: root.aspects.UseSpecificConfigFile
                    Layout.fillWidth: false
                }

                StringDelegate {
                    aspect: root.aspects.SpecificConfigFile
                    enabled: root.aspects.UseSpecificConfigFile.value
                }
            }

            BoolDelegate { aspect: root.aspects.UseHomeFile }
            BoolDelegate { aspect: root.aspects.UseCustomStyle }
            BoolDelegate { aspect: root.aspects.FormatEntireFileFallback }

            BeautifierConfigurations {
                configurations: root.configurations
                enabled: root.aspects.UseCustomStyle.value
                Layout.fillHeight: true
            }
        }
    }
}
