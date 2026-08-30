// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "core_global.h"
#include "outputtasksink.h"
#include "outputwindow.h"

#include <utils/outputformat.h>
#include <utils/outputformatter.h>
#include <utils/storekey.h>

#include <QTextCursor>
#include <QTextDocument>
#include <QWidget>

namespace Core {

class OutputView;

// The text side of an output pane, drawn with Qt Quick: what a pane writes
// into, what a filter lets through, and the view showing whichever applies.
//
// Two documents rather than one with lines switched off, because a Qt Quick
// text item ignores QTextBlock::setVisible() - so a filtered view here means a
// filtered document.
//
// Holds no actions and knows nothing about a pane's tool bar, so it can be
// built as often as a test likes.
class CORE_EXPORT OutputPaneView : public QWidget, public OutputTaskSink
{
    Q_OBJECT

public:
    // \a zoomSettingsKey, when given, is where the zoom is remembered across
    // runs of Qt Creator.
    explicit OutputPaneView(const Utils::Key &zoomSettingsKey = {}, QWidget *parent = nullptr);
    ~OutputPaneView() override;

    void appendMessage(const QString &text, Utils::OutputFormat format);

    // Dims what the last run left, so the next one stands out from it.
    void grayOutOldContent();

    void clear();

    // Retracts lines already printed - the progress lines a build system
    // overwrites as it goes.
    void clearLinesPrefixedWith(const QString &prefix, bool deleteTrailingLineBreak);

    void setFilter(const QString &text, OutputWindow::FilterModeFlags mode,
                   int before = 0, int after = 0);

    // Which of its lines a task was reported from, and showing them when the
    // task is clicked in the Issues pane.
    void registerPositionOf(unsigned taskId, int linkedOutputLines, int skipLines,
                            int offset = 0,
                            TaskSource source = TaskSource::Direct) override;
    bool knowsPositionOf(unsigned taskId) const;
    void showPositionOf(unsigned taskId);

    void setBaseFont(const QFont &font);
    void setWheelZoomEnabled(bool enabled);
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void setFontZoom(float zoom);
    float fontZoom() const;

    // Everything written, filter or no filter: what a reader asking a pane for
    // its text wants, which is not what happens to be on screen.
    QString toPlainText() const;

    // What is shown: the source document, or the filtered one while a filter
    // is set.
    QTextDocument *shownDocument() const;

    // For a pane that gives its output line parsers.
    Utils::OutputFormatter *formatter() { return &m_formatter; }

    // Creating it if there is a front end to create it with. Null before one
    // has loaded, which is a state a Core-level pane really is in: General
    // Messages is built the first time anything writes a message, and that can
    // be while plugins are still initializing.
    OutputView *view();

private:
    void refilter();
    bool isFiltering() const;
    void showEvent(QShowEvent *event) override;

    OutputView *m_view = nullptr;

    // Wanted before there was a view to want it of, applied when one appears.
    QFont m_baseFont;
    bool m_baseFontSet = false;
    float m_zoom = 0;
    bool m_wheelZoomEnabled = true;

    QTextDocument m_source;
    QTextDocument m_filtered;
    Utils::OutputFormatter m_formatter;
    QTextCursor m_startOfNewContent;
    OutputWindow::FilteredAppendState m_appendState;
    QString m_filterText;
    OutputWindow::FilterModeFlags m_filterMode;
    int m_beforeContext = 0;
    int m_afterContext = 0;
    QHash<unsigned, QPair<int, int>> m_taskPositions;
    const Utils::Key m_zoomSettingsKey;
};

#ifdef WITH_TESTS
QObject *createOutputPaneViewTest();
#endif

} // namespace Core
