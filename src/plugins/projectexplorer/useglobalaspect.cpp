// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "useglobalaspect.h"

#include "projectexplorertr.h"

#include <coreplugin/icore.h>

using namespace Utils;

namespace ProjectExplorer {

UseGlobalAspect::UseGlobalAspect(Id settingsPageId, AspectContainer *container)
    : BoolAspect(container)
    , m_settingsPageId(settingsPageId)
{
    setDefaultValue(true);
    // Beside the box rather than on it: the words "global settings" take you
    // to the page they come from, and a check box's own text is plain.
    setLabel(Tr::tr("Use <a href=\"page\">global settings</a>"),
             LabelPlacement::BesideCheckBox);
}

void UseGlobalAspect::setSettingsPageId(Id settingsPageId)
{
    m_settingsPageId = settingsPageId;
}

void UseGlobalAspect::activateLink(const QString &link)
{
    Q_UNUSED(link)
    Core::ICore::showSettings(m_settingsPageId);
}

} // namespace ProjectExplorer
