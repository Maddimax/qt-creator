// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "searchresultwidget.h"

#include "searchresulttreeitemroles.h"

#include <coreplugin/inavigationwidgetfactory.h>

#include <utils/environment.h>

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

SearchResultRow::SearchResultRow(SearchResultHeader *header, SearchResultWidget *widget,
                                 QObject *parent)
    : QObject(parent)
    , m_header(header)
    , m_widget(widget)
{
    connect(m_header, &SearchResultHeader::changed, this, &SearchResultRow::changed);
}

QString SearchResultRow::label() const { return m_header->label(); }
QString SearchResultRow::term() const { return m_header->term(); }
QString SearchResultRow::description() const { return m_header->toolTip(); }
QString SearchResultRow::matchesFound() const { return m_header->matchesFound(); }
bool SearchResultRow::canCancel() const { return m_header->canCancel(); }
bool SearchResultRow::canSearchAgain() const { return m_header->canSearchAgain(); }
bool SearchResultRow::canReplace() const { return m_header->canReplace(); }
bool SearchResultRow::showingReplaceUi() const { return m_header->isShowingReplaceUi(); }
QString SearchResultRow::textToReplace() const { return m_header->textToReplace(); }
void SearchResultRow::setTextToReplace(const QString &text) { m_header->setTextToReplace(text); }
bool SearchResultRow::supportsPreserveCase() const { return m_header->supportsPreserveCase(); }
bool SearchResultRow::preserveCaseChecked() const { return m_header->preserveCaseChecked(); }

void SearchResultRow::setPreserveCaseChecked(bool checked)
{
    m_header->setPreserveCaseChecked(checked);
}

bool SearchResultRow::hasAdditionalOption() const { return m_header->hasAdditionalOption(); }
QString SearchResultRow::additionalOptionLabel() const { return m_header->additionalOptionLabel(); }

QString SearchResultRow::additionalOptionToolTip() const
{
    return m_header->additionalOptionToolTip();
}

bool SearchResultRow::additionalOptionChecked() const
{
    return m_header->additionalOptionChecked();
}

void SearchResultRow::setAdditionalOptionChecked(bool checked)
{
    m_header->setAdditionalOptionChecked(checked);
}

QString SearchResultRow::additionalNote() const { return m_header->additionalNote(); }

void SearchResultRow::cancel()
{
    m_widget->cancelSearch();
}

void SearchResultRow::searchAgain()
{
    m_widget->requestSearchAgain();
}

void SearchResultRow::replace()
{
    m_widget->triggerReplace();
}

