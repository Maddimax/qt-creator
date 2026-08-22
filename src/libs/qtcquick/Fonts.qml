// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

pragma Singleton
pragma ComponentBehavior: Bound
pragma FunctionSignatureBehavior: Enforced

import QtQuick
import QtCreator.Ui

QtObject {
    readonly property font h1: Theme.uiFont(UiElement.UiElementH1)
    readonly property int h1LineHeight: Theme.uiFontLineHeight(UiElement.UiElementH1)
    readonly property font h2: Theme.uiFont(UiElement.UiElementH2)
    readonly property int h2LineHeight: Theme.uiFontLineHeight(UiElement.UiElementH2)
    readonly property font h3: Theme.uiFont(UiElement.UiElementH3)
    readonly property int h3LineHeight: Theme.uiFontLineHeight(UiElement.UiElementH3)
    readonly property font h4: Theme.uiFont(UiElement.UiElementH4)
    readonly property int h4LineHeight: Theme.uiFontLineHeight(UiElement.UiElementH4)
    readonly property font h5: Theme.uiFont(UiElement.UiElementH5)
    readonly property int h5LineHeight: Theme.uiFontLineHeight(UiElement.UiElementH5)
    readonly property font h6: Theme.uiFont(UiElement.UiElementH6)
    readonly property int h6LineHeight: Theme.uiFontLineHeight(UiElement.UiElementH6)
    readonly property font h6Capital: Theme.uiFont(UiElement.UiElementH6Capital)
    readonly property int h6CapitalLineHeight: Theme.uiFontLineHeight(UiElement.UiElementH6Capital)
    readonly property font body1: Theme.uiFont(UiElement.UiElementBody1)
    readonly property int body1LineHeight: Theme.uiFontLineHeight(UiElement.UiElementBody1)
    readonly property font body2: Theme.uiFont(UiElement.UiElementBody2)
    readonly property int body2LineHeight: Theme.uiFontLineHeight(UiElement.UiElementBody2)
    readonly property font buttonMedium: Theme.uiFont(UiElement.UiElementButtonMedium)
    readonly property int buttonMediumLineHeight: Theme.uiFontLineHeight(UiElement.UiElementButtonMedium)
    readonly property font buttonSmall: Theme.uiFont(UiElement.UiElementButtonSmall)
    readonly property int buttonSmallLineHeight: Theme.uiFontLineHeight(UiElement.UiElementButtonSmall)
    readonly property font labelMedium: Theme.uiFont(UiElement.UiElementLabelMedium)
    readonly property int labelMediumLineHeight: Theme.uiFontLineHeight(UiElement.UiElementLabelMedium)
    readonly property font labelSmall: Theme.uiFont(UiElement.UiElementLabelSmall)
    readonly property int labelSmallLineHeight: Theme.uiFontLineHeight(UiElement.UiElementLabelSmall)
    readonly property font captionStrong: Theme.uiFont(UiElement.UiElementCaptionStrong)
    readonly property int captionStrongLineHeight: Theme.uiFontLineHeight(UiElement.UiElementCaptionStrong)
    readonly property font caption: Theme.uiFont(UiElement.UiElementCaption)
    readonly property int captionLineHeight: Theme.uiFontLineHeight(UiElement.UiElementCaption)
    readonly property font iconStandard: Theme.uiFont(UiElement.UiElementIconStandard)
    readonly property int iconStandardLineHeight: Theme.uiFontLineHeight(UiElement.UiElementIconStandard)
    readonly property font iconActive: Theme.uiFont(UiElement.UiElementIconActive)
    readonly property int iconActiveLineHeight: Theme.uiFontLineHeight(UiElement.UiElementIconActive)
}
