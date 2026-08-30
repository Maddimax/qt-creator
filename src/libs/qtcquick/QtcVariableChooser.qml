// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// The variables a field may be written in terms of: a filter, the groups a
// macro expander offers, and what the selected one is for. The Qt Quick
// counterpart of Utils::VariableChooser.
//
// The field says nothing about itself here beyond handing over its model;
// where the chosen text goes - replacing the selection, at the cursor - is
// the field's business.
Popup {
    id: root

    // What AspectModels.variables() handed out: the tree, behind a filter
    // proxy this narrows by calling setFilterFixedString().
    required property var variables

    // The text to write. Choosing a variable offers "%{Foo}"; the menu also
    // offers what it stands for right now.
    signal chose(string text)

    function offer(): void {
        filter.text = ""
        description.text = root.defaultDescription
        root.open()
    }

    readonly property string defaultDescription: qsTr("Select a variable to insert.")

    // What the list is pointing at, kept by the row that becomes current: a
    // Quick view has no current *item*, only a current index, and reading a
    // model by role number is not something QML can do.
    property string currentText: ""
    property bool currentChoosable: false

    // A group heading cannot be written into a field, and neither can the
    // variable being defined.
    function chooseCurrent(): void {
        if (!root.currentChoosable)
            return
        root.chose(root.currentText)
        root.close()
    }

    function moveCurrent(delta: int): void {
        if (view.rows === 0)
            return
        const next = Math.max(0, Math.min(view.rows - 1, view.currentRow + delta))
        view.selectionModel.setCurrentIndex(view.index(next, 0),
                                            ItemSelectionModel.ClearAndSelect)
        view.positionViewAtRow(next, TableView.Contain)
    }

    objectName: "variableChooser"
    padding: Spacing.PaddingHS
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
    // Keys have to reach it for Escape to close it and for the list to be
    // walked, and the filter is where the widget leaves the cursor.
    focus: true
    onOpened: filter.forceActiveFocus()

    implicitWidth: Metrics.formLabelWidth + Metrics.formControlWidth
    implicitHeight: Metrics.chooserListHeight + description.implicitHeight
                    + filter.implicitHeight + 2 * padding

    contentItem: ColumnLayout {
        spacing: Spacing.GapVXs

        QtcSearchBox {
            id: filter

            objectName: "variableFilter"
            iconLeading: true
            Layout.fillWidth: true
            // Recursive on the proxy, so a group stays for as long as one of
            // its variables matches.
            onTextChanged: {
                root.variables.setFilterFixedString(text)
                view.expandRecursively()
            }

            // A filter that narrows to one variable and then makes you reach
            // for the mouse is half a filter.
            Keys.onDownPressed: root.moveCurrent(1)
            Keys.onUpPressed: root.moveCurrent(-1)
            Keys.onReturnPressed: root.chooseCurrent()
            Keys.onEnterPressed: root.chooseCurrent()
        }

        TreeView {
            id: view

            objectName: "variableTree"
            model: root.variables
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            selectionBehavior: TableView.SelectRows
            selectionMode: TableView.SingleSelection
            selectionModel: ItemSelectionModel { model: view.model }
            Layout.fillWidth: true
            Layout.fillHeight: true

            ScrollBar.vertical: ScrollBar {}

            // Choosing is a double click, as in the widget: a single one
            // selects, which is what fills the description below. It belongs
            // to the view rather than to the row - the view handles the press
            // itself, so a row never sees a second one - and it acts on
            // whatever the first click made current.
            Keys.onReturnPressed: root.chooseCurrent()
            Keys.onEnterPressed: root.chooseCurrent()

            // The widget tree shows every group open; a variable one level
            // down behind a closed handle is a variable nobody finds. Rows
            // arrive a frame after the model is set and again after each
            // expansion, so this is where expanding belongs - doing it when
            // the model is assigned finds nothing to expand.
            onRowsChanged: Qt.callLater(view.expandRecursively)

            delegate: TreeViewDelegate {
                id: row

                objectName: "variableRow"

                // Only the roles: TreeViewDelegate already requires index,
                // row and model, and redeclaring one of those shadows the
                // base's, which leaves the delegate unable to incubate.
                required property string unexpandedText
                required property string expandedText
                required property string currentValue
                required property bool selectable

                // A group is a heading, not something to insert - it has no
                // text of its own. The variable being edited is listed and
                // refused, which is what selectable says.
                readonly property bool choosable: row.unexpandedText !== "" && row.selectable

                implicitHeight: Math.max(Metrics.tableRowMinimumHeight,
                                         implicitContentHeight)
                enabled: row.unexpandedText === "" || row.selectable

                onCurrentChanged: {
                    if (!current)
                        return
                    description.text = row.currentValue !== "" ? row.currentValue
                                                               : root.defaultDescription
                    root.currentText = row.unexpandedText
                    root.currentChoosable = row.choosable
                }

                // Choosing is a double click, as in the widget: a single one
                // selects, which is what fills the description below.
                onDoubleClicked: root.chooseCurrent()

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    enabled: row.choosable
                    onTapped: menu.popup()
                }

                Menu {
                    id: menu

                    objectName: "variableMenu"

                    MenuItem {
                        objectName: "insertUnexpanded"
                        text: qsTr("Insert \"%1\"").arg(row.unexpandedText)
                        onTriggered: {
                            root.chose(row.unexpandedText)
                            root.close()
                        }
                    }

                    MenuItem {
                        objectName: "insertExpanded"
                        text: row.expandedText !== "" ? qsTr("Insert \"%1\"").arg(row.expandedText)
                                                      : qsTr("Insert Expanded Value")
                        onTriggered: {
                            root.chose(row.expandedText)
                            root.close()
                        }
                    }
                }
            }
        }

        // Rich text, and what the variable stands for is often long: the
        // widget gives it a fixed floor and wraps.
        QtcLabel {
            id: description

            objectName: "variableDescription"
            text: root.defaultDescription
            textFormat: Text.RichText
            wrapMode: Text.WordWrap
            verticalAlignment: Text.AlignTop
            Layout.fillWidth: true
            Layout.minimumHeight: Metrics.variableDescriptionHeight
        }
    }
}
