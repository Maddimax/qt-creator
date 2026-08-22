// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick

// Mirrors Utils::QtcIconDisplay: a fixed icon at its natural size, centred
// in whatever space it is given. Utils::QtcIconDisplay::paintEvent() does
// not check isEnabled() at all, so unlike this module's other icon-bearing
// components, this one applies no disabled treatment either - a faithful
// mirror of what looks like an oversight on the widget side, not a fix.
//
// The widget has no text or tooltip of its own, so it is purely decorative
// unless the call site names it; accessibleName defaults to empty and the
// item is then hidden from the accessibility tree, matching how a
// decorative image should be exposed.
Item {
    id: root

    property string iconSource: ""
    property string accessibleName: ""

    implicitWidth: image.implicitWidth
    implicitHeight: image.implicitHeight

    Accessible.role: Accessible.Graphic
    Accessible.name: root.accessibleName
    Accessible.ignored: root.accessibleName.length === 0

    Image {
        id: image
        anchors.centerIn: parent
        source: root.iconSource
        smooth: true
    }
}
