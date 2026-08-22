// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

// Mirrors Utils::QtcSearchBox: a QtcLineEdit with a leading (here: trailing,
// see below) search icon shown only while empty.
//
// Utils::QtcSearchBox draws Core's ":/core/images/search.png", a resource of
// the Core plugin that this module cannot reach. Utils::Icons::MAGNIFIER
// (":/utils/images/magnifier.png") is the equivalent icon already available
// through the shared icon provider, so it stands in here.
QtcLineEdit {
    id: root

    rightContentPadding: Spacing.PaddingHM + icon.width + Spacing.GapHXs

    Accessible.searchEdit: true

    Image {
        id: icon
        visible: root.text.length === 0
        source: "image://qtcreator/utils/images/magnifier.png?color=Token_Text_Muted"
        opacity: root.enabled ? 1.0 : Metrics.disabledIconOpacity
        anchors.right: parent.right
        anchors.rightMargin: Spacing.PaddingHM
        anchors.verticalCenter: parent.verticalCenter
    }
}
