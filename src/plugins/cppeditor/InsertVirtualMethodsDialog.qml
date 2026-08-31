// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// Which of a base class's virtual functions to reimplement, and how to write
// them out. The tree is checkable down to the function; a class's box says
// what its functions say.
AspectPage {
    id: root

    contentFillsHeight: true

    AspectGroupBox {
        title: qsTr("Functions to insert:")
        Layout.fillWidth: true
        Layout.fillHeight: true

        TreeDelegate {
            id: functions

            objectName: "virtualFunctionTree"
            aspect: root.aspects.Functions
            Layout.fillWidth: true
            Layout.fillHeight: true

        }

        // Not a BoolDelegate: which branches are open has to be read before
        // the tree is refiltered and put back afterwards, and the aspect is
        // what does the refiltering.
        CheckBox {
            objectName: "hideReimplemented"
            text: root.aspects.HideReimplemented.plainLabelText
            checked: root.aspects.HideReimplemented.value === true
            enabled: root.aspects.HideReimplemented.enabled
            visible: root.aspects.HideReimplemented.visible

            onToggled: {
                const branches = functions.branchState()
                root.aspects.HideReimplemented.value = checked
                functions.setBranchState(branches)
            }
        }
    }

    AspectGroupBox {
        title: qsTr("Insertion options:")
        Layout.fillWidth: true

        SelectionDelegate { aspect: root.aspects.InsertMode }
        BoolDelegate { aspect: root.aspects.VirtualKeyword }

        RowLayout {
            spacing: Spacing.GapHXs
            Layout.fillWidth: true

            BoolDelegate { aspect: root.aspects.UseOverrideReplacement }

            SelectionDelegate {
                aspect: root.aspects.OverrideReplacement
                compact: true
                Layout.fillWidth: true
            }

            ButtonDelegate { aspect: root.aspects.ClearAddedReplacements }
        }
    }
}