SearchResultWidget::SearchResultWidget(QWidget *parent) :
    QWidget(parent)
{
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    setLayout(layout);

    // The Qt Quick row draws the whole strip from the header below, and is
    // what a reader gets. QTC_WIDGET_SEARCH_RESULTS asks for the widgets, and
    // a build with no front end to host QML gets them without asking.
    m_quickRow = nullptr;
    if (!Utils::qtcEnvironmentVariableIsSet("QTC_WIDGET_SEARCH_RESULTS")
        && Core::hasQmlViewFactory()) {
        auto * const row = new SearchResultRow(&m_header, this);
        m_quickRow = Core::createQmlView(
            QUrl("qrc:/qt/qml/QtCreator/Core/SearchResultsRow.qml"), row,
            Core::QmlViewSizing::SizeToScene);
        if (!m_quickRow)
            delete row;
    }

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
    topWidget->setVisible(!m_quickRow);
    auto topLayout = new QVBoxLayout(topWidget);
    topLayout->setContentsMargins(2, 2, 2, 2);
    topLayout->setSpacing(2);
    topWidget->setLayout(topLayout);
    if (m_quickRow)
        layout->addWidget(m_quickRow);
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
    m_preserveCaseCheck->setObjectName("preserveCase");
    m_preserveCaseCheck->setText(Tr::tr("Preser&ve case"));
    m_preserveCaseCheck->setEnabled(false);
    m_additionalNoteLabel = new Utils::InfoLabel({}, Utils::InfoLabelType::Information,
                                                 m_topReplaceWidget);
    m_additionalOptionCheck = new QCheckBox(m_topReplaceWidget);
    m_additionalOptionCheck->setObjectName("additionalReplaceOption");
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
    topReplaceLayout->addWidget(m_additionalNoteLabel);
    topReplaceLayout->addWidget(m_additionalOptionCheck);
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
    connect(m_additionalOptionCheck, &QCheckBox::toggled, &m_header,
            &SearchResultHeader::setAdditionalOptionChecked);
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

void SearchResultWidget::setAdditionalReplaceOption(const QString &label,
                                                    const QString &toolTip)
{
    m_header.setAdditionalOption(label, toolTip);
}

bool SearchResultWidget::additionalReplaceOptionChecked() const
{
    return m_header.additionalOptionChecked();
}

void SearchResultWidget::setAdditionalReplaceNote(const QString &note)
{
    m_header.setAdditionalNote(note);
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

#ifdef WITH_TESTS
QAbstractItemModel *SearchResultWidget::resultsModelForTesting() const
{
    return m_searchResultTreeView->model();
}
#endif

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

void SearchResultHeader::setAdditionalOption(const QString &label, const QString &toolTip)
{
    if (m_additionalOptionLabel == label && m_additionalOptionToolTip == toolTip)
        return;
    m_additionalOptionLabel = label;
    m_additionalOptionToolTip = toolTip;
    emit changed();
}

void SearchResultHeader::setAdditionalOptionChecked(bool checked)
{
    if (m_additionalOptionChecked == checked)
        return;
    m_additionalOptionChecked = checked;
    emit changed();
}

void SearchResultHeader::setAdditionalNote(const QString &note)
{
    if (m_additionalNote == note)
        return;
    m_additionalNote = note;
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
    m_additionalOptionCheck->setText(m_header.additionalOptionLabel());
    m_additionalOptionCheck->setToolTip(m_header.additionalOptionToolTip());
    m_additionalOptionCheck->setVisible(m_header.hasAdditionalOption());
    m_additionalNoteLabel->setText(m_header.additionalNote());
    m_additionalNoteLabel->setVisible(!m_header.additionalNote().isEmpty());
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
        auto * const preserveCase = widget.findChild<QCheckBox *>("preserveCase");
        QVERIFY(preserveCase);

        widget.setSupportsReplace(true, {});
        widget.setSupportPreserveCase(true);

        edits.first()->setText("typed");
        QCOMPARE(widget.textToReplace(), QString("typed"));

        preserveCase->setChecked(true);
        QVERIFY2(widget.headerForTesting().preserveCase(),
                 "ticking the box beside the field reaches nothing");

        // And the extra option the caller named, which is the one that used
        // to be a QWidget handed in from another plugin.
        auto * const extra = widget.findChild<QCheckBox *>("additionalReplaceOption");
        QVERIFY(extra);
        // isHidden(), not isVisibleTo(): the whole widget strip is hidden
        // when the Qt Quick row is the one on screen, so what is asked here
        // is whether this box was hidden in its own right.
        QVERIFY2(extra->isHidden(), "an option nobody named is drawn anyway");
        widget.setAdditionalReplaceOption("Rename 2 files", "a.cpp\nb.cpp");
        QCOMPARE(extra->text(), QString("Rename 2 files"));
        QVERIFY2(!extra->isHidden(), "the option was named and is not drawn");
        extra->setChecked(true);
        QVERIFY2(widget.additionalReplaceOptionChecked(),
                 "ticking the extra option reaches nothing");
    }

    void testTheExtraThingAReplaceCanBeAskedToDo()
    {
        // Three callers used to build a QCheckBox, hand it over as a QWidget
        // and cast it back to read one bool. The row carries it now.
        SearchResultHeader header;
        QVERIFY2(!header.hasAdditionalOption(), "a search offers a nameless extra option");
        QVERIFY2(!header.additionalOptionChecked(),
                 "an option nobody asked for is answered yes");

        header.setAdditionalOption("Rename 3 files", "a.cpp\nb.cpp\nc.cpp");
        QVERIFY(header.hasAdditionalOption());
        QCOMPARE(header.additionalOptionLabel(), QString("Rename 3 files"));
        QCOMPARE(header.additionalOptionToolTip(), QString("a.cpp\nb.cpp\nc.cpp"));
        QVERIFY2(!header.additionalOptionChecked(), "the option starts out answered yes");

        header.setAdditionalOptionChecked(true);
        QVERIFY(header.additionalOptionChecked());

        // Taken away with nothing left ticked behind it: the next search must
        // not rename files because the last one was told to.
        header.setAdditionalOption({}, {});
        QVERIFY(!header.hasAdditionalOption());
        QVERIFY2(!header.additionalOptionChecked(),
                 "an option that is no longer offered is still answered yes");
    }

    void testTheNoteBesideTheReplaceRow()
    {
        SearchResultHeader header;
        QVERIFY(header.additionalNote().isEmpty());
        header.setAdditionalNote("Search Again to update results");
        QCOMPARE(header.additionalNote(), QString("Search Again to update results"));
        header.setAdditionalNote({});
        QVERIFY(header.additionalNote().isEmpty());
    }

    void testTheQuickRowReadsTheSameHeaderAndCanActOnIt()
    {
        // The controller is a view of the header, not a copy of it: what the
        // search says has to arrive without the row being rebuilt, and what
        // the row is told has to reach the search.
        SearchResultWidget widget;
        SearchResultHeader header;
        SearchResultRow row(&header, &widget);

        QSignalSpy changes(&row, &SearchResultRow::changed);
        header.startSearch();
        QVERIFY2(!changes.isEmpty(), "the row hears nothing the search says");
        QCOMPARE(row.matchesFound(), header.matchesFound());
        QVERIFY(row.canCancel());
        QVERIFY(!row.canSearchAgain());

        header.setInfo("Files in File System", "Searching /src", "needle");
        QCOMPARE(row.label(), QString("Files in File System"));
        QCOMPARE(row.term(), QString("needle"));
        QCOMPARE(row.description(), QString("Searching /src"));

        header.setAdditionalOption("Rename 2 files", "a.cpp\nb.cpp");
        QVERIFY(row.hasAdditionalOption());
        QCOMPARE(row.additionalOptionLabel(), QString("Rename 2 files"));

        // And the other way: a reader typing in the row reaches the header
        // that a replace will be read from.
        row.setTextToReplace("after");
        QCOMPARE(header.textToReplace(), QString("after"));
        row.setAdditionalOptionChecked(true);
        QVERIFY(header.additionalOptionChecked());
        row.setPreserveCaseChecked(true);
        QVERIFY(header.preserveCaseChecked());
    }

    void testWhatAResultRowDrawsIsTheModelsAnswer()
    {
        // The delegate used to cut the line into before/match/after and expand
        // tabs in each piece, so the offsets it was given were into the raw
        // line and only meant anything to a painter that did the same.
        SearchResultWidget widget;
        widget.setTabWidth(4);

        Utils::SearchResultItem item;
        item.setFilePath(Utils::FilePath::fromString("/src/a.cpp"));
        // One tab, then the match: drawn, the match starts four columns in,
        // not one.
        item.setLineText("\tneedle here");
        item.setMainRange(1, 1, 6);
        widget.addResults({item}, SearchResult::AddOrdered);

        QAbstractItemModel * const model = widget.resultsModelForTesting();
        QVERIFY(model);
        QModelIndex row = model->index(0, 0);
        QVERIFY(row.isValid());
        // A file group on top, with the match under it.
        if (model->rowCount(row) > 0)
            row = model->index(0, 0, row);
        QVERIFY(row.isValid());

        QCOMPARE(row.data(ItemDataRoles::DrawnTextRole).toString(), QString("    needle here"));
        QCOMPARE(row.data(ItemDataRoles::DrawnHighlightStartRole).toInt(), 4);
        QCOMPARE(row.data(ItemDataRoles::DrawnHighlightLengthRole).toInt(), 6);

        // A wider tab moves it further along, and the rows are told.
        QSignalSpy redrawn(model, &QAbstractItemModel::dataChanged);
        widget.setTabWidth(8);
        QCOMPARE(row.data(ItemDataRoles::DrawnHighlightStartRole).toInt(), 8);
        QVERIFY2(!redrawn.isEmpty(), "the tab width changed and no row was told to redraw");
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
