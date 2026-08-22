// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <qglobal.h>

#if defined(EXTENSIONSYSTEMWIDGETS_LIBRARY)
#  define EXTENSIONSYSTEM_WIDGETS_EXPORT Q_DECL_EXPORT
#elif defined(EXTENSIONSYSTEMWIDGETS_STATIC_LIBRARY)
#  define EXTENSIONSYSTEM_WIDGETS_EXPORT
#else
#  define EXTENSIONSYSTEM_WIDGETS_EXPORT Q_DECL_IMPORT
#endif
