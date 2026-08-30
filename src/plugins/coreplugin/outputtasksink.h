// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "core_global.h"

#include <QHash>
#include <QPair>

QT_BEGIN_NAMESPACE
class QTextDocument;
QT_END_NAMESPACE

namespace Core {

// An output view that remembers which of its lines a task was reported from,
// so that clicking the task in the Issues pane can show them.
//
// A parser reaches this through the object the formatter was given as its
// sink. Both output views implement it and they are unrelated classes, so it
// is found with dynamic_cast rather than by naming either of them - which is
// what a parser used to do, and what silently registered nothing the moment a
// pane was drawn with something else.
class CORE_EXPORT OutputTaskSink
{
public:
    virtual ~OutputTaskSink();

    enum class TaskSource {
        Direct, // The task's output is whatever is still queued to be written.
        Parsed, // It is the last thing written, because a parser just read it.
    };

    virtual void registerPositionOf(unsigned taskId, int linkedOutputLines, int skipLines,
                                    int offset = 0, TaskSource source = TaskSource::Direct) = 0;
};

// Which lines of a document a task's output occupies. Kept out of both views
// because it is arithmetic over line numbers, not something either of them
// does - inside one, the only way to ask it anything was to run a build and
// click a task.
//
// \a extraLines is what has not been written yet: a task reported directly
// names output still queued, while one a parser found names output already
// there.
CORE_EXPORT QPair<int, int> taskLineRange(int blockCount, int linkedOutputLines, int skipLines,
                                          int offset, int extraLines);

} // namespace Core
