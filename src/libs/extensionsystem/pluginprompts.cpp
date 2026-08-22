// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "pluginprompts.h"

namespace ExtensionSystem::PluginPrompts {

static DisableCrashed s_disableCrashed;
static AcceptTermsAndConditions s_acceptTermsAndConditions;

void setDisableCrashed(const DisableCrashed &prompt)
{
    s_disableCrashed = prompt;
}

void setAcceptTermsAndConditions(const AcceptTermsAndConditions &prompt)
{
    s_acceptTermsAndConditions = prompt;
}

bool askDisableCrashed(PluginSpec *spec, const QSet<PluginSpec *> &dependents)
{
    return s_disableCrashed ? s_disableCrashed(spec, dependents) : false;
}

bool askAcceptTermsAndConditions(PluginSpec *spec)
{
    return s_acceptTermsAndConditions ? s_acceptTermsAndConditions(spec) : false;
}

} // namespace ExtensionSystem::PluginPrompts
