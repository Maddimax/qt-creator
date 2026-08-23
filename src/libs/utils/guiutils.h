// Copyright (C) 2023 Tasuku Suzuki <tasuku.suzuki@signal-slot.co.jp>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include "dirtysettings.h"

QT_BEGIN_NAMESPACE
class QObject;
class QWidget;
QT_END_NAMESPACE

namespace Utils {

QTCREATOR_UTILS_EXPORT void setWheelScrollingWithoutFocusBlocked(QWidget *widget);

// Calls func() once, just before widget is shown for the first time, or
// immediately if it is visible already. Lets a widget keep its construction
// cheap and build the expensive content that only matters on screen later.
// Settings page widgets in particular are also constructed - and never shown -
// just to harvest their search keywords.
QTCREATOR_UTILS_EXPORT void onFirstShow(QWidget *widget, const std::function<void()> &func);

QTCREATOR_UTILS_EXPORT QWidget *dialogParent();
QTCREATOR_UTILS_EXPORT void setDialogParentGetter(QWidget *(*getter)());

QTCREATOR_UTILS_EXPORT void installCheckSettingsDirtyTrigger(QObject *object);

QTCREATOR_UTILS_EXPORT void installMarkSettingsDirtyTrigger(QObject *object);
QTCREATOR_UTILS_EXPORT void installMarkSettingsDirtyTriggerRecursively(QObject *object); // Avoid.

} // namespace Utils
