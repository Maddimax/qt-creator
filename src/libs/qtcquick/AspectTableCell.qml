// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
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
    // Whether the view says this cell is in the row the user is on.
    property bool highlighted: false
    // What a cell reads in when its model asks for nothing of its own. A list
    // whose entries are columns lined up with spaces is drawn fixed-width.
    property font defaultFont: Fonts.body2

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

    // Over the row's own colours: a page that shows the details of the
    // current row, or a Remove that acts on it, is talking about a row the
    // user has to be able to pick out. A tree's cell is drawn inside a
    // TreeViewDelegate, which draws its own.
    Rectangle {
        objectName: "tableCellHighlight"
        anchors.fill: parent
        visible: cell.highlighted
        color: Tokens.accentSubtle
    }

    RowLayout {
        // As wide as the column and no taller than it needs to be, so a check
        // box beside a four-line description sits on the row's first line
        // rather than floating in the middle of it.
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: Spacing.GapHXs

        // What the model put in Qt::DecorationRole, where it put anything.
        // Takes no room at all otherwise, which is what keeps every table that
        // has no icons looking as it did.
        Image {
            objectName: "tableCellIcon"
            source: AspectModels.decorationUrl(cell.model.decoration ?? undefined)
            visible: source != ""
            fillMode: Image.PreserveAspectFit
            sourceSize.width: Metrics.listRowIconSize
            sourceSize.height: Metrics.listRowIconSize
            Layout.preferredWidth: visible ? Metrics.listRowIconSize : 0
            Layout.preferredHeight: Metrics.listRowIconSize
            Layout.alignment: Qt.AlignTop
        }

        Loader {
            id: editor

            Layout.fillWidth: true
            sourceComponent: {
                if (cell.model.checkable ?? false)
                    return check
                if (cell.choices.length > 0)
                    return chooser
                return cell.cellEditable ? plain : readOnly
            }
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
            font: cell.ownFont ?? cell.defaultFont
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
