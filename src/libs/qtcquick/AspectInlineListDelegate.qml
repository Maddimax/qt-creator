// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// A list of sub-aspects one under the other, each drawn in full, with a remove
// beside each and an add at the end. The counterpart of AspectList's inline
// style, for a short list whose items are one control each - the key sequences
// a command is mapped to. A long list of named things is AspectListDelegate.
ColumnLayout {
    id: root

    required property Aspect aspect
    // Derived rather than taken as a model role, so that a hand-written page
    // can use this delegate with nothing but the aspect.
    readonly property var itemListModel: aspect ? AspectModels.itemList(aspect) : null
    readonly property string labelText: aspect?.plainLabelText ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true
    readonly property bool editable: (aspect?.enabled ?? false) && !(aspect?.readOnly ?? false)

    visible: aspectVisible
    spacing: Spacing.GapVXs
    Layout.fillWidth: true

    Label {
        text: root.labelText
        visible: root.labelText !== ""
    }

    Repeater {
        model: root.itemListModel

        delegate: RowLayout {
            id: row

            required property int index
            // Carried on the row so that the item's own aspects can be drawn
            // from it, rather than asked for with a role enum QML would have
            // to know.
            required property var itemModel

            spacing: Spacing.GapHXs
            Layout.fillWidth: true

            // Loaded by URL because AspectItems instantiates this delegate,
            // and QML rejects two files that name each other as types. With
            // the model as an initial property: it is required, and a Loader
            // that only assigns it afterwards builds the item without one.
            Loader {
                Layout.fillWidth: true
                // inRow with the model: an item whose aspects read as one
                // value - a port mapping is four fields that mean one thing -
                // is drawn side by side, the way the widget form draws it.
                Component.onCompleted: setSource("AspectItems.qml",
                                                 {"model": row.itemModel,
                                                  "inRow": row.itemModel?.inlineRow ?? false})
            }

            Button {
                objectName: "aspectInlineListRemoveButton"
                text: qsTr("Remove")
                enabled: root.editable
                Layout.alignment: Qt.AlignTop
                onClicked: root.itemListModel.removeItem(row.index)
            }
        }
    }

    RowLayout {
        spacing: Spacing.GapHXs
        Layout.fillWidth: true

        Item { Layout.fillWidth: true }

        Button {
            objectName: "aspectInlineListAddButton"
            text: qsTr("Add")
            enabled: root.editable
            onClicked: root.itemListModel.addItem()
        }
    }
}
