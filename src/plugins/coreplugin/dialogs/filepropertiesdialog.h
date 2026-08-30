// Copyright (C) 2018 Andre Hartmann <aha_1980@gmx.de>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../core_global.h"

#include <QByteArray>

namespace Utils { class FilePath; }

namespace Core {

// How a text file is written, as far as its first bytes show. Answered rather
// than shown, so it can be asked without opening a dialog.
struct TextFileStyle
{
    enum class LineEndings { Unknown, Crlf, Lf, Cr };
    enum class Indentation { Unknown, Tabs, Mixed, Spaces };

    LineEndings lineEndings = LineEndings::Unknown;
    Indentation indentation = Indentation::Unknown;
    // Only meaningful for Indentation::Spaces.
    int spaces = 0;
};

CORE_EXPORT TextFileStyle guessTextFileStyle(const QByteArray &contents);

CORE_EXPORT void executeFilePropertiesDialog(const Utils::FilePath &filePath);

#ifdef WITH_TESTS
QObject *createFilePropertiesTest();
#endif

} // Core
