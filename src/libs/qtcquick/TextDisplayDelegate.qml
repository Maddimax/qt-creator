// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

// Text the page shows and the user cannot edit: a TextDisplay's message, or a
// StringAspect drawn as a label. Both put what they show in displayText; a
// label of their own, where they have one, goes in front of it.
RowLayout {
    id: root

    required property Aspect aspect
    // Not readonly: an aspect can change what it wants drawn - the label on
    // Copilot's sign-in button is its state - and says so with
    // controlConfigurationChanged().
    property var pres: aspect ? AspectModels.presentation(aspect) : ({})
    readonly property string labelText: aspect?.plainLabelText ?? ""
    readonly property string displayText: aspect?.displayText ?? ""
    readonly property string toolTip: aspect?.toolTip ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true

    Connections {
        target: root.aspect
        function onControlConfigurationChanged() {
            root.pres = AspectModels.presentation(root.aspect)
        }
    }

    visible: aspectVisible && (root.labelText !== "" || root.displayText !== "")
    spacing: Spacing.GapHM
    Layout.fillWidth: true

    Label {
        text: root.labelText
        Layout.preferredWidth: Metrics.formLabelWidth
        // An aspect with no label of its own reserves no room for one.
        visible: text !== ""
        elide: Text.ElideRight
    }

    Label {
        text: root.displayText
        visible: root.displayText !== ""
        wrapMode: root.pres.wordWrap ?? true ? Text.WordWrap : Text.NoWrap
        // Markdown is never detected from the text, so an aspect whose message
        // is written in it has to be asked.
        textFormat: {
            switch (root.pres.textFormat ?? "AutoText") {
            case "PlainText":
                return Text.PlainText
            case "RichText":
                return Text.RichText
            case "MarkdownText":
                return Text.MarkdownText
            default:
                return Text.AutoText
            }
        }
        color: {
            switch (root.pres.infoType ?? "None") {
            case "Error":
            case "NotOk":
                return Tokens.notificationDangerDefault
            case "Warning":
                return Tokens.notificationAlertDefault
            case "Ok":
                return Tokens.notificationSuccessDefault
            default:
                return Tokens.textMuted
            }
        }
        // What a value too long for the row says when the pointer rests on
        // it. An aspect showing a path asks for the value itself here: it is
        // the elided half that the reader wants and the tooltip is the only
        // way to see it.
        ToolTip.text: (root.pres.toolTipShowsValue ?? false) ? root.displayText : root.toolTip
        ToolTip.visible: hover.hovered && ToolTip.text !== ""
        Layout.fillWidth: true

        // A Label is not a Control and has no hovered of its own.
        HoverHandler { id: hover }

        onLinkActivated: (link) => root.aspect?.activateLink(link)
    }
}
