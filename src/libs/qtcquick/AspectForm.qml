// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models
import QtCreator.Ui

ScrollView {
    id: root

    // An AspectContainerModel, set from C++.
    required property var model

    contentWidth: availableWidth

    ColumnLayout {
        width: root.availableWidth
        spacing: Spacing.GapVS

        Repeater {
            model: root.model

            delegate: DelegateChooser {
                role: "kind"

                DelegateChoice {
                    roleValue: AspectContainerModel.Bool
                    BoolDelegate {}
                }
                DelegateChoice {
                    roleValue: AspectContainerModel.String
                    StringDelegate {}
                }
                DelegateChoice {
                    roleValue: AspectContainerModel.FilePath
                    StringDelegate {}
                }
                DelegateChoice {
                    roleValue: AspectContainerModel.Integer
                    IntegerDelegate {}
                }
                DelegateChoice {
                    roleValue: AspectContainerModel.Double
                    DoubleDelegate {}
                }
                DelegateChoice {
                    roleValue: AspectContainerModel.Selection
                    SelectionDelegate {}
                }
                DelegateChoice {
                    roleValue: AspectContainerModel.TextDisplay
                    TextDisplayDelegate {}
                }
                DelegateChoice { UnsupportedDelegate {} }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
