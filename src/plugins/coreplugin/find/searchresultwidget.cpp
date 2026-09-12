// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "searchresultwidget.h"

#include "searchresulttreeview.h"
#include "searchresulttreemodel.h"
#include "searchresulttreeitems.h"

#include "findplugin.h"
#include "itemviewfind.h"
#include "../coreplugintr.h"

#include <utils/qtcassert.h>
#include <utils/theme/theme.h>
#include <utils/fancylineedit.h>
#include <utils/infolabel.h>

#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>

#ifdef WITH_TESTS
#include <QSignalSpy>
#include <QTest>
#endif

static const int SEARCHRESULT_WARNING_LIMIT = 200000;
static const char SIZE_WARNING_ID[] = "sizeWarningLabel";

using namespace Utils;

namespace Core::Internal {

class WideEnoughLineEdit : public Utils::FancyLineEdit
{
    Q_OBJECT

public:
    WideEnoughLineEdit(QWidget *parent) : Utils::FancyLineEdit(parent)
    {
        setFiltering(true);
        setPlaceholderText(QString());
        connect(this, &QLineEdit::textChanged, this, &QLineEdit::updateGeometry);

    }

    QSize sizeHint() const override
    {
        QSize sh = QLineEdit::minimumSizeHint();
        sh.rwidth() += qMax(25 * fontMetrics().horizontalAdvance(QLatin1Char('x')),
                            fontMetrics().horizontalAdvance(text()));
        return sh;
    }
};

SearchResultWidget::SearchResultWidget(QWidget *parent) :
    QWidget(parent)
{
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    setLayout(layout);

    QFrame *topWidget = new QFrame;
    QPalette pal;
    pal.setColor(QPalette::Window,     creatorColor(Theme::InfoBarBackground));
    pal.setColor(QPalette::WindowText, creatorColor(Theme::InfoBarText));
    topWidget->setPalette(pal);
    if (creatorTheme()->flag(Theme::DrawSearchResultWidgetFrame)) {
        topWidget->setFrameStyle(QFrame::Panel | QFrame::Raised);
        topWidget->setLineWidth(1);
    }
    topWidget->setAutoFillBackground(true);
    auto topLayout = new QVBoxLayout(topWidget);
    topLayout->setContentsMargins(2, 2, 2, 2);
    topLayout->setSpacing(2);
    topWidget->setLayout(topLayout);
    layout->addWidget(topWidget);

    auto topFindWidget = new QWidget(topWidget);
    auto topFindLayout = new QHBoxLayout(topFindWidget);
    topFindLayout->setContentsMargins(0, 0, 0, 0);
    topFindWidget->setLayout(topFindLayout);
    topLayout->addWidget(topFindWidget);

    m_topReplaceWidget = new QWidget(topWidget);
    auto topReplaceLayout = new QHBoxLayout(m_topReplaceWidget);
    topReplaceLayout->setContentsMargins(0, 0, 0, 0);
    m_topReplaceWidget->setLayout(topReplaceLayout);
    topLayout->addWidget(m_topReplaceWidget);

    m_messageWidget = new QFrame;
    if (creatorTheme()->flag(Theme::DrawSearchResultWidgetFrame)) {
        m_messageWidget->setFrameStyle(QFrame::Panel | QFrame::Raised);
        m_messageWidget->setLineWidth(1);
    }
    m_messageWidget->setAutoFillBackground(true);
    auto messageLayout = new QHBoxLayout(m_messageWidget);
    messageLayout->setContentsMargins(2, 2, 2, 2);
    m_messageWidget->setLayout(messageLayout);
    m_messageLabel = new InfoLabel;
    m_messageLabel->setType(InfoLabelType::Error);
    m_messageLabel->setFilled(true);
    messageLayout->addWidget(m_messageLabel);
    layout->addWidget(m_messageWidget);
    m_messageWidget->setVisible(false);

    m_searchResultTreeView = new SearchResultTreeView(this);
    connect(m_searchResultTreeView, &SearchResultTreeView::filterInvalidated,
            this, &SearchResultWidget::filterInvalidated);
    connect(m_searchResultTreeView, &SearchResultTreeView::filterChanged,
            this, &SearchResultWidget::filterChanged);

    layout->addWidget(m_searchResultTreeView);

    m_infoBarDisplay.setTarget(layout, 2);
    m_infoBarDisplay.setInfoBar(&m_infoBar);

    m_descriptionContainer = new QWidget(topFindWidget);
    auto descriptionLayout = new QHBoxLayout(m_descriptionContainer);
    m_descriptionContainer->setLayout(descriptionLayout);
    descriptionLayout->setContentsMargins(0, 0, 0, 0);
    m_descriptionContainer->setMinimumWidth(200);
    m_descriptionContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_label = new QLabel(m_descriptionContainer);
    m_label->setVisible(false);
    m_searchTerm = new QLabel(m_descriptionContainer);
    m_searchTerm->setTextFormat(Qt::PlainText);
    m_searchTerm->setVisible(false);
    descriptionLayout->addWidget(m_label);
    descriptionLayout->addWidget(m_searchTerm);
    m_cancelButton = new QToolButton(topFindWidget);
    m_cancelButton->setText(Tr::tr("Cancel"));
    m_cancelButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    connect(m_cancelButton, &QAbstractButton::clicked, this, &SearchResultWidget::cancel);
    m_searchAgainButton = new QToolButton(topFindWidget);
    m_searchAgainButton->setToolTip(Tr::tr("Repeat the search with same parameters."));
    m_searchAgainButton->setText(Tr::tr("&Search Again"));
    m_searchAgainButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_searchAgainButton->setVisible(false);
    connect(m_searchAgainButton, &QAbstractButton::clicked, this, &SearchResultWidget::searchAgain);

    m_replaceLabel = new QLabel(Tr::tr("Repla&ce with:"), m_topReplaceWidget);
    m_replaceTextEdit = new WideEnoughLineEdit(m_topReplaceWidget);
    m_replaceLabel->setBuddy(m_replaceTextEdit);
    m_replaceTextEdit->setMinimumWidth(120);
    setTabOrder(m_replaceTextEdit, m_searchResultTreeView);
    m_preserveCaseCheck = new QCheckBox(m_topReplaceWidget);
    m_preserveCaseCheck->setText(Tr::tr("Preser&ve case"));
    m_preserveCaseCheck->setEnabled(false);
    m_additionalReplaceWidget = new QWidget(m_topReplaceWidget);
    m_additionalReplaceWidget->setVisible(false);
    m_replaceButton = new QToolButton(m_topReplaceWidget);
    m_replaceButton->setToolTip(Tr::tr("Replace all occurrences."));
    m_replaceButton->setText(Tr::tr("&Replace"));
    m_replaceButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_replaceButton->setEnabled(false);

    m_preserveCaseCheck->setChecked(Find::hasFindFlag(FindPreserveCase));
    connect(m_preserveCaseCheck, &QAbstractButton::clicked, Find::instance(), &Find::setPreserveCase);

    m_matchesFoundLabel = new QLabel(topFindWidget);
    updateHeader();

    topFindLayout->addWidget(m_descriptionContainer);
    topFindLayout->addWidget(m_cancelButton);
    topFindLayout->addWidget(m_searchAgainButton);
    topFindLayout->addStretch(2);
    topFindLayout->addWidget(m_matchesFoundLabel);
    topReplaceLayout->addWidget(m_replaceLabel);
    topReplaceLayout->addWidget(m_replaceTextEdit);
    topReplaceLayout->addWidget(m_preserveCaseCheck);
    topReplaceLayout->addWidget(m_additionalReplaceWidget);
    topReplaceLayout->addWidget(m_replaceButton);
    topReplaceLayout->addStretch(2);
    setShowReplaceUI(m_header.supportsReplace());
    setSupportPreserveCase(true);

    connect(&m_header, &SearchResultHeader::changed,
            this, &SearchResultWidget::updateHeader);
    // The two widgets a reader edits are editors of the header's state, not
    // the place it lives: what they are told, they tell back.
    connect(m_replaceTextEdit, &QLineEdit::textChanged, &m_header,
            &SearchResultHeader::setTextToReplace);
    connect(m_preserveCaseCheck, &QCheckBox::toggled, &m_header,
            &SearchResultHeader::setPreserveCaseChecked);
    updateHeader();

    connect(m_searchResultTreeView, &SearchResultTreeView::jumpToSearchResult,
            this, &SearchResultWidget::handleJumpToSearchResult);
    connect(m_replaceTextEdit, &QLineEdit::returnPressed,
            this, &SearchResultWidget::handleReplaceButton);
    connect(m_replaceTextEdit, &QLineEdit::textChanged,
            this, &SearchResultWidget::replaceTextChanged);
    connect(m_replaceButton, &QAbstractButton::clicked,
            this, &SearchResultWidget::handleReplaceButton);

    topFindWidget->setMinimumHeight(m_cancelButton->sizeHint().height());
}

SearchResultWidget::~SearchResultWidget()
{
    if (m_infoBar.containsInfo(Id(SIZE_WARNING_ID)))
        cancelAfterSizeWarning();
}

void SearchResultWidget::setInfo(const QString &label, const QString &toolTip, const QString &term)
{
    m_header.setInfo(label, toolTip, term);
}

QWidget *SearchResultWidget::additionalReplaceWidget() const
{
    return m_additionalReplaceWidget;
}

void SearchResultWidget::setAdditionalReplaceWidget(QWidget *widget)
{
    if (QLayoutItem *item = m_topReplaceWidget->layout()->replaceWidget(m_additionalReplaceWidget,
                                                                        widget))
        delete item;
    delete m_additionalReplaceWidget;
    m_additionalReplaceWidget = widget;
}

void SearchResultWidget::addResults(const SearchResultItems &items, SearchResult::AddMode mode)
{
    bool firstItems = (m_header.matchCount() == 0);
    m_header.addMatches(items.size());
    m_searchResultTreeView->addResults(items, mode);
    updateHeader();
    if (firstItems) {
        if (!m_dontAskAgainGroup.isEmpty()) {
            Id undoWarningId = Id("warninglabel/").withSuffix(m_dontAskAgainGroup);
            if (m_infoBar.canInfoBeAdded(undoWarningId)) {
                InfoBarEntry info(undoWarningId, Tr::tr("This change cannot be undone."),
                                  InfoBarEntry::GlobalSuppression::Enabled);
                m_infoBar.addInfo(info);
            }
        }

        m_searchResultTreeView->selectionModel()
            ->select(m_searchResultTreeView->model()->index(0, 0, QModelIndex()),
                     QItemSelectionModel::Select);
        emit navigateStateChanged();
    } else if (m_header.matchCount() <= SEARCHRESULT_WARNING_LIMIT) {
        return;
    } else {
        Id sizeWarningId(SIZE_WARNING_ID);
        if (!m_infoBar.canInfoBeAdded(sizeWarningId))
            return;
        emit paused(true);
        InfoBarEntry info(sizeWarningId,
                          Tr::tr("The search resulted in more than %n items, do you still want to continue?",
                             nullptr, SEARCHRESULT_WARNING_LIMIT));
        info.setCancelButtonInfo(Tr::tr("Cancel"), [this] { cancelAfterSizeWarning(); });
        info.addCustomButton(Tr::tr("Continue"), [this] { continueAfterSizeWarning(); });
        m_infoBar.addInfo(info);
        emit requestPopup(false/*no focus*/);
    }
}

int SearchResultWidget::count() const
{
    return m_header.matchCount();
}

void SearchResultWidget::setSupportsReplace(bool replaceSupported, const QString &group)
{
    m_header.setSupportsReplace(replaceSupported);
    setShowReplaceUI(replaceSupported);
    m_dontAskAgainGroup = group;
}

bool SearchResultWidget::supportsReplace() const
{
    return m_header.supportsReplace();
}

void SearchResultWidget::setTextToReplace(const QString &textToReplace)
{
    m_header.setTextToReplace(textToReplace);
    m_replaceTextEdit->setText(textToReplace);
    m_replaceTextEdit->selectAll();
}

QString SearchResultWidget::textToReplace() const
{
    return m_header.textToReplace();
}

void SearchResultWidget::setSupportPreserveCase(bool enabled)
{
    m_header.setSupportsPreserveCase(enabled);
}

void SearchResultWidget::setShowReplaceUI(bool visible)
{
    m_searchResultTreeView->model()->setShowReplaceUI(visible);
    m_header.setShowingReplaceUi(visible);
    if (visible)
        m_replaceTextEdit->setFocus();
    else
        m_searchResultTreeView->setFocus();
}

bool SearchResultWidget::hasFocusInternally() const
{
    return m_searchResultTreeView->hasFocus()
           || (m_header.isShowingReplaceUi() && m_replaceTextEdit->hasFocus());
}

void SearchResultWidget::setFocusInternally()
{
    if (!canFocusInternally() || hasFocusInternally())
        return;
    if (m_header.isShowingReplaceUi() && (!focusWidget() || focusWidget() == m_replaceTextEdit))
        m_replaceTextEdit->setFocus();
    else
        m_searchResultTreeView->setFocus();
}

bool SearchResultWidget::canFocusInternally() const
{
    return m_header.isShowingReplaceUi() || m_header.matchCount() > 0;
}

void SearchResultWidget::notifyVisibilityChanged(bool visible)
{
    emit visibilityChanged(visible);
}

void SearchResultWidget::setTextEditorFont(const QFont &font, const SearchResultColors &colors)
{
    m_searchResultTreeView->setTextEditorFont(font, colors);
}

void SearchResultWidget::setTabWidth(int tabWidth)
{
    m_searchResultTreeView->setTabWidth(tabWidth);
}

void SearchResultWidget::setAutoExpandResults(bool expand)
{
    m_searchResultTreeView->setAutoExpandResults(expand);
}

void SearchResultWidget::setRelativePaths(bool relative)
{
    m_searchResultTreeView->setRelativePaths(relative);
}

void SearchResultWidget::expandAll()
{
    m_searchResultTreeView->expandAll();
}

void SearchResultWidget::collapseAll()
{
    m_searchResultTreeView->collapseAll();
}

void SearchResultWidget::goToNext()
{
    if (m_header.matchCount() == 0)
        return;
    QModelIndex idx = m_searchResultTreeView->model()->next(m_searchResultTreeView->currentIndex());
    if (idx.isValid()) {
        m_searchResultTreeView->setCurrentIndex(idx);
        m_searchResultTreeView->emitJumpToSearchResult(idx);
    }
}

void SearchResultWidget::goToPrevious()
{
    if (!m_searchResultTreeView->model()->rowCount())
        return;
    QModelIndex idx = m_searchResultTreeView->model()->prev(m_searchResultTreeView->currentIndex());
    if (idx.isValid()) {
        m_searchResultTreeView->setCurrentIndex(idx);
        m_searchResultTreeView->emitJumpToSearchResult(idx);
    }
}

void SearchResultWidget::restart()
{
    m_searchResultTreeView->clear();
    Id sizeWarningId(SIZE_WARNING_ID);
    m_infoBar.removeInfo(sizeWarningId);
    m_infoBar.unsuppressInfo(sizeWarningId);
    m_header.startSearch();
    emit restarted();
}

void SearchResultWidget::setSearchAgainSupported(bool supported)
{
    m_header.setSearchAgainSupported(supported);
}

void SearchResultWidget::setSearchAgainEnabled(bool enabled)
{
    m_searchAgainButton->setEnabled(enabled);
}

void SearchResultWidget::setFilter(SearchResultFilter *filter)
{
    m_searchResultTreeView->setFilter(filter);
}

bool SearchResultWidget::hasFilter() const
{
    return m_searchResultTreeView->hasFilter();
}

void SearchResultWidget::showFilterWidget(QWidget *parent)
{
    m_searchResultTreeView->showFilterWidget(parent);
}

void SearchResultWidget::setReplaceEnabled(bool enabled)
{
    m_replaceButton->setEnabled(enabled);
}

void SearchResultWidget::finishSearch(bool canceled, const QString &reason)
{
    Id sizeWarningId(SIZE_WARNING_ID);
    m_infoBar.removeInfo(sizeWarningId);
    m_infoBar.unsuppressInfo(sizeWarningId);
    m_header.finishSearch(canceled, reason);
}

void SearchResultWidget::sendRequestPopup()
{
    emit requestPopup(true/*focus*/);
}

void SearchResultWidget::continueAfterSizeWarning()
{
    m_infoBar.suppressInfo(Id(SIZE_WARNING_ID));
    emit paused(false);
}

void SearchResultWidget::cancelAfterSizeWarning()
{
    m_infoBar.suppressInfo(Id(SIZE_WARNING_ID));
    emit canceled();
    emit paused(false);
}

void SearchResultWidget::handleJumpToSearchResult(const SearchResultItem &item)
{
    emit activated(item);
}

void SearchResultWidget::handleReplaceButton()
{
    // check if button is actually enabled, because this is also triggered
    // by pressing return in replace line edit
    if (m_replaceButton->isEnabled())
        doReplace();
}

void SearchResultWidget::doReplace()
{
    m_infoBar.clear();
    setShowReplaceUI(false);
    emit replaceButtonClicked(m_header.textToReplace(), items(true), m_header.preserveCase());
}

void SearchResultWidget::cancel()
{
    m_header.requestCancel();
    if (m_infoBar.containsInfo(Id(SIZE_WARNING_ID)))
        cancelAfterSizeWarning();
    else
        emit canceled();
}

void SearchResultWidget::searchAgain()
{
    emit searchAgainRequested();
}

SearchResultItems SearchResultWidget::items(bool checkedOnly) const
{
    SearchResultItems result;
    SearchResultFilterModel *model = m_searchResultTreeView->model();
    const int fileCount = model->rowCount();
    for (int i = 0; i < fileCount; ++i) {
        const QModelIndex fileIndex = model->index(i, 0);
        const int itemCount = model->rowCount(fileIndex);
        for (int rowIndex = 0; rowIndex < itemCount; ++rowIndex) {
            const QModelIndex textIndex = model->index(rowIndex, 0, fileIndex);
            const SearchResultTreeItem * const rowItem = model->itemForIndex(textIndex);
            QTC_ASSERT(rowItem != nullptr, continue);
            if (!checkedOnly || rowItem->checkState())
                result << rowItem->item;
        }
    }
    return result;
}

QString SearchResultHeader::matchesFound() const
{
    if (m_count > 0)
        return Tr::tr("%n matches found.", nullptr, m_count);
    return m_searching ? Tr::tr("Searching...") : Tr::tr("No matches found.");
}

void SearchResultHeader::setInfo(const QString &label, const QString &toolTip,
                                 const QString &term)
{
    if (m_label == label && m_toolTip == toolTip && m_term == term)
        return;
    m_label = label;
    m_toolTip = toolTip;
    m_term = term;
    emit changed();
}

void SearchResultHeader::setSupportsReplace(bool supported)
{
    if (m_replaceSupported == supported)
        return;
    m_replaceSupported = supported;
    emit changed();
}

void SearchResultHeader::setShowingReplaceUi(bool showing)
{
    if (m_showingReplaceUi == showing)
        return;
    m_showingReplaceUi = showing;
    emit changed();
}

void SearchResultHeader::setTextToReplace(const QString &text)
{
    if (m_textToReplace == text)
        return;
    m_textToReplace = text;
    emit changed();
}

void SearchResultHeader::setSupportsPreserveCase(bool supported)
{
    if (m_preserveCaseSupported == supported)
        return;
    m_preserveCaseSupported = supported;
    emit changed();
}

void SearchResultHeader::setPreserveCaseChecked(bool checked)
{
    if (m_preserveCaseChecked == checked)
        return;
    m_preserveCaseChecked = checked;
    emit changed();
}

void SearchResultHeader::requestCancel()
{
    if (m_cancelRequested)
        return;
    m_cancelRequested = true;
    emit changed();
}

void SearchResultHeader::setSearchAgainSupported(bool supported)
{
    if (m_searchAgainSupported == supported)
        return;
    m_searchAgainSupported = supported;
    emit changed();
}

void SearchResultHeader::startSearch()
{
    m_searching = true;
    m_cancelRequested = false;
    m_count = 0;
    m_message.clear();
    emit changed();
}

void SearchResultHeader::addMatches(int count)
{
    if (count <= 0)
        return;
    m_count += count;
    emit changed();
}

void SearchResultHeader::finishSearch(bool canceled, const QString &reason)
{
    m_searching = false;
    // A search that was stopped says so; one that ran out of things to look
    // at has nothing to add to the count.
    m_message = canceled ? (reason.isEmpty() ? Tr::tr("Search was canceled.") : reason)
                         : QString();
    emit changed();
}

// The one place the header's answers reach the widgets that draw them.
void SearchResultWidget::updateHeader()
{
    m_matchesFoundLabel->setText(m_header.matchesFound());
    m_label->setText(m_header.label());
    m_label->setVisible(!m_header.label().isEmpty());
    m_descriptionContainer->setToolTip(m_header.toolTip());
    m_searchTerm->setText(m_header.term());
    m_searchTerm->setVisible(!m_header.term().isEmpty());
    m_topReplaceWidget->setVisible(m_header.isShowingReplaceUi());
    m_preserveCaseCheck->setVisible(m_header.supportsPreserveCase());
    m_cancelButton->setVisible(m_header.canCancel());
    m_searchAgainButton->setVisible(m_header.canSearchAgain());
    m_replaceButton->setEnabled(m_header.canReplace());
    m_preserveCaseCheck->setEnabled(m_header.canReplace());
    if (!m_header.message().isEmpty())
        m_messageLabel->setText(m_header.message());
    m_messageWidget->setVisible(!m_header.message().isEmpty());
}

#ifdef WITH_TESTS

class SearchResultHeaderTest final : public QObject
{
    Q_OBJECT

private slots:
    void testWhatTheHeaderSaysAboutTheSearch()
    {
        SearchResultHeader header;
        header.startSearch();
        QVERIFY(header.isSearching());
        QCOMPARE(header.matchesFound(), Tr::tr("Searching..."));

        header.addMatches(3);
        QCOMPARE(header.matchCount(), 3);
        // Found something, and still looking: the count wins over the verb,
        // because it is the count a reader is waiting for.
        QVERIFY(header.isSearching());
        QCOMPARE(header.matchesFound(), Tr::tr("%n matches found.", nullptr, 3));

        header.finishSearch(false, {});
        QVERIFY(!header.isSearching());
        QCOMPARE(header.matchesFound(), Tr::tr("%n matches found.", nullptr, 3));

        header.startSearch();
        header.finishSearch(false, {});
        QCOMPARE(header.matchCount(), 0);
        QCOMPARE(header.matchesFound(), Tr::tr("No matches found."));
    }

