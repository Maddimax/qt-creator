// Copyright (C) 2016 Lorenz Haas
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QObject>

namespace Beautifier::Internal {

void setupClangFormat();

#ifdef WITH_TESTS
QObject *createClangFormatTest();
#endif

} // Beautifier::Internal
