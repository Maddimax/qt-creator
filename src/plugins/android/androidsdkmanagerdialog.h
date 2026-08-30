// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
#pragma once

namespace Android::Internal {

void executeAndroidSdkManagerDialog();

#ifdef WITH_TESTS
QObject *createSdkManagerOptionsTest();
#endif

} // Android::Internal
