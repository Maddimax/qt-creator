// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "itemviewfind.h"

#include "../findplaceholder.h"

#include <utils/aggregate.h>

#include <QModelIndex>
#include <QTextCursor>
#include <QTreeView>
#include <QVBoxLayout>

#ifdef WITH_TESTS
#include <QStandardItemModel>
#include <QTest>
#endif

using namespace Utils;

namespace Core {

/*!
    \class Core::ItemViewFind
    \inmodule QtCreator
    \internal
*/

// A widget view, answering for itself.
class WidgetViewTarget : public ItemViewTarget
{
public:
    explicit WidgetViewTarget(QAbstractItemView *view)
        : m_view(view)
    {}

    QAbstractItemModel *model() const override { return m_view->model(); }
    QModelIndex currentIndex() const override { return m_view->currentIndex(); }
    void setCurrentIndex(const QModelIndex &index) override { m_view->setCurrentIndex(index); }
    void reveal(const QModelIndex &index) override
    {
        m_view->scrollTo(index);
        // A match inside a collapsed row is not on screen however far the view
        // scrolls, so opening its parent is part of revealing it.
        if (index.parent().isValid()) {
            if (auto treeView = qobject_cast<QTreeView *>(m_view))
                treeView->expand(index.parent());
        }
    }

    QWidget *widget() const override { return m_view; }

private:
    QAbstractItemView * const m_view;
};

class ItemModelFindPrivate
{
public:
    explicit ItemModelFindPrivate(ItemViewTarget *target, int role, ItemViewFind::FetchOption option)
        : m_view(target),
          m_incrementalWrappedState(false),
          m_role(role),
          m_option(option)
    {
    }

    ~ItemModelFindPrivate() { delete m_view; }

    ItemViewTarget *m_view;
    QModelIndex m_incrementalFindStart;
    bool m_incrementalWrappedState;
    int m_role;
    ItemViewFind::FetchOption m_option;
};

ItemViewTarget::~ItemViewTarget() = default;

ItemViewFind::ItemViewFind(QAbstractItemView *view, int role, FetchOption option)
    : d(new ItemModelFindPrivate(new WidgetViewTarget(view), role, option))
{
}

ItemViewFind::ItemViewFind(ItemViewTarget *target, int role, FetchOption option)
    : d(new ItemModelFindPrivate(target, role, option))
{
}

ItemViewFind::~ItemViewFind()
{
    delete d;
}

bool ItemViewFind::supportsReplace() const
{
    return false;
}

FindFlags ItemViewFind::supportedFindFlags() const
{
    return FindBackward | FindCaseSensitively | FindRegularExpression | FindWholeWords;
}

void ItemViewFind::resetIncrementalSearch()
{
    d->m_incrementalFindStart = QModelIndex();
    d->m_incrementalWrappedState = false;
}

void ItemViewFind::clearHighlights()
{
}

QString ItemViewFind::currentFindString() const
{
    return QString();
}

QString ItemViewFind::completedFindString() const
{
    return QString();
}

void ItemViewFind::highlightAll(const QString &/*txt*/, FindFlags /*findFlags*/)
{
}

IFindSupport::Result ItemViewFind::findIncremental(const QString &txt, FindFlags findFlags)
{
    if (!d->m_incrementalFindStart.isValid()) {
        d->m_incrementalFindStart = d->m_view->currentIndex();
        d->m_incrementalWrappedState = false;
    }
    d->m_view->setCurrentIndex(d->m_incrementalFindStart);
    bool wrapped = false;
    IFindSupport::Result result = find(txt, findFlags, true/*startFromCurrent*/,
                                       &wrapped);
    if (wrapped != d->m_incrementalWrappedState) {
        d->m_incrementalWrappedState = wrapped;
        showWrapIndicator(d->m_view->widget());
    }
    return result;
}

IFindSupport::Result ItemViewFind::findStep(const QString &txt, FindFlags findFlags)
{
    bool wrapped = false;
    IFindSupport::Result result = find(txt, findFlags, false/*startFromNext*/,
                                       &wrapped);
    if (wrapped)
        showWrapIndicator(d->m_view->widget());
    if (result == IFindSupport::Found) {
        d->m_incrementalFindStart = d->m_view->currentIndex();
        d->m_incrementalWrappedState = false;
    }
    return result;
}

// Takes a widget rather than a view: what is wrapped only has to be something
// a find bar can sit under, which a Qt Quick view is too.
static QFrame *createHelper(QWidget *view,
                            ItemViewFind::ColorOption colorOption,
                            ItemViewFind *finder)
{
    auto widget = new QFrame;
    widget->setFrameStyle(QFrame::NoFrame);

    auto placeHolder = new FindToolBarPlaceHolder(widget);
    placeHolder->setLightColored(colorOption);

    auto vbox = new QVBoxLayout(widget);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);
    vbox->addWidget(view);
    vbox->addWidget(placeHolder);

    Aggregation::aggregate({view, finder});

    return widget;
}

QFrame *ItemViewFind::createSearchableWrapper(QAbstractItemView *treeView,
                                              ColorOption colorOption,
                                              FetchOption option)
{
    return createHelper(treeView, colorOption, new ItemViewFind(treeView, Qt::DisplayRole, option));
}

