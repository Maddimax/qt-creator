// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "outputtasksink.h"

namespace Core {

OutputTaskSink::~OutputTaskSink() = default;

QPair<int, int> taskLineRange(int blockCount, int linkedOutputLines, int skipLines,
                              int offset, int extraLines)
{
    const int blockNumber = blockCount - offset;

    // -1 because the formatter has already added the newline, so the document
    // holds one block more than it holds lines of output.
    const int firstLine = blockNumber - linkedOutputLines - skipLines - 1 + extraLines;

    return {firstLine, firstLine + linkedOutputLines - 1};
}

} // namespace Core
