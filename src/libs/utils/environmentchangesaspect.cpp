// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "environmentchangesaspect.h"

#include "environment.h"
#include "environmentdialog.h"
#include "guiutils.h"
#include "utilstr.h"


namespace Utils {

EnvironmentChangesAspect::EnvironmentChangesAspect(AspectContainer *container)
    : TypedAspect(container)
{
    // The summary is derived from the value, so it changes with it.
    connect(this, &BaseAspect::volatileValueChanged,
            this, &BaseAspect::displayTextChanged);
}

AspectPresentation EnvironmentChangesAspect::presentation() const
{
    AspectPresentation p = TypedAspect::presentation();
    p.control = AspectControls::TextWithAction;
    p.actionText = Tr::tr("Change...");
    return p;
}

QString EnvironmentChangesAspect::displayText() const
{
    return EnvironmentItem::toShortSummary(volatileValue().itemsFromUser(), false);
}

void EnvironmentChangesAspect::triggerAction()
{
    const std::optional<EnvironmentChanges> changes
        = runEnvironmentItemsDialog(dialogParent(), volatileValue());
    if (changes)
        setVolatileValue(*changes);
}

} // namespace Utils