    void testSearchAgainIsOfferedOnlyWhenTheSearchHasStopped()
    {
        // The rule used to be written three times, and one of them asked the
        // Cancel button whether it was visible - so what could be repeated
        // depended on what had been drawn.
        SearchResultHeader header;
        header.setSearchAgainSupported(true);
        header.startSearch();
        QVERIFY2(!header.canSearchAgain(), "a running search offers to be run again");
        QVERIFY2(header.canCancel(), "a running search cannot be cancelled");

        header.finishSearch(false, {});
        QVERIFY2(header.canSearchAgain(), "a finished search cannot be repeated");
        QVERIFY2(!header.canCancel(), "a finished search still offers to be cancelled");

        // And a filter that does not support it is never offered it.
        SearchResultHeader without;
        without.startSearch();
        without.finishSearch(false, {});
        QVERIFY(!without.canSearchAgain());
    }

    void testOnlyACancelledSearchHasSomethingToSay()
    {
        SearchResultHeader header;
        header.startSearch();
        header.finishSearch(false, {});
        QVERIFY2(header.message().isEmpty(), "a search that simply ended reported a problem");

        header.startSearch();
        header.finishSearch(true, {});
        QCOMPARE(header.message(), Tr::tr("Search was canceled."));

        // A caller with a better reason than the default gets to give it.
        header.startSearch();
        header.finishSearch(true, QString("Too many files."));
        QCOMPARE(header.message(), QString("Too many files."));

        // And starting again clears it, or the row keeps explaining a search
        // that is no longer the one on screen.
        header.startSearch();
        QVERIFY2(header.message().isEmpty(), "the last search's reason outlived it");
    }

