// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "groupedlistaspect.h"

#include "groupedmodel.h"
#include "qtcassert.h"


namespace Utils {

GroupedListAspect::GroupedListAspect(AspectContainer *container)
    : BaseAspect(container)
{}

GroupedListAspect::~GroupedListAspect() = default;

AspectPresentation GroupedListAspect::presentation() const
{
    AspectPresentation p = BaseAspect::presentation();
    p.control = AspectControls::GroupedList;
    p.showsDefault = m_showsDefault;
    return p;
}

void GroupedListAspect::setModel(GroupedModel *model)
{
    QTC_ASSERT(model, return);
    m_model = model;
    m_selection = std::make_unique<GroupedSelection>(*model);
    if (m_canRemove)
        m_selection->setCanRemoveRow(m_canRemove);
    if (m_canClone)
        m_selection->setCanCloneRow(m_canClone);

    connect(m_selection.get(), &GroupedSelection::currentRowChanged,
            this, &GroupedListAspect::currentRowChanged);
    connect(m_selection.get(), &GroupedSelection::actionsChanged,
            this, &GroupedListAspect::actionsChanged);
    connect(m_selection.get(), &GroupedSelection::currentRemoved,
            this, &GroupedListAspect::currentRemoved);
    connect(m_selection.get(), &GroupedSelection::currentCloned,
            this, &GroupedListAspect::currentCloned);
}

GroupedModel *GroupedListAspect::model() const
{
    return m_model;
}

GroupedSelection *GroupedListAspect::selection() const
{
    return m_selection.get();
}

QAbstractItemModel *GroupedListAspect::displayModel() const
{
    QTC_ASSERT(m_model, return nullptr);
    return m_model->groupedDisplayModel();
}

int GroupedListAspect::rowForIndex(const QModelIndex &displayIndex) const
{
    QTC_ASSERT(m_model, return -1);
    return m_model->mapToSource(displayIndex).row();
}

QModelIndex GroupedListAspect::indexForRow(int row) const
{
    QTC_ASSERT(m_model, return {});
    if (row < 0)
        return {};
    return m_model->mapFromSource(m_model->index(row, 0));
}

int GroupedListAspect::currentRow() const
{
    return m_selection ? m_selection->currentRow() : -1;
}

void GroupedListAspect::setCurrentRow(int row)
{
    QTC_ASSERT(m_selection, return);
    m_selection->setCurrentRow(row);
}

bool GroupedListAspect::canRemove() const
{
    return m_selection && m_selection->canRemoveCurrent();
}

bool GroupedListAspect::canClone() const
{
    return m_selection && m_selection->canCloneCurrent();
}

bool GroupedListAspect::canMakeDefault() const
{
    return m_selection && m_selection->canMakeCurrentDefault();
}

bool GroupedListAspect::currentIsRemoved() const
{
    return m_selection && m_selection->currentIsRemoved();
}

void GroupedListAspect::removeCurrent()
{
    QTC_ASSERT(m_selection, return);
    m_selection->removeCurrent();
}

void GroupedListAspect::cloneCurrent()
{
    QTC_ASSERT(m_selection, return);
    m_selection->cloneCurrent();
}

void GroupedListAspect::makeCurrentDefault()
{
    QTC_ASSERT(m_selection, return);
    m_selection->makeCurrentDefault();
}

void GroupedListAspect::setShowsDefault(bool on)
{
    m_showsDefault = on;
    emit controlConfigurationChanged();
}

void GroupedListAspect::setCanRemoveRow(std::function<bool(int)> predicate)
{
    m_canRemove = predicate;
    if (m_selection)
        m_selection->setCanRemoveRow(std::move(predicate));
}

void GroupedListAspect::setCanCloneRow(std::function<bool(int)> predicate)
{
    m_canClone = predicate;
    if (m_selection)
        m_selection->setCanCloneRow(std::move(predicate));
}

} // namespace Utils
