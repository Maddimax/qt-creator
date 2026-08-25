// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    id: root

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true

        ColumnLayout {
            spacing: Spacing.GapVS
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop

            SelectionDelegate { aspect: root.aspects.Device }

            // Name, type, whether it was detected, and whether it answers.
            GroupDelegate { aspect: root.aspects.General }

            // What the device itself asks - which is nothing for some kinds,
            // and then it reports what it knows about itself instead.
            AspectGroupBox {
                title: root.aspects.TypeSpecific?.plainLabelText ?? ""
                visible: root.aspects.TypeSpecific?.container ?? null

                AspectItems {
                    Layout.fillWidth: true
                    model: root.aspects.TypeSpecific.container
                           ? AspectModels.container(root.aspects.TypeSpecific.container)
                           : null
                }
            }

            Item { Layout.fillHeight: true }
        }

        // Where devices come from is the page's business. Add starts the
        // wizard and offers the kinds that need no wizard; the two below act
        // on the current device, and what follows them is the device's own.
        ColumnLayout {
            spacing: Spacing.GapVXs
            Layout.alignment: Qt.AlignTop

            ButtonDelegate {
                aspect: root.aspects.Add
                Layout.fillWidth: false
            }

            Item { Layout.preferredHeight: Spacing.GapVL }

            ButtonDelegate {
                aspect: root.aspects.Remove
                Layout.fillWidth: false
            }

            ButtonDelegate {
                aspect: root.aspects.SetAsDefault
                Layout.fillWidth: false
            }

            // One button per action the current device offers. Which ones
            // there are changes with the device, so the page names none.
            AspectItems {
                Layout.fillWidth: true
                model: AspectModels.container(root.aspects.DeviceActions)
            }
        }
    }
}
