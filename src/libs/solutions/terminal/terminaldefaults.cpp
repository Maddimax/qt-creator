// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "terminaldefaults.h"

namespace TerminalSolution {

QString defaultFontFamily()
{
#ifdef Q_OS_DARWIN
    return QLatin1String("Menlo");
#elif defined(Q_OS_WIN)
    return QLatin1String("Consolas");
#else
    return QLatin1String("Monospace");
#endif
}

int defaultFontSize()
{
#ifdef Q_OS_DARWIN
    return 12;
#elif defined(Q_OS_WIN)
    return 10;
#else
    return 9;
#endif
}

} // namespace TerminalSolution
