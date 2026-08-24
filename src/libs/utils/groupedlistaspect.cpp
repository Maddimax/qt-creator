// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "groupedlistaspect.h"

#include "groupedmodel.h"
#include "groupedview.h"
#include "layoutbuilder.h"
#include "qtcassert.h"

#include <QWidget>

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

// The tree and the buttons are members of the GroupedView rather than heap
// allocations, so whatever holds them has to outlive the layout they are put
// in - a widget deleting them as children would be freeing what it never
// allocated. Holding the view by value gets that for nothing: members go
// before ~QWidget deletes children, and each one detaches itself on the way.
class GroupedListWidget : public QWidget
{
public:
    explicit GroupedListWidget(GroupedModel &model)
        : m_view(model)
    {
        using namespace Layouting;
        Row {
            &m_view.view(),
            Column {
                &m_view.cloneButton(),
                &m_view.removeButton(),
                &m_view.makeDefaultButton(),
                st,
            },
            noMargin,
        }.attachTo(this);
    }

    GroupedView &view() { return m_view; }

private:
    GroupedView m_view;
};

// The widget renderer draws this with the QTreeView the pages already used. It
// is a view of its own rather than one built from the descriptor, so it is put
// in whole; the aspect's own selection stays the one in charge.
void GroupedListAspect::addToLayoutImpl(Layouting::Layout &parent)
{
    QTC_ASSERT(m_model, return);
    auto widget = new GroupedListWidget(*m_model);
    GroupedView &view = widget->view();
    connect(&view, &GroupedView::currentRowChanged, this, [this](int, int newRow) {
        setCurrentRow(newRow);
    });
    connect(this, &GroupedListAspect::currentRowChanged, &view, [&view](int, int newRow) {
        if (view.currentRow() != newRow)
            view.selectRow(newRow);
    });
    parent.addItem(widget);
}

} // namespace Utils
