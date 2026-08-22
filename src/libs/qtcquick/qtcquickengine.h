// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qtcquick_global.h"

QT_BEGIN_NAMESPACE
class QQmlEngine;
QT_END_NAMESPACE

namespace QtcQuick {

// The engine shared by all of Qt Creator's Qt Quick surfaces. Created on first
// use and owned until shutdown. QTC_QML_IMPORT_PATH, when set, is searched
// before the compiled-in modules so that .qml files can be edited in place.
QTCQUICK_EXPORT QQmlEngine *engine();

} // namespace QtcQuick
