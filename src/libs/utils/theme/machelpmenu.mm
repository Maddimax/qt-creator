// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "machelpmenu.h"

#include <QMenu>

#include <AppKit/AppKit.h>

namespace Utils {

void setMacOSHelpMenu(QMenu *menu)
{
    NSApp.helpMenu = menu->toNSMenu();
}

} // namespace Utils
