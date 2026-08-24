// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "aspects.h"
#include "groupedselection.h"

namespace Utils {

class GroupedModel;

// A list of items in named groups - auto-detected ones and the user's - with
// one of them current and Clone, Remove and Make Default acting on it. The
// aspect owns neither the model nor what it holds: a page hands over the
// GroupedModel it already had.
//
// Everything a view needs is here rather than in the view, so that a QTreeView
// and a Qt Quick TreeView show the same thing and answer the same way. See
// GroupedSelection.
class QTCREATOR_UTILS_EXPORT GroupedListAspect : public BaseAspect
{
    Q_OBJECT

    Q_PROPERTY(int currentRow READ currentRow WRITE setCurrentRow NOTIFY currentRowChanged)
    Q_PROPERTY(bool canRemove READ canRemove NOTIFY actionsChanged)
    Q_PROPERTY(bool canClone READ canClone NOTIFY actionsChanged)
    Q_PROPERTY(bool canMakeDefault READ canMakeDefault NOTIFY actionsChanged)
    // Removal is undone rather than done twice, so a view labels the button
    // "Restore" while this is true.
    Q_PROPERTY(bool currentIsRemoved READ currentIsRemoved NOTIFY actionsChanged)

public:
    explicit GroupedListAspect(AspectContainer *container = nullptr);
    ~GroupedListAspect() override;

    AspectPresentation presentation() const override;

    // The model whose items are listed. Not owned; it outlives the page.
    void setModel(GroupedModel *model);
    GroupedModel *model() const;
    GroupedSelection *selection() const;

    // The tree a view shows: the groups, with the items under them. Rows in
    // every other function here are the model's own, which a view maps to and
    // from with rowForIndex() and indexForRow().
    Q_INVOKABLE QAbstractItemModel *displayModel() const;
    Q_INVOKABLE int rowForIndex(const QModelIndex &displayIndex) const;
    Q_INVOKABLE QModelIndex indexForRow(int row) const;

    int currentRow() const;
    void setCurrentRow(int row);

    bool canRemove() const;
    bool canClone() const;
    bool canMakeDefault() const;
    bool currentIsRemoved() const;

    Q_INVOKABLE void removeCurrent();
    Q_INVOKABLE void cloneCurrent();
    Q_INVOKABLE void makeCurrentDefault();

    void setCanRemoveRow(std::function<bool(int)> predicate);
    void setCanCloneRow(std::function<bool(int)> predicate);

    // Whether one of the items is the default one. The model decides that at
    // construction and keeps it to itself, so the page says so here.
    void setShowsDefault(bool on);

    void addToLayoutImpl(Layouting::Layout &parent) override;

signals:
    void currentRowChanged(int oldRow, int newRow);
    void actionsChanged();
    void currentRemoved();
    void currentCloned();

private:
    GroupedModel *m_model = nullptr;
    std::unique_ptr<GroupedSelection> m_selection;
    std::function<bool(int)> m_canRemove;
    std::function<bool(int)> m_canClone;
    bool m_showsDefault = false;
};

} // namespace Utils
