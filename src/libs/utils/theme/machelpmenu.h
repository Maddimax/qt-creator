// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../utils_global.h"

#include <qglobal.h>

QT_BEGIN_NAMESPACE
class QMenu;
QT_END_NAMESPACE

namespace Utils {

// Registers the menu as the native help menu, which gives it the search
// field. A no-op on other platforms.
#ifdef Q_OS_MACOS
QTCREATOR_UTILS_EXPORT void setMacOSHelpMenu(QMenu *menu);
#else
inline void setMacOSHelpMenu(QMenu *) {}
#endif

} // namespace Utils
