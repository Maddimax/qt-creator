// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

RowLayout {
    id: delegate

    required property Aspect aspect
    readonly property string labelText: aspect?.plainLabelText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true

    Connections {
        target: delegate.aspect
        function onControlConfigurationChanged() {
            delegate.pres = AspectModels.presentation(delegate.aspect)
        }
    }
    // The descriptor, read from the aspect rather than taken as model roles, so
    // that a hand-written page can use this delegate with nothing but the
    // aspect. See AspectModels::presentation().
    // Not readonly: an aspect can change what it wants drawn - the label on
    // Copilot's sign-in button is its state - and says so with
    // controlConfigurationChanged().
    property var pres: aspect ? AspectModels.presentation(aspect) : ({})

    // The value is stored scaled up: a timeout is kept in milliseconds and
    // shown in seconds. Never zero, so it is always safe to divide by.
    readonly property int scale: Math.max(1, delegate.pres.displayScaleFactor ?? 1)
    readonly property int base: delegate.pres.displayIntegerBase ?? 10
    readonly property string specialValueText: delegate.pres.specialValueText ?? ""

    visible: aspectVisible
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    Label {
        text: delegate.labelText
        Layout.preferredWidth: Metrics.formLabelWidth
        // An aspect with no label of its own reserves no room for one.
        visible: text !== ""
        elide: Text.ElideRight
    }

    Label {
        text: delegate.pres.prefix ?? ""
        visible: text !== ""
    }

    SpinBox {
        id: box

        editable: true
        enabled: (delegate.aspect?.enabled ?? false) && !(delegate.aspect?.readOnly ?? false)
        // A delegate can outlive its aspect: the property goes null and pres
        // becomes empty, so every bound needs a value of the right type.
        from: Math.round((delegate.pres.minimum ?? 0) / delegate.scale)
        to: Math.round((delegate.pres.maximum ?? 0) / delegate.scale)
        stepSize: delegate.pres.step ?? 1
        value: Math.round((delegate.aspect?.value ?? 0) / delegate.scale)
        ToolTip.text: delegate.toolTip
        ToolTip.visible: hovered && delegate.toolTip !== ""

        // A QSpinBox groups no digits and can write another base; Qt Quick's
        // formats for the locale, which turns a port number into "46.327".
        textFromValue: function(value, locale) {
            if (delegate.specialValueText !== "" && value === box.from)
                return delegate.specialValueText
            return value.toString(delegate.base)
        }

        valueFromText: function(text, locale) {
            if (delegate.specialValueText !== "" && text === delegate.specialValueText)
                return box.from
            const parsed = parseInt(text, delegate.base)
            return isNaN(parsed) ? box.value : parsed
        }

        onValueModified: if (delegate.aspect) delegate.aspect.value = value * delegate.scale
    }

    Label {
        text: delegate.pres.suffix ?? ""
        visible: text !== ""
    }

    Item { Layout.fillWidth: true }
}
