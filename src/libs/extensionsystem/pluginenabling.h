// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "extensionsystemwidgets_global.h"

#include <QSet>

#include <optional>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace ExtensionSystem {

class PluginSpec;

// Asks the user to confirm the plugins that additionally have to be enabled or
// disabled along with \a plugins. Returns the additional plugins, which may be
// an empty set, or nullopt if the user cancelled.
EXTENSIONSYSTEM_WIDGETS_EXPORT std::optional<QSet<PluginSpec *>> askForEnablingPlugins(
    QWidget *dialogParent, const QSet<PluginSpec *> &plugins, bool enable);

} // namespace ExtensionSystem
