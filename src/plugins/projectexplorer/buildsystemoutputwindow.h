// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "projectexplorer_export.h"

#include <coreplugin/outputwindow.h>

#include <utils/outputformat.h>
#include <utils/outputformatter.h>

#include <QAction>
#include <QPointer>
#include <QTextCursor>
#include <QTextDocument>
#include <QWidget>

namespace Core { class OutputView; }
namespace Utils { class FancyLineEdit; }

namespace ProjectExplorer::Internal {

// The Build System Output in Projects mode, drawn with Qt Quick.
//
// Two documents: what was written, and what a filter lets through. The view is
// shown whichever of the two applies, because filtering a Qt Quick view means
// a filtered document rather than one with lines switched off - a Quick text
// item ignores QTextBlock::setVisible().
//
// Holds no actions and registers no commands, so that it can be built more
// than once. BuildSystemOutputWindow below is the one that can not.
class PROJECTEXPLORER_EXPORT BuildSystemOutputView : public QWidget
{
    Q_OBJECT

public:
    explicit BuildSystemOutputView(QWidget *parent = nullptr);

    void appendMessage(const QString &text, Utils::OutputFormat format);

    // Dims what the last run left, so the next one stands out from it.
    void grayOutOldContent();

    void clear();

    // Retracts lines already printed - the progress lines a build system
    // overwrites as it goes.
    void clearLinesPrefixedWith(const QString &prefix, bool deleteTrailingLineBreak);

    void setFilter(const QString &text, Core::OutputWindow::FilterModeFlags mode);

    void setBaseFont(const QFont &font);
    void zoomIn();
    void zoomOut();

    // What is on screen: the source document, or the filtered one while a
    // filter is set. Public for testing, and because find operates on it.
    QTextDocument *shownDocument() const;

    Core::OutputView *view() const { return m_view; }

private:
    void refilter();
    bool isFiltering() const;

    Core::OutputView *m_view = nullptr;
    QTextDocument m_source;
    QTextDocument m_filtered;
    Utils::OutputFormatter m_formatter;
    QTextCursor m_startOfNewContent;
    Core::OutputWindow::FilteredAppendState m_appendState;
    QString m_filterText;
    Core::OutputWindow::FilterModeFlags m_filterMode;
};

// The view plus the commands that drive it: clear, the filter line edit and
// its three options, and the two zoom steps. Registers actions under fixed
// ids, so there is exactly one of these.
class BuildSystemOutputWindow : public QWidget
{
    Q_OBJECT

public:
    BuildSystemOutputWindow();

    QWidget *toolBar();

    void appendMessage(const QString &text, Utils::OutputFormat format);
    void grayOutOldContent();
    void clearLinesPrefixedWith(const QString &prefix, bool deleteTrailingLineBreak);

private:
    void updateFilter();

    BuildSystemOutputView *m_view = nullptr;
    QPointer<QWidget> m_toolBar;
    QPointer<Utils::FancyLineEdit> m_filterOutputLineEdit;
    QAction m_clear;
    QAction m_filterActionRegexp;
    QAction m_filterActionCaseSensitive;
    QAction m_invertFilterAction;
    QAction m_zoomIn;
    QAction m_zoomOut;
};

#ifdef WITH_TESTS
QObject *createBuildSystemOutputTest();
#endif

} // namespace ProjectExplorer::Internal
