// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "extensionsystemwidgets_global.h"

namespace ExtensionSystem {

// Installs the QtWidgets implementations of PluginPrompts. Call once, before
// plugins are loaded.
EXTENSIONSYSTEM_WIDGETS_EXPORT void installWidgetPrompts();

} // namespace ExtensionSystem
