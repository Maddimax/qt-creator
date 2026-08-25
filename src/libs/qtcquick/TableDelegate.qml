// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// Rows and columns, from the model the aspect hands out. What a cell is - a
// check box, a choice, a field, or text to read - is the model's answer,
// because it is the same answer for a QTableView and it usually depends on the
// row's other cells. See Utils::AspectTable.
RowLayout {
    id: root

    required property Aspect aspect
    // The descriptor, read from the aspect rather than taken as model roles, so
    // that a hand-written page can use this delegate with nothing but the
    // aspect. See AspectModels::presentation().
    property var pres: aspect ? AspectModels.presentation(aspect) : ({})
    readonly property string labelText: aspect?.plainLabelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true
    readonly property bool editable: (aspect?.enabled ?? false) && !(aspect?.readOnly ?? false)

    // The aspect owns the model, so it outlives any one page.
    readonly property var sourceModel: aspect ? AspectModels.tableModel(aspect) : null

    // Which row is selected, in the aspect's own model rather than the filtered
    // one - a page showing a detail of the current row means that row, not the
    // one at that position in whatever is on screen. -1 when nothing is.
    readonly property int currentRow: {
        const index = view.currentIndex
        return index && index.valid ? root.rows.mapToSource(index).row : -1
    }

    // Every selected row, in the aspect's own model, ascending. A page that
    // acts on a selection - export these, remove those - wants all of them and
    // not just the one the keyboard is on.
    readonly property var selectedRows: {
        const rows = []
        const indexes = view.selectionModel.selectedIndexes
        for (let i = 0; i < indexes.length; ++i) {
            const row = root.rows.mapToSource(indexes[i]).row
            if (!rows.includes(row))
                rows.push(row)
        }
        rows.sort((a, b) => a - b)
        return rows
    }

    // Narrowing the rows is the view's business; which rows there are and what
    // they offer is the model's. Always in the chain, so there is one index
    // space whether or not the filter field is shown.
    readonly property TableFilterModel rows: TableFilterModel {
        sourceModel: root.sourceModel
    }

    Connections {
        target: root.aspect
        function onControlConfigurationChanged() {
            root.pres = AspectModels.presentation(root.aspect)
        }
    }

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true
    // A table is the tall thing on a page, so it takes the room the page has
    // rather than sitting in a box the height of a list editor.
    Layout.fillHeight: true

    Label {
        text: root.labelText
        Layout.preferredWidth: Metrics.formLabelWidth
        // An aspect with no label of its own reserves no room for one.
        visible: text !== ""
        elide: Text.ElideRight
        Layout.alignment: Qt.AlignTop
    }

    ColumnLayout {
        id: column

        spacing: Spacing.GapVXs
        Layout.fillWidth: true
        Layout.fillHeight: true

        // Above the table and out of the way of it, the way a search field sits
        // over a list.
        RowLayout {
            spacing: 0
            visible: filter.placeholderText !== ""
            Layout.fillWidth: true

            Item { Layout.fillWidth: true }

            TextField {
                id: filter

                objectName: "tableFilterField"
                placeholderText: root.pres.filterPlaceholderText ?? ""
                enabled: root.aspect?.enabled ?? false
                Layout.preferredWidth: Math.round(column.width / 4)
                onTextChanged: root.rows.setFilterFixedString(text)
            }
        }

        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: Metrics.formListHeight
            // Asked for, rather than however tall the rows happen to add up
            // to: a table with more rows than fit scrolls, and does not push
            // the buttons under it off the bottom of the page.
            Layout.preferredHeight: Metrics.formListHeight

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                HorizontalHeaderView {
                    syncView: view
                    clip: true
                    Layout.fillWidth: true
                }

                TableView {
                    id: view

                    model: root.rows
                    reuseItems: false
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    selectionBehavior: TableView.SelectRows
                    // More than one at a time: the widget tables these replace
                    // used ExtendedSelection, and the pages that act on a
                    // selection were written for it.
                    selectionMode: TableView.ExtendedSelection
                    selectionModel: ItemSelectionModel { model: view.model }
                    ToolTip.text: root.toolTip
                    ToolTip.visible: false
                    // Each column as wide as it needs to be, and the last one
                    // takes what is left - the way the widget table stretched
                    // its last header section. Dividing the width evenly gave a
                    // check box as much room as a description.
                    // Every column as wide as its contents want, up to a cap,
                    // and the last one takes what is left. Without the cap a
                    // long description made its column wider than the table,
                    // which scrolled the centred header label out of the
                    // clipped header - the column looked unnamed.
                    columnWidthProvider: function (column) {
                        const capped = function (i) {
                            return Math.min(view.implicitColumnWidth(i),
                                            Metrics.tableColumnMaxWidth)
                        }
                        if (column < view.columns - 1)
                            return capped(column)
                        let used = 0
                        for (let i = 0; i < view.columns - 1; ++i)
                            used += capped(i)
                        return Math.max(Metrics.lineEditWidth, view.width - used)
                    }
                    // A width the provider already answered for is cached, so
                    // the last column has to be asked again when the table is
                    // resized.
                    onWidthChanged: Qt.callLater(view.forceLayout)
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ScrollBar.vertical: ScrollBar {}

                    delegate: Item {
                        id: cell

                        required property var model

                        readonly property var choices: model.choices ?? []
                        readonly property string cellText: model.display ?? ""
                        // A row the model wants read in its own colours says so.
                        // Unset means the form's, not transparent or black.
                        readonly property var cellForeground: model.foreground ?? undefined
                        readonly property var cellBackground: model.background ?? undefined
                        readonly property var ownFont: model.cellFont ?? undefined
                        // A cell with neither choices nor a pattern can still be
                        // one the model does not want written to, so it says.
                        readonly property bool cellEditable:
                            root.editable && (model.editable ?? true)

                        implicitWidth: Math.max(Metrics.lineEditWidth, editor.implicitWidth)
                        // A wrapping cell only knows how tall it is once it
                        // knows how wide it is, and its width is the column's.
                        // So the row's height follows the laid-out text, and a
                        // long description gets the lines it needs.
                        implicitHeight: Math.max(Metrics.tableRowMinimumHeight,
                                                 editor.implicitHeight)

                        Rectangle {
                            anchors.fill: parent
                            visible: cell.cellBackground !== undefined
                            color: cell.cellBackground ?? "transparent"
                        }

                        Loader {
                            id: editor

                            // As wide as the column and no taller than it needs
                            // to be, so a check box beside a four-line
                            // description sits on the row's first line rather
                            // than floating in the middle of it.
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
                                onToggled: {
                                    cell.model.checkState = checked ? Qt.Checked : Qt.Unchecked
                                }
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
                                // The choice's id, which is what the model
                                // stores; the display text is for reading.
                                onActivated: (index) => {
                                    cell.model.edit = cell.choices[index].id
                                }
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
                                    // Removing a row destroys its field, which
                                    // loses focus, which emits this - with the
                                    // text of the row that is going away and an
                                    // index that now belongs to another row.
                                    // Only a field the user changed has
                                    // anything to say.
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
                                // Line up with the text in an editable cell,
                                // and keep two columns of text apart.
                                leftPadding: Spacing.PaddingHS
                                rightPadding: Spacing.PaddingHS
                                topPadding: Spacing.PaddingVXs
                                bottomPadding: Spacing.PaddingVXs
                                // Wrapped, not elided: a description is worth
                                // reading, and the row grows to hold it. Eliding
                                // as well would pin the implicit height to one
                                // line while the text wrapped underneath it,
                                // which is how rows came to overlap.
                                wrapMode: Text.WordWrap
                                verticalAlignment: Text.AlignTop
                                // What the model says, where it says anything;
                                // otherwise the text, which is worth having
                                // when it is elided.
                                ToolTip.text: cell.model.cellToolTip || cell.cellText
                                ToolTip.visible: cellHover.hovered && ToolTip.text !== ""

                                HoverHandler { id: cellHover }
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            spacing: Spacing.GapHXs
            visible: root.pres.allowAdding || root.pres.allowRemoving

            Button {
                text: qsTr("Add")
                visible: root.pres.allowAdding
                enabled: root.editable
                onClicked: root.sourceModel.insertRows(root.sourceModel.rowCount(), 1)
            }

            Button {
                text: qsTr("Remove")
                visible: root.pres.allowRemoving
                enabled: root.editable && root.selectedRows.length > 0
                // Backwards, so that removing one does not move the next.
                onClicked: {
                    const rows = root.selectedRows
                    for (let i = rows.length - 1; i >= 0; --i)
                        root.sourceModel.removeRows(rows[i], 1)
                }
            }

            Item { Layout.fillWidth: true }
        }
    }
}
