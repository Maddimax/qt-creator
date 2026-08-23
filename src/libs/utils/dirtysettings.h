// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include <functional>

QT_BEGIN_NAMESPACE
class QObject;
QT_END_NAMESPACE

namespace Utils {

// Tells the settings dialog that something it shows has changed, and asks it to
// re-check. Kept apart from guiutils.h so that code with a setting to report
// does not need the widget helpers that install the triggers.

// returns previous value
QTCREATOR_UTILS_EXPORT bool setIgnoreForDirtyHook(QObject *object, bool ignore = true);
QTCREATOR_UTILS_EXPORT bool isIgnoredForDirtyHook(const QObject *object);

QTCREATOR_UTILS_EXPORT void markSettingsDirty();
QTCREATOR_UTILS_EXPORT void checkSettingsDirty();

// Disables use of dirty hooks while active. Reference-counted, so overlapping
// guards (even from different threads) no longer strand the suppression state.
class QTCREATOR_UTILS_EXPORT DirtySettingsGuard
{
public:
    DirtySettingsGuard();
    ~DirtySettingsGuard();
};

namespace Internal {
QTCREATOR_UTILS_EXPORT void setMarkSettingsDirtyHook(const std::function<void (bool)> &hook);
QTCREATOR_UTILS_EXPORT void setCheckSettingsDirtyHook(const std::function<void ()> &hook);
} // namespace Internal

} // namespace Utils
