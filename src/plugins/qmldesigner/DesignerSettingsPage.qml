// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// Qt Quick Designer's own settings. Grouped the way the designer thinks about
// them rather than the way they are stored, which is why the groups do not
// follow the container's order.
AspectPage {
    id: root

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true

        AspectGroupBox {
            title: qsTr("Snapping")

            ColumnLayout {
                IntegerDelegate { aspect: root.aspects.ContainerPadding }
                IntegerDelegate { aspect: root.aspects.ItemSpacing }
            }
        }

        AspectGroupBox {
            title: qsTr("Canvas")

            ColumnLayout {
                IntegerDelegate { aspect: root.aspects.CanvasWidth }
                IntegerDelegate { aspect: root.aspects.CanvasHeight }
                BoolDelegate { aspect: root.aspects.SmoothRendering }
            }
        }

        AspectGroupBox {
            title: qsTr("Root Component Init Size")

            ColumnLayout {
                IntegerDelegate { aspect: root.aspects.RootElementInitWidth }
                IntegerDelegate { aspect: root.aspects.RootElementInitHeight }
            }
        }

        AspectGroupBox {
            title: qsTr("Styling")

            ColumnLayout {
                RowLayout {
                    spacing: Spacing.GapHM

                    StringDelegate { aspect: root.aspects.ControlsStyle }
                    ButtonDelegate { aspect: root.aspects.ResetStyle; Layout.fillWidth: false }
                }
                SelectionDelegate { aspect: root.aspects.Controls2Style }
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Subcomponents")

        BoolDelegate { aspect: root.aspects.AlwaysSaveInCrumbleBar }
    }

    RowLayout {
        spacing: Spacing.GapHM
        Layout.fillWidth: true

        AspectGroupBox {
            title: qsTr("Warnings")

            ColumnLayout {
                BoolDelegate { aspect: root.aspects.WarnAboutQtQuickFeaturesInDesigner }
                BoolDelegate { aspect: root.aspects.WarnAboutQtQuickDesignerFeaturesInCodeEditor }
                BoolDelegate { aspect: root.aspects.WarnAboutQmlFilesInsteadOfUiQmlFiles }
            }
        }

        AspectGroupBox {
            title: qsTr("Internationalization")

            RadioGroupDelegate { aspect: root.aspects.TypeOfQsTrFunction }
        }
    }

    AspectGroupBox {
        title: qsTr("Features")

        ColumnLayout {
            BoolDelegate { aspect: root.aspects.AlwaysDesignMode }
            BoolDelegate { aspect: root.aspects.ReformatUiQmlFiles }
            BoolDelegate { aspect: root.aspects.AskBeforeDeletingAsset }
            BoolDelegate { aspect: root.aspects.AskBeforeDeletingContentLibFile }
            BoolDelegate { aspect: root.aspects.EnableTimelineView }
            BoolDelegate { aspect: root.aspects.EnableDockWidgetContentMinSize }
        }
    }

    AspectGroupBox {
        title: qsTr("Debugging")

        ColumnLayout {
            BoolDelegate { aspect: root.aspects.ShowDebugSettings }
            BoolDelegate { aspect: root.aspects.ShowPropertyEditorWarnings }
            SelectionDelegate { aspect: root.aspects.ForwardPuppetOutput }
            BoolDelegate { aspect: root.aspects.EnableQtQuickDesignerDebugView }
            BoolDelegate { aspect: root.aspects.WarnException }
            SelectionDelegate { aspect: root.aspects.DebugPuppet }
        }
    }
}
