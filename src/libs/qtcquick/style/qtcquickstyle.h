// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <qglobal.h>

#if defined(QTCQUICKSTYLE_LIBRARY)
#  define QTCQUICKSTYLE_EXPORT Q_DECL_EXPORT
#elif defined(QTCQUICKSTYLE_STATIC_LIBRARY)
#  define QTCQUICKSTYLE_EXPORT
#else
#  define QTCQUICKSTYLE_EXPORT Q_DECL_IMPORT
#endif

namespace QtcQuick {

// Selects the style whose QML this library carries. It has to be called from
// outside this library: a library that is depended on for its resources
// alone, without any symbol being used, is dropped by linkers that default to
// --as-needed, and then the style's QML is not in the resource system at all.
QTCQUICKSTYLE_EXPORT void setQtCreatorStyle();

} // namespace QtcQuick
