// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "checkabledecider.h"

#include "qtcassert.h"
#include "qtcsettings.h"

namespace Utils {

static QtcSettings *theSettings = nullptr;

void setDoNotAskAgainSettings(QtcSettings *settings)
{
    theSettings = settings;
}

QtcSettings *doNotAskAgainSettings()
{
    return theSettings;
}

Key doNotAskAgainGroup()
{
    return "DoNotAskAgain";
}

CheckableDecider::CheckableDecider(const Key &settingsSubKey)
{
    QTC_ASSERT(theSettings, return);
    shouldAskAgain = [settingsSubKey] {
        theSettings->beginGroup(doNotAskAgainGroup());
        bool shouldNotAsk = theSettings->value(settingsSubKey, false).toBool();
        theSettings->endGroup();
        return !shouldNotAsk;
    };
    doNotAskAgain = [settingsSubKey] {
        theSettings->beginGroup(doNotAskAgainGroup());
        theSettings->setValue(settingsSubKey, true);
        theSettings->endGroup();
    };
}

CheckableDecider::CheckableDecider(bool *storage)
{
    shouldAskAgain = [storage] { return !*storage; };
    doNotAskAgain = [storage] { *storage = true; };
}

} // namespace Utils
