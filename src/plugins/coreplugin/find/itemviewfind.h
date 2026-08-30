// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "ifindsupport.h"

QT_BEGIN_NAMESPACE
class QAbstractItemModel;
class QAbstractItemView;
class QWidget;
class QFrame;
class QModelIndex;
QT_END_NAMESPACE

namespace Core {
class ItemModelFindPrivate;

// What searching an item view needs of the view: the model, where the current
// row is, moving it, and bringing it into sight. A QAbstractItemView answers
// all four; so does a Qt Quick view, through the pane that holds it - which is
// why this is an interface rather than a widget pointer.
class CORE_EXPORT ItemViewTarget
{
public:
    virtual ~ItemViewTarget();

    virtual QAbstractItemModel *model() const = 0;
    virtual QModelIndex currentIndex() const = 0;
    virtual void setCurrentIndex(const QModelIndex &index) = 0;

    // Scroll to it, and open whatever it is inside, so that a match found off
    // screen or inside a collapsed row is a match the reader can see.
    virtual void reveal(const QModelIndex &index) = 0;

    // Where to show "the search wrapped", and what a searchable wrapper wraps.
    virtual QWidget *widget() const = 0;
};

class CORE_EXPORT ItemViewFind : public IFindSupport
{
    Q_OBJECT
public:
    enum FetchOption {
        DoNotFetchMoreWhileSearching,
        FetchMoreWhileSearching
    };

    enum ColorOption {
        DarkColored = 0,
        LightColored = 1
    };

    explicit ItemViewFind(QAbstractItemView *view, int role = Qt::DisplayRole,
            FetchOption option = DoNotFetchMoreWhileSearching);

    // The same search over anything that can answer for a view. Takes
    // ownership of \a target.
    explicit ItemViewFind(ItemViewTarget *target, int role = Qt::DisplayRole,
                          FetchOption option = DoNotFetchMoreWhileSearching);
    ~ItemViewFind() override;

    bool supportsReplace() const override;
    Utils::FindFlags supportedFindFlags() const override;
    void resetIncrementalSearch() override;
    void clearHighlights() override;
    QString currentFindString() const override;
    QString completedFindString() const override;

    void highlightAll(const QString &txt, Utils::FindFlags findFlags) override;
    Result findIncremental(const QString &txt, Utils::FindFlags findFlags) override;
    Result findStep(const QString &txt, Utils::FindFlags findFlags) override;

    static QFrame *createSearchableWrapper(QAbstractItemView *treeView, ColorOption colorOption = DarkColored,
                                           FetchOption option = DoNotFetchMoreWhileSearching);
    static QFrame *createSearchableWrapper(ItemViewFind *finder, ColorOption colorOption = DarkColored);

private:
    Result find(const QString &txt, Utils::FindFlags findFlags,
                bool startFromCurrentIndex, bool *wrapped);
    QModelIndex nextIndex(const QModelIndex &idx, bool *wrapped) const;
    QModelIndex prevIndex(const QModelIndex &idx, bool *wrapped) const;
    QModelIndex followingIndex(const QModelIndex &idx, bool backward,
                               bool *wrapped);

private:
    ItemModelFindPrivate *d;
};

#ifdef WITH_TESTS
QObject *createItemViewFindTest();
#endif

} // namespace Core
