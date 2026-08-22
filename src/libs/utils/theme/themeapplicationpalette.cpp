// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "theme.h"
#include "theme_p.h"


#include <QApplication>

namespace Utils {

// Kept apart from theme.cpp so that a theme can be built and queried without
// QtWidgets. QApplication::setPalette() is used rather than the QGuiApplication
// one because it also propagates to existing widgets.
void setThemeApplicationPalette()
{
    Theme *theme = creatorTheme();
    if (theme && Internal::isOverridingPalette(theme))
        QApplication::setPalette(theme->palette());
}

} // namespace Utils