QFrame *ItemViewFind::createSearchableWrapper(ItemViewFind *finder,
                                              ItemViewFind::ColorOption colorOption)
{
    return createHelper(finder->d->m_view->widget(), colorOption, finder);
}

IFindSupport::Result ItemViewFind::find(const QString &searchTxt,
                                        FindFlags findFlags,
                                        bool startFromCurrentIndex,
                                        bool *wrapped)
{
    if (wrapped)
        *wrapped = false;
    if (searchTxt.isEmpty())
        return IFindSupport::NotFound;
    if (d->m_view->model()->rowCount() <= 0) // empty model
        return IFindSupport::NotFound;

    QModelIndex currentIndex = d->m_view->currentIndex();
    if (!currentIndex.isValid()) // nothing selected, start from top
        currentIndex = d->m_view->model()->index(0, 0);
    QTextDocument::FindFlags flags = Utils::textDocumentFlagsForFindFlags(findFlags);
    QModelIndex resultIndex;
    QModelIndex index = currentIndex;
    int currentRow = currentIndex.row();

    bool sensitive = (findFlags & FindCaseSensitively);
    QRegularExpression searchExpr;
    if (findFlags & FindRegularExpression) {
        searchExpr = QRegularExpression(searchTxt,
                                        (sensitive ? QRegularExpression::NoPatternOption :
                                                     QRegularExpression::CaseInsensitiveOption));
    } else if (findFlags & FindWholeWords) {
        const QString escapedSearchText = QRegularExpression::escape(searchTxt);
        const QString wordBoundary = QLatin1String("\b");
        searchExpr = QRegularExpression(wordBoundary + escapedSearchText + wordBoundary,
                                        (sensitive ? QRegularExpression::NoPatternOption :
                                                     QRegularExpression::CaseInsensitiveOption));
    } else {
        searchExpr = QRegularExpression(QRegularExpression::escape(searchTxt),
                                        (sensitive ? QRegularExpression::NoPatternOption :
                                                     QRegularExpression::CaseInsensitiveOption));
    }


    bool backward = (flags & QTextDocument::FindBackward);
    if (wrapped)
        *wrapped = false;
    bool anyWrapped = false;
    bool stepWrapped = false;
    if (!startFromCurrentIndex)
        index = followingIndex(index, backward, &stepWrapped);
    else
        currentRow = -1;
    do {
        anyWrapped |= stepWrapped; // update wrapped state if we actually stepped to next/prev item
        if (index.isValid()) {
            const QString &text = d->m_view->model()->data(
                        index, d->m_role).toString();
            if (d->m_view->model()->flags(index) & Qt::ItemIsSelectable
                    && (index.row() != currentRow || index.parent() != currentIndex.parent())
                    && text.indexOf(searchExpr) != -1) {
                resultIndex = index;
                break;
            }
        }
        index = followingIndex(index, backward, &stepWrapped);
        if (index == currentIndex) { // we're back where we started
            if (d->m_view->model()->data(index, d->m_role).toString().indexOf(searchExpr) != -1)
                resultIndex = index;
            break;
        }
    } while (index.isValid());

    if (resultIndex.isValid()) {
        d->m_view->setCurrentIndex(resultIndex);
        d->m_view->reveal(resultIndex);
        if (wrapped)
            *wrapped = anyWrapped;
        return IFindSupport::Found;
    }
    return IFindSupport::NotFound;
}

QModelIndex ItemViewFind::nextIndex(const QModelIndex &idx, bool *wrapped) const
{
    if (wrapped)
        *wrapped = false;
    QAbstractItemModel *model = d->m_view->model();
    // pathological
    if (!idx.isValid())
        return model->index(0, 0);

    // same parent has more columns, go to next column
    const QModelIndex parentIdx = idx.parent();
    if (idx.column() + 1 < model->columnCount(parentIdx))
        return model->index(idx.row(), idx.column() + 1, parentIdx);

    // tree views have their children attached to first column
    // make sure we are at first column
    QModelIndex current = model->index(idx.row(), 0, parentIdx);

    // check for children
    if (d->m_option == FetchMoreWhileSearching && model->canFetchMore(current))
        model->fetchMore(current);
    if (model->rowCount(current) > 0)
        return model->index(0, 0, current);

    // no more children, go up and look for parent with more children
    QModelIndex nextIndex;
    while (!nextIndex.isValid()) {
        int row = current.row();
        current = current.parent();

        if (d->m_option == FetchMoreWhileSearching && model->canFetchMore(current))
            model->fetchMore(current);
        if (row + 1 < model->rowCount(current)) {
            // Same parent has another child
            nextIndex = model->index(row + 1, 0, current);
        } else {
            // go up one parent
            if (!current.isValid()) {
                // we start from the beginning
                if (wrapped)
                    *wrapped = true;
                nextIndex = model->index(0, 0);
            }
        }
    }
    return nextIndex;
}

