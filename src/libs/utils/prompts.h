// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include <QString>

#include <functional>

namespace Utils::Prompts {

// Questions that core code has to ask the user. A user interface installs the
// implementations; without them the caller gets Cancel, which is the answer
// that changes nothing.
enum class Button {
    Cancel = 0,
    Yes,
    No,
    YesToAll,
    NoToAll,
};

enum Buttons {
    YesNo,
    YesNoCancel,
    OverwriteButtons, // Yes, YesToAll, No, NoToAll, Cancel
};

using QuestionPrompt
    = std::function<Button(const QString &title, const QString &text, Buttons buttons)>;
using ErrorPrompt = std::function<void(const QString &title, const QString &text)>;

QTCREATOR_UTILS_EXPORT void setQuestionPrompt(const QuestionPrompt &prompt);
QTCREATOR_UTILS_EXPORT void setErrorPrompt(const ErrorPrompt &prompt);

QTCREATOR_UTILS_EXPORT Button askQuestion(const QString &title, const QString &text,
                                          Buttons buttons);
QTCREATOR_UTILS_EXPORT void showError(const QString &title, const QString &text);

} // namespace Utils::Prompts
