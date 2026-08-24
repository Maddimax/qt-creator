// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma Singleton
pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick

// Sizes that are not spacing, so they have no SpacingTokens equivalent, but
// which several components have to agree on.
QtObject {
    // Width of the label column in a settings form.
    readonly property int formLabelWidth: 200
    // Preferred width of a bounded editor in a settings form.
    readonly property int formControlWidth: 240
    // Height of a list editor in a settings form: enough rows to be worth
    // scrolling, not enough to dominate the page.
    readonly property int formListHeight: 120
    // Height of a multi-line text editor in a settings form. Shorter than a
    // list: what goes in one is usually a handful of lines.
    readonly property int formTextAreaHeight: 90
    // Widest a table column may get from its contents. Past this the text
    // elides: a column wider than the table scrolls the header label out of
    // view, and a long description would take the whole width.
    readonly property int tableColumnMaxWidth: 320
    // Shortest a table row may be, so a row of check boxes and a row of
    // wrapped text still look like the same table.
    readonly property int tableRowMinimumHeight: 24
    // Side length of an icon beside a row's text in a list editor.
    readonly property int listRowIconSize: 16
    // Side length of the color preview swatch in ColorDelegate.
    readonly property int colorSwatchSize: 24

    // Opacity applied to a disabled icon that keeps its enabled tint
    // (QtcSearchBox's leading icon, mirroring disabledIconOpacity).
    readonly property real disabledIconOpacity: 0.3

    // QtcSwitch's track: fixed pixel size in the widget, not on the
    // spacing scale (the track height reuses Spacing.PrimitiveXl).
    readonly property int switchTrackWidth: 32
    // Gap between the track's edge and the knob, enabled+checked vs. any
    // other state (QtcSwitch's thumbPadding).
    readonly property int switchKnobInsetChecked: 3
    readonly property int switchKnobInsetUnchecked: 2
    // The knob stretches while pressed; widths for the checked and
    // unchecked cases (QtcSwitch's thumbW when isDown()).
    readonly property int switchKnobPressedWidthChecked: 17
    readonly property int switchKnobPressedWidthUnchecked: 19
    // Side length of the on/off glyph drawn inside the track.
    readonly property int switchMarkSize: 6

    // QtcProgressBar's sizeHint width; the track height reuses
    // Spacing.PrimitiveM.
    readonly property int progressBarMinimumWidth: 64

    // Default width for a single-line text editor with no content yet
    // (QtcLineEdit, QtcSearchBox), matching the QtCreatorStyle TextField
    // and ComboBox background's own implicitWidth.
    readonly property int lineEditWidth: 120
}
