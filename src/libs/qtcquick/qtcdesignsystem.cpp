// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qtcdesignsystem.h"

namespace QtcQuick {

DesignSystem::DesignSystem(QObject *parent)
    : Utils::Theme(Utils::creatorTheme(), parent)
{}

QColor DesignSystem::colorToken(const QString &token) const
{
    const Utils::Result<Utils::Theme::Color> role = Utils::Theme::colorToken(token);
    return role ? color(*role) : QColor();
}

QFont DesignSystem::uiFont(Utils::StyleHelper::UiElement element) const
{
    return Utils::StyleHelper::uiFont(element);
}

int DesignSystem::uiFontLineHeight(Utils::StyleHelper::UiElement element) const
{
    return Utils::StyleHelper::uiFontLineHeight(element);
}

bool DesignSystem::isDark() const
{
    return colorScheme() == Qt::ColorScheme::Dark;
}

} // namespace QtcQuick
