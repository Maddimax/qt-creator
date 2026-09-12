// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "searchresultwindow.h"

#include <utils/infobar.h>
#include <utils/searchresultitem.h>

#include <QWidget>

QT_BEGIN_NAMESPACE
class QFrame;
class QLabel;
class QLineEdit;
class QToolButton;
class QCheckBox;
QT_END_NAMESPACE

namespace Utils { class InfoLabel; }
namespace Core {

namespace Internal {
class SearchResultTreeView;

// What the row above the results says about the search: how far it has got,
// how many matches it found, and which of the buttons beside it apply. A
// widget cannot hold it, because the rules below are written once here and
// were written three times - once by reading another button's visibility -
// when each of them lived on the widget that drew it.
class SearchResultHeader : public QObject
{
    Q_OBJECT

public:
    bool isSearching() const { return m_searching; }
    int matchCount() const { return m_count; }

    // One sentence with three forms, rather than three places that each know
    // part of when to say which.
    QString matchesFound() const;

    // A search that is running can be cancelled and cannot be repeated; one
    // that has finished is the other way round.
    bool canCancel() const { return m_searching && !m_cancelRequested; }
    bool canSearchAgain() const { return m_searchAgainSupported && !m_searching; }
    bool canReplace() const { return m_count > 0; }

    // What went wrong, where anything did. Empty is the ordinary case.
    QString message() const { return m_message; }

    // What this search is: the filter's own description, what it was looking
    // for, and the longer form for a tooltip. Written to four widgets and
    // kept nowhere before, so nothing could ask afterwards.
    QString label() const { return m_label; }
    QString toolTip() const { return m_toolTip; }
    QString term() const { return m_term; }
    void setInfo(const QString &label, const QString &toolTip, const QString &term);

    // The replace row. What is typed into it lived in the QLineEdit drawing
    // it, and whether to preserve case was read from the check box beside it
    // and gated on support at the point of reading.
    bool supportsReplace() const { return m_replaceSupported; }
    void setSupportsReplace(bool supported);
    bool isShowingReplaceUi() const { return m_showingReplaceUi; }
    void setShowingReplaceUi(bool showing);
    QString textToReplace() const { return m_textToReplace; }
    void setTextToReplace(const QString &text);
    bool supportsPreserveCase() const { return m_preserveCaseSupported; }
    void setSupportsPreserveCase(bool supported);
    bool preserveCaseChecked() const { return m_preserveCaseChecked; }
    void setPreserveCaseChecked(bool checked);
    // Which is what a replace actually asks for.
    bool preserveCase() const { return m_preserveCaseSupported && m_preserveCaseChecked; }

    void setSearchAgainSupported(bool supported);

    // Asked for once: a search being stopped cannot be stopped again, and the
    // button went away by being hidden directly before this was state.
    void requestCancel();

    void startSearch();
    void addMatches(int count);
    void finishSearch(bool canceled, const QString &reason);

signals:
    void changed();

private:
    bool m_searching = true;
    bool m_searchAgainSupported = false;
    bool m_cancelRequested = false;
    bool m_replaceSupported = false;
    bool m_showingReplaceUi = false;
    bool m_preserveCaseSupported = true;
    bool m_preserveCaseChecked = false;
    int m_count = 0;
    QString m_message;
    QString m_label;
    QString m_toolTip;
    QString m_term;
    QString m_textToReplace;
};

#ifdef WITH_TESTS
QObject *createSearchResultHeaderTest();
#endif

class SearchResultWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SearchResultWidget(QWidget *parent = nullptr);
    ~SearchResultWidget() override;

    void setInfo(const QString &label, const QString &toolTip, const QString &term);
    QWidget *additionalReplaceWidget() const;
    void setAdditionalReplaceWidget(QWidget *widget);

    void addResults(const Utils::SearchResultItems &items, SearchResult::AddMode mode);

    int count() const;
    bool isSearching() const { return m_header.isSearching(); }

    void setSupportsReplace(bool replaceSupported, const QString &group);
    bool supportsReplace() const;
    void triggerReplace() { doReplace(); }

    void setTextToReplace(const QString &textToReplace);
    QString textToReplace() const;
    void setSupportPreserveCase(bool enabled);

    bool hasFocusInternally() const;
    void setFocusInternally();
    bool canFocusInternally() const;

    void notifyVisibilityChanged(bool visible);

    void setTextEditorFont(const QFont &font, const Utils::SearchResultColors &colors);
    void setTabWidth(int tabWidth);

    void setAutoExpandResults(bool expand);
    void setRelativePaths(bool relative);
    void expandAll();
    void collapseAll();

    void goToNext();
    void goToPrevious();

    void restart();

    void setSearchAgainSupported(bool supported);
    void setSearchAgainEnabled(bool enabled);
    void setFilter(SearchResultFilter *filter);
    bool hasFilter() const;
    void showFilterWidget(QWidget *parent);
    void setReplaceEnabled(bool enabled);
    Utils::SearchResultItems items(bool checkedOnly) const;

#ifdef WITH_TESTS
    const SearchResultHeader &headerForTesting() const { return m_header; }
#endif

public slots:
    void finishSearch(bool canceled, const QString &reason);
    void sendRequestPopup();

signals:
    void activated(const Utils::SearchResultItem &item);
    void replaceButtonClicked(const QString &replaceText,
                              const Utils::SearchResultItems &checkedItems, bool preserveCase);
    void replaceTextChanged(const QString &replaceText);
    void searchAgainRequested();
    void canceled();
    void paused(bool paused);
    void restarted();
    void visibilityChanged(bool visible);
    void requestPopup(bool focus);
    void filterInvalidated();
    void filterChanged();

    void navigateStateChanged();

private:
    void handleJumpToSearchResult(const Utils::SearchResultItem &item);
    void handleReplaceButton();
    void doReplace();
    void cancel();
    void searchAgain();

    void setShowReplaceUI(bool visible);
    void continueAfterSizeWarning();
    void cancelAfterSizeWarning();

    void updateHeader();

    SearchResultTreeView *m_searchResultTreeView = nullptr;
    SearchResultHeader m_header;
    QString m_dontAskAgainGroup;
    QFrame *m_messageWidget = nullptr;
    Utils::InfoBar m_infoBar;
    Utils::InfoBarDisplay m_infoBarDisplay;
    QWidget *m_topReplaceWidget = nullptr;
    QLabel *m_replaceLabel = nullptr;
    QLineEdit *m_replaceTextEdit = nullptr;
    QToolButton *m_replaceButton = nullptr;
    QToolButton *m_searchAgainButton = nullptr;
    QCheckBox *m_preserveCaseCheck = nullptr;
    QWidget *m_additionalReplaceWidget = nullptr;
    QWidget *m_descriptionContainer = nullptr;
    QLabel *m_label = nullptr;
    QLabel *m_searchTerm = nullptr;
    Utils::InfoLabel *m_messageLabel = nullptr;
    QToolButton *m_cancelButton = nullptr;
    QLabel *m_matchesFoundLabel = nullptr;
};

} // Internal
} // Find
