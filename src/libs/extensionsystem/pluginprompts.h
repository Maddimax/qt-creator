// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "extensionsystem_global.h"

#include <QSet>

#include <functional>

namespace ExtensionSystem {

class PluginSpec;

// Questions the plugin loader has to ask the user but cannot answer itself.
// A user interface installs them; without them the loader takes the
// conservative path, which is to leave the configuration alone and to treat
// terms and conditions as not accepted.
namespace PluginPrompts {

// Whether to temporarily disable \a spec, which appears to have crashed the
// previous run, together with the \a dependents that would go with it.
using DisableCrashed
    = std::function<bool(PluginSpec *spec, const QSet<PluginSpec *> &dependents)>;

// Whether the user accepts \a spec's terms and conditions.
using AcceptTermsAndConditions = std::function<bool(PluginSpec *spec)>;

EXTENSIONSYSTEM_EXPORT void setDisableCrashed(const DisableCrashed &prompt);
EXTENSIONSYSTEM_EXPORT void setAcceptTermsAndConditions(const AcceptTermsAndConditions &prompt);

EXTENSIONSYSTEM_EXPORT bool askDisableCrashed(PluginSpec *spec,
                                              const QSet<PluginSpec *> &dependents);
EXTENSIONSYSTEM_EXPORT bool askAcceptTermsAndConditions(PluginSpec *spec);

} // namespace PluginPrompts
} // namespace ExtensionSystem
