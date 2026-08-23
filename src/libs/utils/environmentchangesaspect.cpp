// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "environmentchangesaspect.h"

#include "aspectwidgets.h"
#include "elidinglabel.h"
#include "environment.h"
#include "environmentdialog.h"
#include "guiutils.h"
#include "layoutbuilder.h"
#include "utilstr.h"

#include <QPushButton>
#include <QSizePolicy>

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

void EnvironmentChangesAspect::addToLayoutImpl(Layouting::Layout &parent)
{
    auto changesLabel = new ElidingLabel();
    QSizePolicy sizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    changesLabel->setSizePolicy(sizePolicy);
    changesLabel->setElideMode(Qt::ElideRight);
    auto updateChangesLabel = [this, changesLabel]() {
        const EnvironmentItems items = volatileValue().itemsFromUser();
        changesLabel->setText(EnvironmentItem::toShortSummary(items, false));
    };
    updateChangesLabel();
    connect(this, &EnvironmentChangesAspect::volatileValueChanged, this, updateChangesLabel);
    AspectWidgets::registerSubWidget(this, changesLabel);

    QPushButton *changeButton = new QPushButton(Tr::tr("Change..."));
    changeButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    AspectWidgets::registerSubWidget(this, changeButton);
    connect(changeButton, &QPushButton::clicked, this, [changeButton, this]() {
        std::optional<EnvironmentChanges> changes
            = runEnvironmentItemsDialog(changeButton, volatileValue());
        if (changes)
            setVolatileValue(*changes);
    });

    // createLabel() may return nullptr; addEmpty == false drops it then.
    parent.addItems({AspectWidgets::createLabel(this), changesLabel, changeButton},
                    /*addEmpty=*/false);
}

} // namespace Utils
