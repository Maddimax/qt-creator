// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// One of the two things a filter picks - everything the registered
// documentation offers, with the current filter's own choice ticked.
ColumnLayout {
    id: root

    required property string title
    // The aspect's model for this list, or null while there is no filter to
    // show the options of.
    required property var options

    spacing: Spacing.GapVXs

    QtcLabel {
        text: root.title
        Layout.fillWidth: true
    }

    Frame {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: Metrics.formListHeight
        Layout.preferredHeight: Metrics.formListHeight

        ListView {
            id: view

            anchors.fill: parent
            clip: true
            model: root.options
            // Nothing is registered, or nothing is selected: an empty frame
            // says that better than a list with no rows in it would.
            enabled: root.options !== null

            ScrollBar.vertical: ScrollBar {}

            delegate: CheckBox {
                id: option

                required property int index
                required property string optionText
                required property int optionChecked

                text: option.optionText
                checked: option.optionChecked === Qt.Checked
                width: view.width

                onToggled: root.options.toggle(option.index, option.checked)
            }
        }
    }
}
