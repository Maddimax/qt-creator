// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qtcquick_global.h"

#include <utils/stylehelper.h>
#include <utils/theme/theme.h>

#include <QFont>
#include <QQmlEngine>

namespace QtcQuick {

// Utils::StyleHelper's tokens, addressable from QML as Spacing.GapVM and
// UiElement.UiElementBody2.
namespace Spacing {
Q_NAMESPACE
QML_FOREIGN_NAMESPACE(Utils::StyleHelper::SpacingTokens)
QML_ELEMENT
} // namespace Spacing

namespace UiElements {
Q_NAMESPACE
QML_FOREIGN_NAMESPACE(Utils::StyleHelper)
QML_NAMED_ELEMENT(UiElement)
} // namespace UiElements

// Registered so that the Color, Flag and ImageFile enums are reachable as
// ThemeColor.Token_Text_Default. Prefer the Tokens singleton, which has a real
// property per token and therefore re-evaluates when the theme changes.
struct ThemeColorForeign
{
    Q_GADGET
    QML_FOREIGN(Utils::Theme)
    QML_NAMED_ELEMENT(ThemeColor)
    QML_UNCREATABLE("Use the Theme singleton")
};

// Forwards to the current theme rather than holding a copy, so that a theme
// change is visible immediately.
class QTCQUICK_EXPORT DesignSystem : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Theme)
    QML_SINGLETON

    Q_PROPERTY(bool dark READ isDark NOTIFY changed)

public:
    explicit DesignSystem(QObject *parent = nullptr);

    Q_INVOKABLE QColor color(Utils::Theme::Color role) const;
    Q_INVOKABLE bool flag(Utils::Theme::Flag f) const;
    Q_INVOKABLE QColor colorToken(const QString &token) const;
    Q_INVOKABLE QFont uiFont(Utils::StyleHelper::UiElement element) const;
    Q_INVOKABLE int uiFontLineHeight(Utils::StyleHelper::UiElement element) const;

    bool isDark() const;

signals:
    void changed();
};

} // namespace QtcQuick
