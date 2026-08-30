// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QString>

#include <optional>

QT_BEGIN_NAMESPACE
class QAbstractItemModel;
QT_END_NAMESPACE

namespace Debugger::Internal {

// Up and Down in the console's prompt, as a state machine over the rows.
//
// Two things make this more than a walk, and both are easy to lose on a port:
// the first Up has to remember whatever was half-typed, because Down back to
// the bottom restores that rather than the newest entry; and the row the walk
// starts from can stop being the prompt, since the prompt moves down every
// time something is run.
//
// Both answers are std::nullopt at the ends of the history, which is what
// leaves the line alone rather than blanking it.
class ConsoleHistory
{
public:
    void setModel(const QAbstractItemModel *model);
    // The prompt has moved: the walk starts from there again, and whatever was
    // remembered belongs to a line that has already been run.
    void restart(int promptRow);

    std::optional<QString> up(const QString &shown);
    std::optional<QString> down(const QString &shown);

private:
    const QAbstractItemModel *m_model = nullptr;
    int m_promptRow = -1;
    int m_row = -1;
    QString m_halfTyped;
};

} // Debugger::Internal