QModelIndex ItemViewFind::prevIndex(const QModelIndex &idx, bool *wrapped) const
{
    if (wrapped)
        *wrapped = false;
    QAbstractItemModel *model = d->m_view->model();
    // if same parent has earlier columns, just move there
    if (idx.column() > 0)
        return model->index(idx.row(), idx.column() - 1, idx.parent());

    QModelIndex current = idx;
    bool checkForChildren = true;
    if (current.isValid()) {
        int row = current.row();
        if (row > 0) {
            current = model->index(row - 1, 0, current.parent());
        } else {
            current = current.parent();
            checkForChildren = !current.isValid();
            if (checkForChildren && wrapped) {
                // we start from the end
                *wrapped = true;
            }
        }
    }
    if (checkForChildren) {
        // traverse down the hierarchy
        if (d->m_option == FetchMoreWhileSearching && model->canFetchMore(current))
            model->fetchMore(current);
        while (int rc = model->rowCount(current)) {
            current = model->index(rc - 1, 0, current);
        }
    }
    // set to last column
    current = model->index(current.row(), model->columnCount(current.parent()) - 1, current.parent());
    return current;
}

QModelIndex ItemViewFind::followingIndex(const QModelIndex &idx, bool backward, bool *wrapped)
{
    if (backward)
        return prevIndex(idx, wrapped);
    return nextIndex(idx, wrapped);
}

#ifdef WITH_TESTS

namespace {

// A target with no view behind it at all: a model, a current index, and a note
// of what it was asked to reveal. Which is the point - searching an item view
// never needed a widget, only these.
class StandInTarget : public ItemViewTarget
{
public:
    explicit StandInTarget(QAbstractItemModel *model)
        : m_model(model)
    {}

    QAbstractItemModel *model() const override { return m_model; }
    QModelIndex currentIndex() const override { return m_current; }
    void setCurrentIndex(const QModelIndex &index) override { m_current = index; }
    void reveal(const QModelIndex &index) override { revealed << index; }
    QWidget *widget() const override { return nullptr; }

    QList<QModelIndex> revealed;

private:
    QAbstractItemModel * const m_model;
    QPersistentModelIndex m_current;
};

} // namespace

class ItemViewFindTest final : public QObject
{
    Q_OBJECT

private slots:
    void testSearchingAListWithoutAView()
    {
        QStandardItemModel model;
        for (const QString &text : QStringList{"alpha", "beta", "gamma", "Beta again"})
            model.appendRow(new QStandardItem(text));

        auto * const target = new StandInTarget(&model);
        ItemViewFind find(target);

        // Found from the top, and the row it found is the current one.
        QCOMPARE(find.findStep("beta", {}), IFindSupport::Found);
        QCOMPARE(target->currentIndex().data().toString(), QString("beta"));

        // Case-blind unless asked, so the next step reaches the other one.
        QCOMPARE(find.findStep("beta", {}), IFindSupport::Found);
        QCOMPARE(target->currentIndex().data().toString(), QString("Beta again"));

        // Asked, it does not.
        target->setCurrentIndex({});
        QCOMPARE(find.findStep("Beta again", Utils::FindCaseSensitively),
                 IFindSupport::Found);
        QCOMPARE(find.findStep("BETA", Utils::FindCaseSensitively), IFindSupport::NotFound);

        // And what is not there is not found, rather than the search settling
        // on whatever was current.
        QCOMPARE(find.findStep("delta", {}), IFindSupport::NotFound);
    }

    void testSearchingWrapsAndRevealsWhatItFound()
    {
        QStandardItemModel model;
        for (const QString &text : QStringList{"one", "two", "three"})
            model.appendRow(new QStandardItem(text));

        auto * const target = new StandInTarget(&model);
        ItemViewFind find(target);

        QCOMPARE(find.findStep("three", {}), IFindSupport::Found);
        QCOMPARE(target->currentIndex().row(), 2);

        // Past the end it starts again rather than stopping: a search that
        // reached the bottom and gave up would hide the match above.
        QCOMPARE(find.findStep("one", {}), IFindSupport::Found);
        QCOMPARE(target->currentIndex().row(), 0);

        // Everything it found, it asked to be shown. A match the reader cannot
        // see is not a match as far as they are concerned.
        QCOMPARE(target->revealed.size(), 2);
        QCOMPARE(target->revealed.last().row(), 0);
    }

    void testSearchingBackwards()
    {
        QStandardItemModel model;
        for (const QString &text : QStringList{"match", "filler", "match"})
            model.appendRow(new QStandardItem(text));

        auto * const target = new StandInTarget(&model);
        ItemViewFind find(target);

        // A step looks from *after* where it is, so from the top it passes the
        // first row: findStep is "find the next one", and the row you are on
        // is not the next one.
        QCOMPARE(find.findStep("match", {}), IFindSupport::Found);
        QCOMPARE(target->currentIndex().row(), 2);

        QCOMPARE(find.findStep("match", Utils::FindBackward), IFindSupport::Found);
        QCOMPARE(target->currentIndex().row(), 0);
    }
};

QObject *createItemViewFindTest()
{
    return new ItemViewFindTest;
}

#endif // WITH_TESTS

} // namespace Core

#ifdef WITH_TESTS
#include "itemviewfind.moc"
#endif
