// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "core_global.h"
#include "icontext.h"

#include <utils/outputformat.h>
#include <utils/storekey.h>

#include <QPlainTextEdit>

QT_BEGIN_NAMESPACE
class QPalette;
QT_END_NAMESPACE

namespace Utils {
class OutputFormatter;
class OutputLineParser;
}

namespace Core {

class IFindSupport;
namespace Internal { class OutputWindowPrivate; }

class CORE_EXPORT OutputWindow : public QPlainTextEdit
{
    Q_OBJECT

public:
    enum class FilterModeFlag {
        Default       = 0x00, // Plain text, non case sensitive, for initialization
        RegExp        = 0x01,
        CaseSensitive = 0x02,
        Inverted      = 0x04,
    };
    Q_DECLARE_FLAGS(FilterModeFlags, FilterModeFlag)

    // What a line has to satisfy to stay visible under \a filterText. Kept out
    // of the class because it is a decision about text, not about a widget:
    // inside one it could only be exercised by typing into a pane.
    // What a line has to satisfy to stay visible under a filter.
    using TextMatchingFunction = std::function<bool(const QString &text)>;

    static TextMatchingFunction filterPredicate(const QString &filterText,
                                                FilterModeFlags mode);

    // The lines a filter reveals around what it matched: \a before above each
    // match and \a after below, within a document of \a lineCount lines. The
    // matches themselves are not in it - they are visible already. Also a
    // decision about text: the widget expressed it by asking a document for
    // block numbers that may not exist and letting the invalid ones do
    // nothing, which is a clamp nobody could see.
    static QList<int> contextLines(const QList<int> &matchedLines, int lineCount,
                                   int before, int after);

    // What is shown of a chunk too long to show: the first and last half of
    // what the limit allows, with a note between them saying how much went.
    static QString elideChunk(const QString &chunk, qsizetype maxCharCount);

    // How many blocks a document may keep for \a incomingChars more characters
    // to fit within \a maxCharCount, given what it holds now. -1 where nothing
    // has to go, which is what QPlainTextEdit reads as "no limit".
    static int blocksToKeep(const QList<int> &blockLengths, qsizetype existingChars,
                            qsizetype incomingChars, qsizetype maxCharCount);

    // Fills \a target with the lines of \a source that \a matches accepts, plus
    // \a before and \a after lines of context, keeping how each was drawn.
    //
    // A Qt Quick view cannot be filtered the way a QPlainTextEdit is: it
    // ignores QTextBlock::setVisible(), so a filtered view means a filtered
    // document rather than one with parts switched off.
    static void copyFiltered(const QTextDocument *source, QTextDocument *target,
                             const TextMatchingFunction &matches, int before, int after);

    // How far a filtered document has followed the one it is built from.
    struct FilteredAppendState
    {
        int lastConsidered = -1; // The last source block looked at.
        int lastEmitted = -1;    // The last source block copied over.
        int afterRemaining = 0;  // Context lines still owed after a match.
    };

    // Copies the lines \a source has gained since \a state last saw it. A
    // rebuild costs the whole document, which a build appending thousands of
    // times cannot pay; this pays only for what arrived.
    //
    // A line that arrives later can make an earlier skipped one into context,
    // so this is not simply "copy what matches": the lines owed *before* a new
    // match are copied with it.
    static void appendFiltered(const QTextDocument *source, QTextDocument *target,
                               const TextMatchingFunction &matches, int before, int after,
                               FilteredAppendState &state);

    // Dims the output a run left behind, so that what the next one appends
    // stands out from it. \a startOfNewContent marks where the run being dimmed
    // began, and is moved to where the next one will.
    //
    // It is a cursor rather than a position because the document is also cut
    // from the front when the character limit bites, which would leave a
    // position pointing at a line that is no longer the one meant.
    //
    // The palette is passed in because the dimmed colour is halfway between
    // the text and the background, and a Qt Quick view has no widget palette
    // to read that from.
    static void grayOutContentBefore(QTextCursor &startOfNewContent, const QPalette &palette);

    // Removes every line starting with \a prefix. What lets a pane retract
    // what it said - the progress lines a build system overwrites.
    static void removeLinesPrefixedWith(QTextDocument *document, const QString &prefix,
                                        bool deleteTrailingLineBreak);

    OutputWindow(Context context, const Utils::Key &settingsKey, QWidget *parent = nullptr);
    ~OutputWindow() override;

    void setLineParsers(const QList<Utils::OutputLineParser *> &parsers);
    Utils::OutputFormatter *outputFormatter() const;

    void appendMessage(const QString &out, Utils::OutputFormat format);

    enum class TaskSource { Direct, Parsed };
    void registerPositionOf(
        unsigned taskId, int linkedOutputLines, int skipLines, int offset, TaskSource taskSource);
    bool knowsPositionOf(unsigned taskId) const;
    void showPositionOf(unsigned taskId);

    void grayOutOldContent();
    void clear();
    void clearLinesPrefixedWith(const QString& prefix, bool deleteTrailingLineBreak);
    void flush();
    void reset();

    void scrollToBottom();

    void setMaxCharCount(qsizetype count);
    qsizetype maxCharCount() const;

    void setBaseFont(const QFont &newFont);
    float fontZoom() const;
    void setFontZoom(float zoom);
    void resetZoom() { setFontZoom(0); }
    void setWheelZoomEnabled(bool enabled);

    bool updateFilterProperties(
        const QString &filterText,
        Qt::CaseSensitivity caseSensitivity,
        bool regexp,
        bool isInverted,
        int beforeContext,
        int afterContext);

    void setOutputFileNameHint(const QString &fileName);

    IFindSupport *findSupport() const;

    void filterNewContent();

signals:
    void wheelZoom();
    void outputDiscarded();
    void cleanOldOutput();

public slots:
    void setWordWrapEnabled(bool wrap);
    void setDiscardExcessiveOutput(bool discard);

protected:
    OutputWindow(
        Context context, const Utils::Key &settingsKey, bool aggregateFindSupport, QWidget *parent);

    virtual void handleLink(const QPoint &pos);
    virtual void adaptContextMenu(QMenu *menu, const QPoint &pos);

    virtual TextMatchingFunction makeMatchingFilterFunction() const;
    void resetLastFilteredBlockNumber();

    virtual bool shouldFilterNewContentOnBlockCountChanged() const;

private:
    QMimeData *createMimeDataFromSelection() const override;
    void keyPressEvent(QKeyEvent *ev) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void showEvent(QShowEvent *) override;
    void wheelEvent(QWheelEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

    using QPlainTextEdit::setFont; // call setBaseFont instead, which respects the zoom factor
    void enableUndoRedo();
    void handleNextOutputChunk();

    enum class ChunkCompleteness { Complete, Split };
    void handleOutputChunk(
        const QString &output, Utils::OutputFormat format, ChunkCompleteness completeness);

    void discardExcessiveOutput();
    void discardPendingToolOutput();
    void updateAutoScroll();
    qsizetype totalQueuedSize() const;
    qsizetype totalQueuedLines() const;

    Internal::OutputWindowPrivate *d = nullptr;
};

#ifdef WITH_TESTS
QObject *createOutputFilterTest();
#endif

} // namespace Core
