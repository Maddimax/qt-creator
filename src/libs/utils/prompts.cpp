// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "prompts.h"

namespace Utils::Prompts {

static QuestionPrompt s_questionPrompt;
static ErrorPrompt s_errorPrompt;
static FileChoicePrompt s_fileChoicePrompt;

void setQuestionPrompt(const QuestionPrompt &prompt)
{
    s_questionPrompt = prompt;
}

void setErrorPrompt(const ErrorPrompt &prompt)
{
    s_errorPrompt = prompt;
}

void setFileChoicePrompt(const FileChoicePrompt &prompt)
{
    s_fileChoicePrompt = prompt;
}

Button askQuestion(const QString &title, const QString &text, Buttons buttons,
                   Button defaultButton)
{
    return s_questionPrompt ? s_questionPrompt(title, text, buttons, defaultButton)
                            : defaultButton;
}

void showError(const QString &title, const QString &text)
{
    if (s_errorPrompt)
        s_errorPrompt(title, text);
}

FilePath chooseFile(const FilePaths &candidates)
{
    return s_fileChoicePrompt ? s_fileChoicePrompt(candidates) : FilePath();
}

} // namespace Utils::Prompts
