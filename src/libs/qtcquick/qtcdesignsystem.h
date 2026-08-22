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

// Deriving from Utils::Theme is what makes its Color, Flag and ImageFile enums
// resolvable as Theme.Token_Text_Default in QML.
class QTCQUICK_EXPORT DesignSystem : public Utils::Theme
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Theme)
    QML_SINGLETON

    Q_PROPERTY(bool dark READ isDark NOTIFY changed)

public:
    explicit DesignSystem(QObject *parent = nullptr);

    Q_INVOKABLE QColor colorToken(const QString &token) const;
    Q_INVOKABLE QFont uiFont(Utils::StyleHelper::UiElement element) const;
    Q_INVOKABLE int uiFontLineHeight(Utils::StyleHelper::UiElement element) const;

    bool isDark() const;

signals:
    // A theme change requires a restart, so this is the seam rather than a live signal.
    void changed();
};

} // namespace QtcQuick
