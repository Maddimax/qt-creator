// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "abiwidget.h"

#include "abi.h"
#include "abiaspect.h"

#include <utils/layoutbuilder.h>

/*!
    \class ProjectExplorer::AbiWidget

    \brief The AbiWidget class is a widget to set an ABI.

    What it shows is AbiAspects; this is one way of drawing that, and the one
    the settings pages still on widgets use.

    \sa ProjectExplorer::Abi, ProjectExplorer::AbiAspects
*/

namespace ProjectExplorer {
namespace Internal {

class AbiWidgetPrivate
{
public:
    AbiAspects m_aspects;
};

} // namespace Internal

AbiWidget::AbiWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Internal::AbiWidgetPrivate>())
{
    using namespace Layouting;

    const QLatin1String separator("-");
    const QList<Utils::SelectionAspect *> parts = d->m_aspects.parts();

    Row {
        &d->m_aspects.choice(),
        parts.at(0),
        separator,
        parts.at(1),
        separator,
        parts.at(2),
        separator,
        parts.at(3),
        separator,
        parts.at(4),
        st,
        spacing(2), noMargin,
    }.attachTo(this);

    connect(&d->m_aspects, &AbiAspects::abiChanged, this, &AbiWidget::abiChanged);
}

AbiWidget::~AbiWidget() = default;

void AbiWidget::setAbis(const Abis &abiList, const Abi &currentAbi)
{
    d->m_aspects.setAbis(abiList, currentAbi);
}

Abis AbiWidget::supportedAbis() const
{
    return d->m_aspects.supportedAbis();
}

bool AbiWidget::isCustomAbi() const
{
    return d->m_aspects.isCustomAbi();
}

Abi AbiWidget::currentAbi() const
{
    return d->m_aspects.currentAbi();
}

} // namespace ProjectExplorer
