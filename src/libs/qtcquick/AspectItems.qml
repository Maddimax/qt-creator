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
//
// A GridLayout rather than a ColumnLayout so that the same file serves both
// directions: a container whose aspects read as one value is drawn as a row.
GridLayout {
    id: root

    // An AspectContainerModel, set from C++ or by GroupDelegate.
    required property var model
    // Side by side rather than one under the other. See
    // AspectContainer::setInlineRow() and InlineGroupDelegate.
    property bool inRow: false

    // One column is a column; as many columns as there are aspects is a row.
    columns: inRow ? Math.max(1, repeater.count) : 1
    columnSpacing: Spacing.GapHS
    rowSpacing: Spacing.GapVS

    Repeater {
        id: repeater

        model: root.model

        delegate: DelegateChooser {
            role: "kind"

            DelegateChoice {
                roleValue: AspectContainerModel.Bool
                BoolDelegate {}
            }
            DelegateChoice {
                roleValue: AspectContainerModel.TriStateBool
                TriStateDelegate {}
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
                roleValue: AspectContainerModel.RadioGroup
                RadioGroupDelegate {}
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
                roleValue: AspectContainerModel.AspectList
                AspectListDelegate {}
            }
            DelegateChoice {
                roleValue: AspectContainerModel.AspectInlineList
                AspectInlineListDelegate {}
            }
            DelegateChoice {
                roleValue: AspectContainerModel.TextWithAction
                TextWithActionDelegate {}
            }
            DelegateChoice {
                roleValue: AspectContainerModel.Button
                ButtonDelegate {}
            }
            DelegateChoice {
                roleValue: AspectContainerModel.Radio
                RadioDelegate {}
            }
            DelegateChoice {
                roleValue: AspectContainerModel.Text
                TextAreaDelegate {}
            }
            DelegateChoice {
                roleValue: AspectContainerModel.KeySequence
                KeySequenceDelegate {}
            }
            DelegateChoice {
                roleValue: AspectContainerModel.Secret
                SecretDelegate {}
            }
            DelegateChoice {
                roleValue: AspectContainerModel.Table
                TableDelegate {}
            }
            DelegateChoice {
                roleValue: AspectContainerModel.GroupedList
                GroupedListDelegate {}
            }
            DelegateChoice {
                roleValue: AspectContainerModel.Tree
                TreeDelegate {}
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
            DelegateChoice {
                roleValue: AspectContainerModel.InlineGroup
                InlineGroupDelegate {}
            }
            DelegateChoice {
                roleValue: AspectContainerModel.FlattenedGroup
                FlattenedGroupDelegate {}
            }
            DelegateChoice { UnsupportedDelegate {} }
        }
    }
}
