// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "widgetprompts.h"

#include "guiutils.h"
#include "prompts.h"

#include <QMessageBox>

namespace Utils {

using namespace Prompts;

static QMessageBox::StandardButtons standardButtons(Buttons buttons)
{
    switch (buttons) {
    case YesNo:
        return QMessageBox::Yes | QMessageBox::No;
    case YesNoCancel:
        return QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel;
    case OverwriteButtons:
        return QMessageBox::Yes | QMessageBox::YesToAll | QMessageBox::No | QMessageBox::NoToAll
               | QMessageBox::Cancel;
    }
    return QMessageBox::Cancel;
}

static Button fromStandardButton(int answer)
{
    switch (answer) {
    case QMessageBox::Yes:      return Button::Yes;
    case QMessageBox::No:       return Button::No;
    case QMessageBox::YesToAll: return Button::YesToAll;
    case QMessageBox::NoToAll:  return Button::NoToAll;
    default:                    return Button::Cancel;
    }
}

void installWidgetPrompts()
{
    setQuestionPrompt([](const QString &title, const QString &text, Buttons buttons) {
        return fromStandardButton(
            QMessageBox::question(dialogParent(), title, text, standardButtons(buttons)));
    });
    setErrorPrompt([](const QString &title, const QString &text) {
        QMessageBox::critical(dialogParent(), title, text);
    });
}

} // namespace Utils
