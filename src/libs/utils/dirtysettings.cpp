// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "dirtysettings.h"

#include "qtcassert.h"

#include <QObject>
#include <QThread>
#include <QVariant>

#include <atomic>

namespace Utils {

const char IGNORE_FOR_DIRTY_HOOK[] = "qtcIgnoreForDirtyHook";

bool setIgnoreForDirtyHook(QObject *object, bool ignore)
{
    const bool prev = object->property(IGNORE_FOR_DIRTY_HOOK).toBool();
    if (prev != ignore)
        object->setProperty(IGNORE_FOR_DIRTY_HOOK, ignore);
    return prev;
}

bool isIgnoredForDirtyHook(const QObject *object)
{
    return object->property(IGNORE_FOR_DIRTY_HOOK).toBool();
}

static std::function<void (bool)> s_markSettingDirtyHook;
static std::function<void ()> s_checkSettingDirtyHook;

static std::atomic<int> s_suppressSettingsDirtyLevel = 0;

namespace Internal {

void setMarkSettingsDirtyHook(const std::function<void (bool)> &hook)
{
    s_markSettingDirtyHook = hook;
}

void setCheckSettingsDirtyHook(const std::function<void ()> &hook)
{
    s_checkSettingDirtyHook = hook;
}

} // Internal

void markSettingsDirty()
{
    // The hook drives a QWidget (the settings dialog), so this must run on the GUI thread.
    QTC_ASSERT(QThread::isMainThread(), return);

    // This can happen if per-project settings are opened before
    // the settings mode was entered. The hook is not installed at
    // that time, and it's ok to do nothing.
    if (!s_markSettingDirtyHook)
        return;

    if (s_suppressSettingsDirtyLevel.load() > 0)
        return;

    s_markSettingDirtyHook(true);
}

void checkSettingsDirty()
{
    // The hook drives a QWidget (the settings dialog), so this must run on the GUI thread.
    QTC_ASSERT(QThread::isMainThread(), return);

    if (s_suppressSettingsDirtyLevel.load() > 0)
        return;

    if (!s_checkSettingDirtyHook)
        return;

    s_checkSettingDirtyHook();
}

DirtySettingsGuard::DirtySettingsGuard()
{
    s_suppressSettingsDirtyLevel.fetch_add(1);
}

DirtySettingsGuard::~DirtySettingsGuard()
{
    const int previousLevel = s_suppressSettingsDirtyLevel.fetch_sub(1);
    QTC_CHECK(previousLevel > 0);
}

} // namespace Utils
