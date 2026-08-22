// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "widgetprompts.h"

#include "guiutils.h"
#include "prompts.h"

#include <QCursor>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>

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

static QMessageBox::StandardButton toStandardButton(Button button)
{
    switch (button) {
    case Button::Yes:      return QMessageBox::Yes;
    case Button::No:       return QMessageBox::No;
    case Button::YesToAll: return QMessageBox::YesToAll;
    case Button::NoToAll:  return QMessageBox::NoToAll;
    case Button::Cancel:   break;
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
    setQuestionPrompt([](const QString &title, const QString &text, Buttons buttons,
                         Button defaultButton) {
        QMessageBox box(QMessageBox::Question, title, text, standardButtons(buttons),
                        dialogParent());
        // Qt only guesses a default and an escape button, and the guess differs
        // between platforms, so set both when the caller named one.
        if (QAbstractButton *button = box.button(toStandardButton(defaultButton))) {
            box.setDefaultButton(qobject_cast<QPushButton *>(button));
            box.setEscapeButton(button);
        }
        box.exec();
        return fromStandardButton(box.standardButton(box.clickedButton()));
    });
    setErrorPrompt([](const QString &title, const QString &text) {
        QMessageBox::critical(dialogParent(), title, text);
    });
    setFileChoicePrompt([](const FilePaths &candidates) {
        QMenu filesMenu;
        for (const FilePath &candidate : candidates)
            filesMenu.addAction(candidate.toUserOutput());
        if (const QAction * const action = filesMenu.exec(QCursor::pos()))
            return FilePath::fromUserInput(action->text());
        return FilePath();
    });
}

} // namespace Utils
