// Copyright (C) 2024 The Qt Company Ltd.
// Copyright (C) 2026 BogDan Vatra <bogdan@kde.org>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QObject>

namespace GNProjectManager::Internal {

void setupGNToolsSettingsPage();

#ifdef WITH_TESTS
QObject *createGNToolsSettingsTest();
#endif

} // namespace GNProjectManager::Internal
