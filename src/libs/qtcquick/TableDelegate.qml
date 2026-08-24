// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// Rows and columns, from the model the aspect hands out. What a cell offers -
// a list of choices, or a pattern its text has to match - is the model's
// answer, because it is the same answer for a QTableView and it usually
// depends on the row's other cells. See Utils::AspectTable.
RowLayout {
    id: root

    required property Aspect aspect
    // The descriptor, read from the aspect rather than taken as model roles, so
    // that a hand-written page can use this delegate with nothing but the
    // aspect. See AspectModels::presentation().
    property var pres: aspect ? AspectModels.presentation(aspect) : ({})
    readonly property string labelText: aspect?.labelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true
    readonly property bool editable: (aspect?.enabled ?? false) && !(aspect?.readOnly ?? false)

    // The aspect owns the model, so it outlives any one page.
    readonly property var tableModel: aspect?.tableModel() ?? null

    Connections {
        target: root.aspect
        function onControlConfigurationChanged() {
            root.pres = AspectModels.presentation(root.aspect)
        }
    }

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    Label {
        text: root.labelText
        Layout.preferredWidth: Metrics.formLabelWidth
        elide: Text.ElideRight
        Layout.alignment: Qt.AlignTop
    }

    ColumnLayout {
        spacing: Spacing.GapVXs
        Layout.fillWidth: true

        Frame {
            Layout.fillWidth: true
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

                    model: root.tableModel
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    selectionBehavior: TableView.SelectRows
                    selectionModel: ItemSelectionModel { model: view.model }
                    ToolTip.text: root.toolTip
                    ToolTip.visible: false
                    // Fill the width even when the contents do not need it, the
                    // way the widget table's stretched header sections do.
                    columnWidthProvider: function (column) {
                        return Math.max(view.implicitColumnWidth(column),
                                        view.width / Math.max(1, view.columns))
                    }
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ScrollBar.vertical: ScrollBar {}

                    delegate: Item {
                        id: cell

                        required property var model

                        readonly property var choices: model.choices ?? []
                        readonly property string cellText: model.display ?? ""
                        // A cell with neither choices nor a pattern can still be
                        // one the model does not want written to, so it says.
                        readonly property bool cellEditable:
                            root.editable && (model.editable ?? true)

                        implicitWidth: Math.max(Metrics.lineEditWidth, editor.implicitWidth)
                        implicitHeight: editor.implicitHeight

                        Loader {
                            id: editor

                            anchors.fill: parent
                            sourceComponent: cell.choices.length > 0 ? chooser : plain
                        }

                        Component {
                            id: chooser

                            ComboBox {
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
                                text: cell.cellText
                                enabled: cell.cellEditable
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
                    }
                }
            }
        }

        RowLayout {
            spacing: Spacing.GapHXs

            Button {
                text: qsTr("Add")
                visible: root.pres.allowAdding
                enabled: root.editable
                onClicked: root.tableModel.insertRows(root.tableModel.rowCount(), 1)
            }

            Button {
                text: qsTr("Remove")
                visible: root.pres.allowRemoving
                enabled: root.editable && view.currentRow >= 0
                onClicked: root.tableModel.removeRows(view.currentRow, 1)
            }

            Item { Layout.fillWidth: true }
        }
    }
}
