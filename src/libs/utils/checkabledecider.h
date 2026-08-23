// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include <functional>

namespace Utils {

class Key;
class QtcSettings;

// Whether a question with a "Do not ask again" check box should still be asked,
// and how to remember that it should not be. Kept apart from
// CheckableMessageBox so that code deciding this does not need a dialog.
class QTCREATOR_UTILS_EXPORT CheckableDecider
{
public:
    CheckableDecider() = default;
    CheckableDecider(const Key &settingsSubKey);
    CheckableDecider(bool *doNotAskAgain);
    CheckableDecider(const std::function<bool()> &should, const std::function<void()> &doNot)
        : shouldAskAgain(should), doNotAskAgain(doNot)
    {}

    std::function<bool()> shouldAskAgain;
    std::function<void()> doNotAskAgain;
};

// Where the answers are kept. Set once at startup, by
// CheckableMessageBox::initialize().
QTCREATOR_UTILS_EXPORT void setDoNotAskAgainSettings(QtcSettings *settings);
QTCREATOR_UTILS_EXPORT QtcSettings *doNotAskAgainSettings();
QTCREATOR_UTILS_EXPORT Key doNotAskAgainGroup();

} // namespace Utils
