// Copyright (C) 2020 Alexis Jeandet.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QObject>

namespace MesonProjectManager::Internal {

void setupToolsSettingsPage();

#ifdef WITH_TESTS
QObject *createToolsSettingsTest();
#endif

} // MesonProjectManager::Internal
