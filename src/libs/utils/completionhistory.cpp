// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "completionhistory.h"

#include "qtcassert.h"
#include "qtcsettings.h"

namespace Utils::CompletionHistory {

static QtcSettings *theSettings = nullptr;

// One settings key per field, and the same one both kinds of form read.
static Key fullKey(const Key &historyKey)
{
    return "CompleterHistory/" + historyKey;
}

void setSettings(QtcSettings *settings)
{
    theSettings = settings;
}

QStringList entries(const Key &historyKey, int maxLines)
{
    if (!theSettings || historyKey.isEmpty())
        return {};
    return theSettings->value(fullKey(historyKey)).toStringList().mid(0, maxLines);
}

void addEntry(const Key &historyKey, const QString &value, int maxLines)
{
    QTC_ASSERT(theSettings, return);
    if (historyKey.isEmpty())
        return;
    const QString entry = value.trimmed();
    if (entry.isEmpty())
        return;

    QStringList list = entries(historyKey, maxLines);
    list.removeAll(entry);
    list.prepend(entry);
    list = list.mid(0, maxLines - 1);
    theSettings->setValueWithDefault(fullKey(historyKey), list);
}

void clear(const Key &historyKey)
{
    QTC_ASSERT(theSettings, return);
    if (!historyKey.isEmpty())
        theSettings->remove(fullKey(historyKey));
}

bool existsFor(const Key &historyKey)
{
    QTC_ASSERT(theSettings, return false);
    return theSettings->value(fullKey(historyKey)).isValid();
}

} // namespace Utils::CompletionHistory
