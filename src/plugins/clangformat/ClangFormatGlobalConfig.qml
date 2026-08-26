// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

// Shown above a code style, both on the global page and on a project's. Which
// of these a project may set is the container's decision, not this form's: an
// aspect a project does not own reports itself invisible.
AspectPage {
    id: root

    BoolDelegate { aspect: aspects.UseGlobalSettings }
    BoolDelegate { aspect: aspects.UseClangFormat }

    IntegerDelegate { aspect: aspects.FileSizeThreshold }
    SelectionDelegate { aspect: aspects.FormattingMode }

    // Indented under the mode above, because that is what decides whether
    // either of them can be chosen at all.
    ColumnLayout {
        spacing: Spacing.GapVXs
        Layout.leftMargin: Spacing.PaddingHL

        BoolDelegate { aspect: aspects.FormatWhileTyping }
        BoolDelegate { aspect: aspects.FormatOnSave }
    }

    BoolDelegate { aspect: aspects.UseCustomSettings }

    TextDisplayDelegate { aspect: aspects.ProjectHasClangFormat }
    TextDisplayDelegate { aspect: aspects.ProjectFileNote }
}
