// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include "storekey.h"

#include <QStringList>

namespace Utils {

class QtcSettings;

// What has been typed into a field before, most recent first. The store behind
// HistoryCompleter, without the QCompleter: a Qt Quick form cannot use a
// widget popup, and the two kinds of form have to offer the same history or
// the same field would remember different things depending on how it is drawn.
namespace CompletionHistory {

// The default is the widget line edit's, so that a field keeps as much history
// however it is drawn.
constexpr int defaultMaxLines = 6;

QTCREATOR_UTILS_EXPORT void setSettings(QtcSettings *settings);
QTCREATOR_UTILS_EXPORT QStringList entries(const Key &historyKey,
                                           int maxLines = defaultMaxLines);
// Puts \a value at the front, removing it from further down: the history is
// what was used, in the order it was last used.
QTCREATOR_UTILS_EXPORT void addEntry(const Key &historyKey, const QString &value,
                                     int maxLines = defaultMaxLines);
QTCREATOR_UTILS_EXPORT bool existsFor(const Key &historyKey);
// Forgets what was entered in that field. Also what a test does with itself
// afterwards, so that the next run starts where this one did.
QTCREATOR_UTILS_EXPORT void clear(const Key &historyKey);

} // namespace CompletionHistory
} // namespace Utils