    void testReplaceWaitsForSomethingToReplace()
    {
        SearchResultHeader header;
        header.startSearch();
        QVERIFY(!header.canReplace());
        header.addMatches(1);
        QVERIFY(header.canReplace());
        header.startSearch();
        QVERIFY2(!header.canReplace(), "a new search kept the last one's matches");
    }

    void testWhatTheRowSaysThisSearchIs()
    {
        // Written straight to four widgets and kept nowhere, so nothing could
        // ask afterwards what was being looked for.
        SearchResultHeader header;
        QVERIFY(header.label().isEmpty());
        QVERIFY(header.term().isEmpty());

        header.setInfo("Files in File System", "Searching /src for thing", "thing");
        QCOMPARE(header.label(), QString("Files in File System"));
        QCOMPARE(header.toolTip(), QString("Searching /src for thing"));
        QCOMPARE(header.term(), QString("thing"));
    }

    void testWhatAReplaceAsksForComesFromTheRowRatherThanItsWidgets()
    {
        // The text lived in the QLineEdit drawing it and the tick in the check
        // box beside it, and whether the tick counted was decided where it was
        // read rather than where it was kept.
        SearchResultHeader header;
        header.setSupportsReplace(true);
        header.setTextToReplace("after");
        QCOMPARE(header.textToReplace(), QString("after"));

        header.setSupportsPreserveCase(true);
        header.setPreserveCaseChecked(true);
        QVERIFY2(header.preserveCase(), "a ticked box on a filter that supports it does not count");

        // Ticked, but the filter cannot do it: the tick does not count, and
        // the row does not offer it either.
        header.setSupportsPreserveCase(false);
        QVERIFY2(!header.preserveCase(),
                 "a filter that cannot preserve case was asked to anyway");
        QVERIFY(!header.supportsPreserveCase());
    }

