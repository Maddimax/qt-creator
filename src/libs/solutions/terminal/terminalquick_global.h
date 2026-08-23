// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <qglobal.h>

#if defined(TERMINALQUICK_LIBRARY)
#define TERMINAL_QUICK_EXPORT Q_DECL_EXPORT
#elif defined(TERMINALQUICK_STATIC_LIBRARY)
#define TERMINAL_QUICK_EXPORT
#else
#define TERMINAL_QUICK_EXPORT Q_DECL_IMPORT
#endif
