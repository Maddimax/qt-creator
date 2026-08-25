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

    // The plugin's own settings, shown here rather than owned: see
    // Utils::ContainerAspect.
    readonly property var suite: AspectModels.named(aspects.Suite.container)

    AspectGroupBox {
        title: qsTr("Dashboard Servers")

        RowLayout {
            spacing: Spacing.GapHM

            SelectionDelegate { aspect: root.aspects.Server }

            // What a server is is a dialog's business; which one is the
            // default is this page's.
            ColumnLayout {
                spacing: Spacing.GapVXs
                Layout.alignment: Qt.AlignTop

                ButtonDelegate { aspect: root.aspects.AddServer; Layout.fillWidth: false }
                ButtonDelegate { aspect: root.aspects.EditServer; Layout.fillWidth: false }
                ButtonDelegate { aspect: root.aspects.RemoveServer; Layout.fillWidth: false }
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Path Mapping")
        Layout.fillHeight: true

        AspectListDelegate { aspect: root.aspects.Mappings }
    }

    AspectGroupBox {
        title: qsTr("Local Analyses")

        ColumnLayout {
            spacing: Spacing.GapVS

            StringDelegate { aspect: root.suite.SuitePath }
            BoolDelegate { aspect: root.suite.SaveOpenFiles }
            TextDisplayDelegate { aspect: root.aspects.Version }
            TextDisplayDelegate { aspect: root.aspects.BuildDate }
            TextDisplayDelegate { aspect: root.aspects.Support }
            StringDelegate { aspect: root.suite.BauhausPython }
            StringDelegate { aspect: root.suite.JavaHome }
        }
    }

    AspectGroupBox {
        title: qsTr("Misc Options")

        BoolDelegate { aspect: root.suite.HighlightMarks }
    }
}
