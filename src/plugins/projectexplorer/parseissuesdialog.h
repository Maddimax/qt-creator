// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QtGlobal>

QT_BEGIN_NAMESPACE
class QObject;
QT_END_NAMESPACE

namespace ProjectExplorer::Internal {

void executeParseIssuesDialog();

#ifdef WITH_TESTS
QObject *createParseIssuesDialogTest();
#endif

} // namespace ProjectExplorer::Internal
