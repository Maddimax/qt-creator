// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "core_global.h"
#include "outputtasksink.h"
#include "outputwindow.h"

#include <utils/filepath.h>
#include <utils/outputformat.h>
#include <utils/outputformatter.h>
#include <utils/storekey.h>

#include <QColor>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
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

    // Queued rather than written straight away, as the widget does: output can
    // arrive faster than it can be drawn, and a pane that writes every chunk
    // the moment it arrives stops responding while a build runs.
    void appendMessage(const QString &text, Utils::OutputFormat format);

    // Writes what is waiting. Declines while the backlog is large and does it
    // when that has drained instead, so asking to flush cannot itself be the
    // thing that blocks.
    void flush();

    // Forgets what is waiting and starts the formatter again, for a pane about
    // to show a different run.
    void reset();

    // Whether output that cannot be kept up with is thrown away rather than
    // queued for a minute. Off by default: the widget does not touch a pane's
    // output unless it is asked to.
    void setDiscardExcessiveOutput(bool discard);

    // Dims what the last run left, so the next one stands out from it.
    void grayOutOldContent();

    void clear();

    // Retracts lines already printed - the progress lines a build system
    // overwrites as it goes.
    void clearLinesPrefixedWith(const QString &prefix, bool deleteTrailingLineBreak);

    void setFilter(const QString &text, OutputWindow::FilterModeFlags mode,
                   int before = 0, int after = 0);

    // The same, as an output pane describes it: every pane has these six
    // values from IOutputPane and would otherwise each convert them itself.
    void setFilter(const QString &text, Qt::CaseSensitivity caseSensitivity, bool regexp,
                   bool inverted, int before, int after);

    // Which of its lines a task was reported from, and showing them when the
    // task is clicked in the Issues pane.
    void registerPositionOf(unsigned taskId, int linkedOutputLines, int skipLines,
                            int offset = 0,
                            TaskSource source = TaskSource::Direct) override;
    bool knowsPositionOf(unsigned taskId) const;
    void showPositionOf(unsigned taskId);

    // How much output is kept. Beyond it the oldest lines go, and a single
    // chunk larger than the whole allowance is elided in the middle. The
    // widget applied this by default, so a pane that never asks for one still
    // gets it.
    void setMaxCharCount(qsizetype count);
    qsizetype maxCharCount() const;

    void setWordWrapEnabled(bool enabled);

    // Also given to the formatter, which needs to know what it is drawing on
    // to decide whether the colours it was told to use are readable there.
    void setBackgroundColor(const QColor &color);

    void setBaseFont(const QFont &font);
    void setWheelZoomEnabled(bool enabled);
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void setFontZoom(float zoom);
    float fontZoom() const;

    // What a file of this pane's output is called by default, and what a
    // scratch buffer of it is named after.
    void setOutputFileNameHint(const QString &fileName);

    // Writes everything the pane holds to \a file. Separate from the menu
    // entry that asks where, so that what it writes can be checked without a
    // file dialog.
    Utils::Result<> saveContentsTo(const Utils::FilePath &file) const;

    // Opens the pane's output in a temporary editor, for reading it with
    // everything an editor has.
    void copyContentsToScratchBuffer() const;

    // The name pattern a scratch buffer of \a outputFileNameHint's output
    // gets. Its own function because the fallback matters: a hint with no base
    // name would otherwise produce a file called "-XXXXXX.txt".
    static QString scratchBufferNameTemplate(const QString &outputFileNameHint);

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

#ifdef WITH_TESTS
    // Writes one chunk now, the way the queue timer does. A test drives the
    // queue by hand rather than waiting for it, so that what it asserts is the
    // pacing and not the clock.
    void writeNextChunkForTest();
#endif

signals:
    // Output was thrown away because it was arriving faster than it could be
    // drawn. A pane reports this, so that what is missing is not a mystery.
    void outputDiscarded();

private:
    void refilter();
    bool isFiltering() const;
    void showEvent(QShowEvent *event) override;
    void writeNextChunk();
    void writeChunk(const QString &text, Utils::OutputFormat format, bool chunkWasSplit);
    void discardPendingOutput();
    qsizetype totalQueued(const std::function<qsizetype(const QString &)> &measure) const;

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
    qsizetype m_maxCharCount = 0;

    // Set when the source loses leading blocks to the limit. The filtered copy
    // follows the source by block number, and those all shift when the front
    // of it goes.
    bool m_sourceTrimmed = false;

    // What has arrived but not yet been drawn.
    QList<QPair<QString, Utils::OutputFormat>> m_queuedOutput;
    QTimer m_queueTimer;
    qsizetype m_chunkSize = 10000;
    int m_formatterCalls = 0;
    bool m_flushRequested = false;
    bool m_discardExcessiveOutput = false;
    bool m_wordWrapEnabled = false;
    QColor m_backgroundColor;
    QString m_outputFileNameHint;
    OutputWindow::PendingOutputState m_pendingState;

    const Utils::Key m_zoomSettingsKey;
};

#ifdef WITH_TESTS
QObject *createOutputPaneViewTest();
#endif

} // namespace Core
