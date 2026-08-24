// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCreator.Ui

Label {
    required property Aspect aspect
    readonly property string labelText: aspect?.plainLabelText ?? ""
    readonly property bool aspectVisible: aspect?.visible ?? true

    // Deliberately visible rather than silent, so gaps are obvious.
    text: qsTr("%1 (no Qt Quick editor yet)")
              .arg(labelText !== "" ? labelText : (aspect?.objectName ?? ""))
    visible: aspectVisible
    color: Tokens.notificationAlertDefault
    font: Fonts.caption
    Layout.fillWidth: true
}
