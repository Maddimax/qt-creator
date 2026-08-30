// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "consoleedit.h"
#include "console.h"
#include "consoleitem.h"

#include <utils/qtcassert.h>

#include <QUrl>
#include <QMenu>
#include <QKeyEvent>

namespace Debugger::Internal {

ConsoleEdit::ConsoleEdit(const QModelIndex &index, QWidget *parent) :
    QTextEdit(parent),
    m_historyIndex(index)
{
    setFrameStyle(QFrame::NoFrame);
    setUndoRedoEnabled(false);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ensureCursorVisible();
    setTextInteractionFlags(Qt::TextEditorInteraction);
}

void ConsoleEdit::keyPressEvent(QKeyEvent *e)
{
    bool keyConsumed = false;

    switch (e->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        QString currentScript = getCurrentScript();
        debuggerConsole()->evaluate(currentScript);
        emit editingFinished();
        keyConsumed = true;
        break;
    }

    case Qt::Key_Up:
        handleUpKey();
        keyConsumed = true;
        break;

    case Qt::Key_Down:
        handleDownKey();
        keyConsumed = true;
        break;

    default:
        break;
    }

    if (!keyConsumed)
        QTextEdit::keyPressEvent(e);
}

void ConsoleEdit::focusOutEvent(QFocusEvent * /*e*/)
{
    emit editingFinished();
}

void ConsoleEdit::handleUpKey()
{
    QTC_ASSERT(m_historyIndex.isValid(), return);
    int currentRow = m_historyIndex.row();
    const QAbstractItemModel *model = m_historyIndex.model();
    if (currentRow == model->rowCount() - 1)
        m_cachedScript = getCurrentScript();

    const int previous = ConsoleItem::previousInputRow(model, currentRow);
    if (previous == -1)
        return;

    m_historyIndex = model->index(previous, 0);
    replaceCurrentScript(model->data(m_historyIndex, ConsoleItem::ExpressionRole).toString());
}

void ConsoleEdit::handleDownKey()
{
    QTC_ASSERT(m_historyIndex.isValid(), return);
    int currentRow = m_historyIndex.row();
    const QAbstractItemModel *model = m_historyIndex.model();
    const int next = ConsoleItem::nextInputRow(model, currentRow);
    if (next == -1)
        return;

    m_historyIndex = model->index(next, 0);
    // Back at the bottom is back to what was being typed before the history
    // was walked into, not to the last thing that was run.
    if (next == model->rowCount() - 1)
        replaceCurrentScript(m_cachedScript);
    else
        replaceCurrentScript(model->data(m_historyIndex, ConsoleItem::ExpressionRole).toString());
}

QString ConsoleEdit::getCurrentScript() const
{
    QTextCursor cursor = textCursor();
    cursor.setPosition(0);
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    return cursor.selectedText();
}

void ConsoleEdit::replaceCurrentScript(const QString &script)
{
    QTextCursor cursor = textCursor();
    cursor.setPosition(0);
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.insertText(script);
    setTextCursor(cursor);
}

} // Debugger::Internal
