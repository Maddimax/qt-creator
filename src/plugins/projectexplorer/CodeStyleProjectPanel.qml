// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui
import QtCreator.TextEditor

// Code style for one project. Which languages there are is not known here, so
// the forms are repeated over the container rather than named; the one for the
// language that is current is the only one visible, which the container says.
AspectPage {
    id: root

    contentFillsHeight: true

    // Typed, not var: a name no aspect answers to is undefined, and undefined
    // reaches AspectModels.container() as a null model - a Repeater with
    // nothing in it and not one word of complaint. Declared as an Aspect, the
    // same mistake fails to assign.
    readonly property Aspect forms: aspects.Forms

    TextDisplayDelegate { aspect: aspects.GlobalLink }
    SelectionDelegate { aspect: aspects.Language }

    Repeater {
        model: AspectModels.container(root.forms)

        delegate: CodeStyleProjectForm {
            required property var aspect

            aspects: AspectModels.named(aspect)
            visible: aspect.visible
            Layout.fillHeight: visible
        }
    }
}
