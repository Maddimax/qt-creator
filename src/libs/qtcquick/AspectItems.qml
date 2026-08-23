// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models
import QtCreator.Ui

// The aspects of one AspectContainerModel, one delegate each. Split out of
// AspectForm so that GroupDelegate can reuse it for a nested container, which
// is what makes the nesting recursive.
ColumnLayout {
    id: root

    // An AspectContainerModel, set from C++ or by GroupDelegate.
    required property var model

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
                roleValue: AspectContainerModel.StringList
                StringListDelegate {}
            }
            DelegateChoice {
                roleValue: AspectContainerModel.StringListEditor
                StringListEditorDelegate {}
            }
            DelegateChoice {
                roleValue: AspectContainerModel.Invisible
                Item {}
            }
            DelegateChoice {
                roleValue: AspectContainerModel.FilePathList
                FilePathListDelegate {}
            }
            DelegateChoice {
                roleValue: AspectContainerModel.MultiSelection
                MultiSelectionDelegate {}
            }
            DelegateChoice {
                roleValue: AspectContainerModel.Color
                ColorDelegate {}
            }
            DelegateChoice {
                roleValue: AspectContainerModel.FontFamily
                FontFamilyDelegate {}
            }
            DelegateChoice {
                roleValue: AspectContainerModel.TextDisplay
                TextDisplayDelegate {}
            }
            DelegateChoice {
                roleValue: AspectContainerModel.Container
                GroupDelegate {}
            }
            DelegateChoice { UnsupportedDelegate {} }
        }
    }
}
