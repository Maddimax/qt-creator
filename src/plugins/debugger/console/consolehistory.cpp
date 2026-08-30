// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "consolehistory.h"

#include "consoleitem.h"

#include <QAbstractItemModel>

namespace Debugger::Internal {

static QString expressionAt(const QAbstractItemModel *model, int row)
{
    return model->data(model->index(row, 0), ConsoleItem::ExpressionRole).toString();
}

void ConsoleHistory::setModel(const QAbstractItemModel *model)
{
    m_model = model;
    m_promptRow = -1;
    m_row = -1;
    m_halfTyped.clear();
}

void ConsoleHistory::restart(int promptRow)
{
    m_promptRow = promptRow;
    m_row = promptRow;
    m_halfTyped.clear();
}

std::optional<QString> ConsoleHistory::up(const QString &shown)
{
    if (!m_model)
        return std::nullopt;

    if (m_row == m_promptRow)
        m_halfTyped = shown;

    const int previous = ConsoleItem::previousInputRow(m_model, m_row);
    if (previous == -1)
        return std::nullopt;

    m_row = previous;
    return expressionAt(m_model, previous);
}

std::optional<QString> ConsoleHistory::down(const QString &shown)
{
    Q_UNUSED(shown)
    if (!m_model)
        return std::nullopt;

    const int next = ConsoleItem::nextInputRow(m_model, m_row);
    if (next == -1)
        return std::nullopt;

    m_row = next;
    return next == m_promptRow ? m_halfTyped : expressionAt(m_model, next);
}

} // Debugger::Internal
