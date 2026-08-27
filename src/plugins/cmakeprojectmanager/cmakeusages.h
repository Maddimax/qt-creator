// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QObject>
#include <QTextCursor>

namespace Core { class IEditor; }

namespace CMakeProjectManager::Internal {

// Where the symbol under the cursor is named: a function, macro, target or
// variable, wherever the project spells it out. Asked of the editor rather
// than of a widget, since a CMake file opens in the Qt Quick view, which is
// not one.
void findUsagesUnderCursor(Core::IEditor *editor, QTextCursor cursor = {});

// The same places, offered for a name of the user's choosing.
void renameSymbolUnderCursor(Core::IEditor *editor, QTextCursor cursor = {});

#ifdef WITH_TESTS
QObject *createCMakeUsagesTest();
#endif

} // CMakeProjectManager::Internal
