// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtCreator.Ui

// One cell, as the model describes it: a check box, a choice, a field, or text
// to read. Which of those it is is the model's answer and not the view's -
// it is the same answer for a QTableView, and it usually depends on the row's
// other cells. See Utils::AspectTable.
//
// Shared by TableDelegate and TreeDelegate so that a tree's cells can be
// written to the same way a table's can.
Item {
    id: cell

    // The delegate's model row, as a view hands it over.
    required property var model
    // Whether the view lets anything be written at all. A cell the model has
    // closed stays closed either way.
    required property bool editable
    // What a cell that says nothing about itself is. A table's rows are the
    // user's to edit; a tree reports unless its model says otherwise, which is
    // what the EditableRole is for.
    property bool editableByDefault: true

    readonly property var choices: model.choices ?? []
    readonly property string cellText: model.display ?? ""
    // A row the model wants read in its own colours says so. Unset means the
    // form's, not transparent or black.
    readonly property var cellForeground: model.foreground ?? undefined
    readonly property var cellBackground: model.background ?? undefined
    readonly property var ownFont: model.cellFont ?? undefined
    // A cell with neither choices nor a pattern can still be one the model
    // does not want written to, so it says.
    readonly property bool cellEditable:
        cell.editable && (model.editable ?? cell.editableByDefault)

    implicitWidth: Math.max(Metrics.lineEditWidth, editor.implicitWidth)
    // A wrapping cell only knows how tall it is once it knows how wide it is,
    // and its width is the column's. So the row's height follows the laid-out
    // text, and a long description gets the lines it needs.
    implicitHeight: Math.max(Metrics.tableRowMinimumHeight, editor.implicitHeight)

    Rectangle {
        anchors.fill: parent
        visible: cell.cellBackground !== undefined
        color: cell.cellBackground ?? "transparent"
    }

    Loader {
        id: editor

        // As wide as the column and no taller than it needs to be, so a check
        // box beside a four-line description sits on the row's first line
        // rather than floating in the middle of it.
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        sourceComponent: {
            if (cell.model.checkable ?? false)
                return check
            if (cell.choices.length > 0)
                return chooser
            return cell.cellEditable ? plain : readOnly
        }
    }

    Component {
        id: check

        CheckBox {
            objectName: "tableCellCheckBox"
            text: cell.cellText
            checked: cell.model.checkState === Qt.Checked
            enabled: cell.cellEditable
            onToggled: cell.model.checkState = checked ? Qt.Checked : Qt.Unchecked
        }
    }

    Component {
        id: chooser

        ComboBox {
            objectName: "tableCellComboBox"
            textRole: "display"
            model: cell.choices
            enabled: cell.cellEditable
            currentIndex: cell.choices.findIndex((choice) => {
                return choice.display === cell.cellText
            })
            // The choice's id, which is what the model stores; the display
            // text is for reading.
            onActivated: (index) => cell.model.edit = cell.choices[index].id
        }
    }

    Component {
        id: plain

        TextField {
            objectName: "tableCellField"
            text: cell.cellText
            validator: RegularExpressionValidator {
                regularExpression: new RegExp(cell.model.validator || ".*")
            }
            onEditingFinished: {
                // Removing a row destroys its field, which loses focus, which
                // emits this - with the text of the row that is going away and
                // an index that now belongs to another row. Only a field the
                // user changed has anything to say.
                if (text !== cell.cellText)
                    cell.model.edit = text
            }
        }
    }

    Component {
        id: readOnly

        Label {
            objectName: "tableCellLabel"
            text: cell.cellText
            color: cell.cellForeground ?? Tokens.textDefault
            font: cell.ownFont ?? Fonts.body2
            // Line up with the text in an editable cell, and keep two columns
            // of text apart.
            leftPadding: Spacing.PaddingHS
            rightPadding: Spacing.PaddingHS
            topPadding: Spacing.PaddingVXs
            bottomPadding: Spacing.PaddingVXs
            // Wrapped, not elided: a description is worth reading, and the row
            // grows to hold it. Eliding as well would pin the implicit height
            // to one line while the text wrapped underneath it, which is how
            // rows came to overlap.
            wrapMode: Text.WordWrap
            verticalAlignment: Text.AlignTop
            // What the model says, where it says anything; otherwise the text,
            // which is worth having when it is elided.
            ToolTip.text: cell.model.cellToolTip || cell.cellText
            ToolTip.visible: cellHover.hovered && ToolTip.text !== ""

            HoverHandler { id: cellHover }
        }
    }
}
