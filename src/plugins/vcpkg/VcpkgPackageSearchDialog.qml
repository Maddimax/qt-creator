// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// The packages that can be added, and what the chosen one says about itself.
// Everything on the right is read from the package, not typed.
AspectPage {
    id: root

    contentFillsHeight: true

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true
        Layout.fillHeight: true

        // The indicator sits over the list, so it needs a parent that is not a
        // layout: anchors inside one are undefined behaviour.
        Item {
            Layout.preferredWidth: Metrics.packageListWidth
            Layout.fillHeight: true

            TableDelegate {
                id: packages

                objectName: "packageTable"
                anchors.fill: parent
                aspect: root.aspects.Packages

                onCurrentRowChanged: root.aspects.Packages.setCurrentRow(packages.currentRow)
            }

            BusyIndicator {
                objectName: "loadingPackages"
                anchors.centerIn: parent
                running: root.aspects.Loading.value ?? false
                visible: running
            }
        }

        AspectGroupBox {
            title: qsTr("Package Details")
            Layout.fillWidth: true
            Layout.fillHeight: true

            TextDisplayDelegate { aspect: root.aspects.Name }
            TextDisplayDelegate { aspect: root.aspects.Version }
            TextDisplayDelegate { aspect: root.aspects.License }
            TextDisplayDelegate { aspect: root.aspects.Description }
            TextDisplayDelegate { aspect: root.aspects.Homepage }
        }
    }

    TextDisplayDelegate { aspect: root.aspects.AlreadyADependency }
}