    void testCancellingIsAskedForOnce()
    {
        // Cancelling used to hide the Cancel button directly, so the row and
        // the button disagreed until the search actually stopped - and the
        // next thing to redraw the row brought the button back.
        SearchResultHeader header;
        header.startSearch();
        QVERIFY(header.canCancel());

        header.requestCancel();
        QVERIFY2(!header.canCancel(), "a search being cancelled offers to be cancelled again");
        QVERIFY2(header.isSearching(), "the search stopped before anything said it had");

        // Still nothing to repeat until it really ends.
        header.setSearchAgainSupported(true);
        QVERIFY(!header.canSearchAgain());
        header.finishSearch(true, {});
        QVERIFY(header.canSearchAgain());

        // And the next search can be cancelled like any other.
        header.startSearch();
        QVERIFY2(header.canCancel(), "the last search's cancel outlived it");
    }

    void testTypingIntoTheReplaceFieldReachesWhatAReplaceWillUse()
    {
        // The two widgets a reader edits are editors of the row's state. If
        // they do not tell it back, a replace uses whatever was last set
        // programmatically and quietly ignores what was typed.
        SearchResultWidget widget;
        const QList<QLineEdit *> edits = widget.findChildren<QLineEdit *>();
        QCOMPARE(edits.size(), 1);
        const QList<QCheckBox *> boxes = widget.findChildren<QCheckBox *>();
        QCOMPARE(boxes.size(), 1);

        widget.setSupportsReplace(true, {});
        widget.setSupportPreserveCase(true);

        edits.first()->setText("typed");
        QCOMPARE(widget.textToReplace(), QString("typed"));

        boxes.first()->setChecked(true);
        QVERIFY2(widget.headerForTesting().preserveCase(),
                 "ticking the box beside the field reaches nothing");
    }

    void testTheRowIsToldWheneverAnyOfThatChanges()
    {
        // The widgets are redrawn from one slot, so anything that changes an
        // answer above has to say so or the row goes stale.
        SearchResultHeader header;
        QSignalSpy changes(&header, &SearchResultHeader::changed);
        header.startSearch();
        QCOMPARE(changes.size(), 1);
        header.addMatches(2);
        QCOMPARE(changes.size(), 2);
        header.setSearchAgainSupported(true);
        QCOMPARE(changes.size(), 3);
        header.finishSearch(true, {});
        QCOMPARE(changes.size(), 4);
        // Nothing changed, so nothing is said.
        header.setSearchAgainSupported(true);
        QCOMPARE(changes.size(), 4);
        header.addMatches(0);
        QCOMPARE(changes.size(), 4);
    }
};

QObject *createSearchResultHeaderTest()
{
    return new SearchResultHeaderTest;
}

#endif // WITH_TESTS

} // namespace Core::Internal

#include "searchresultwidget.moc"
