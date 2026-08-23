// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <qglobal.h>

#if defined(TERMINALMODEL_LIBRARY)
#define TERMINAL_MODEL_EXPORT Q_DECL_EXPORT
#elif defined(TERMINALMODEL_STATIC_LIBRARY)
#define TERMINAL_MODEL_EXPORT
#else
#define TERMINAL_MODEL_EXPORT Q_DECL_IMPORT
#endif
