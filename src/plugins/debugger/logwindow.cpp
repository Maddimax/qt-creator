// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "logwindow.h"

#include "debuggeractions.h"
#include "debuggerengine.h"
#include "debuggericons.h"
#include "debuggerinternalconstants.h"
#include "debuggertr.h"

#include <QDebug>

#ifdef WITH_TESTS
#include <QTest>
#endif
#include <QTextLayout>
#include <QTime>

#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSyntaxHighlighter>
#include <QToolButton>

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/findplaceholder.h>
#include <coreplugin/minisplitter.h>
#include <coreplugin/outputpaneview.h>
#include <coreplugin/outputview.h>
#include <coreplugin/find/basetextfind.h>

#include <utils/filedialogs.h>
#include <utils/aggregate.h>
#include <utils/fancylineedit.h>
#include <utils/fileutils.h>
#include <utils/theme/theme.h>

using namespace Utils;

namespace Debugger::Internal {

GlobalLogWindow *theGlobalLog = nullptr;

// About what the widget pane kept: it trimmed at 100000 blocks, back to 90% of
// them, and a log line is short.
const qsizetype maxLogCharCount = 10 * 1000 * 1000;

LogChannel channelForChar(QChar c)
{
    switch (c.unicode()) {
        case 'd': return LogDebug;
        case 'w': return LogWarning;
        case 'e': return LogError;
        case '<': return LogInput;
        case '>': return LogOutput;
        case 's': return LogStatus;
        case 't': return LogTime;
        default: return LogMisc;
    }
}

QChar charForChannel(int channel)
{
    switch (channel) {
        case LogDebug: return QLatin1Char('d');
        case LogWarning: return QLatin1Char('w');
        case LogError: return QLatin1Char('e');
        case LogInput: return QLatin1Char('<');
        case LogOutput: return QLatin1Char('>');
        case LogStatus: return QLatin1Char('s');
        case LogTime: return QLatin1Char('t');
        case LogMisc:
        default: return QLatin1Char(' ');
    }
}

// A channel with nothing particular to say about itself answers an invalid
// colour, which is what leaves the line in the ordinary text colour.
QColor colorForChannel(int channel)
{
    using Utils::Theme;
    switch (channel) {
    case LogInput:   return creatorColor(Theme::Debugger_LogWindow_LogInput);
    case LogStatus:  return creatorColor(Theme::Debugger_LogWindow_LogStatus);
    case LogWarning: return creatorColor(Theme::OutputPanes_WarningMessageTextColor);
    case LogError:   return creatorColor(Theme::OutputPanes_ErrorMessageTextColor);
    case LogTime:    return creatorColor(Theme::Debugger_LogWindow_LogTime);
    default:         return {};
    }
}

QString logText(int channel, const QString &output, const QString &timeStamp)
{
    if (output.isEmpty())
        return {};

    const QChar cchar = charForChannel(channel);
    const QChar nchar = '\n';

    QString out;
    out.reserve(output.size() + 1000);

    // A console stream carries no time of its own to be stamped: it is the
    // program talking, not the debugger.
    if (output.at(0) != '~' && !timeStamp.isEmpty()) {
        out.append(charForChannel(LogTime));
        out.append(timeStamp);
        out.append(nchar);
    }

    for (int pos = 0, n = output.size(); pos < n; ) {
        const int npos = output.indexOf(nchar, pos);
        const int nnpos = npos == -1 ? n : npos;
        const int l = nnpos - pos;
        // The debugger's own prompt is not worth a line of the transcript.
        if (l != 6 || QStringView(output).mid(pos, 6) != QLatin1String("(gdb) ")) {
            out.append(cchar);
            if (l > 30000) {
                // A single line this long is a dump of something, and a text
                // document asserts on really long ones.
                out.append(output.mid(pos, 30000));
                out.append(" [...] <cut off>\n");
            } else {
                out.append(output.mid(pos, l + 1));
            }
        }
        pos = nnpos + 1;
    }
    if (!out.endsWith(nchar))
        out.append(nchar);

    return out;
}

int commandTokenForLine(const QString &line)
{
    QStringView rest(line);
    // Cut the time stamp, which is written in front of the command.
    if (rest.size() > 18 && rest.at(0) == '[')
        rest = rest.mid(18);

    int n = 0;
    for (int i = 0; i != rest.size(); ++i) {
        const QChar c = rest.at(i);
        if (!c.isDigit())
            break;
        n = 10 * n + c.unicode() - '0';
    }
    return n;
}

static bool writeLogContents(const QPlainTextEdit *editor)
{
    bool success = false;
    while (!success) {
        const FilePath filePath = FileUtils::getSaveFilePath(Tr::tr("Log File"));
        if (filePath.isEmpty())
            break;
        FileSaver saver(filePath, QIODevice::Text);
        saver.write(editor->toPlainText().toUtf8());
        if (saver.finalize())
            success = true;
    }
    return success;
}

/////////////////////////////////////////////////////////////////////
//
// OutputHighlighter
//
/////////////////////////////////////////////////////////////////////

class OutputHighlighter : public QSyntaxHighlighter
{
public:
    OutputHighlighter(QTextDocument *document, QWidget *drawnOn)
        : QSyntaxHighlighter(document), m_drawnOn(drawnOn)
    {}

private:
    void highlightBlock(const QString &text) override
    {
        const QColor color
            = colorForChannel(channelForChar(text.isEmpty() ? QChar() : text.at(0)));
        if (color.isValid()) {
            QTextCharFormat format;
            format.setForeground(color);
            setFormat(1, text.size(), format);
        }

        // The channel marker is the first character of every line and is not
        // for reading: drawn in the background colour at a point size of one,
        // it takes no width and cannot be seen.
        QTextCharFormat hidden;
        hidden.setForeground(m_drawnOn->palette().color(QPalette::Base));
        hidden.setFontPointSize(1);
        setFormat(0, 1, hidden);
    }

    QWidget *m_drawnOn;
};


/////////////////////////////////////////////////////////////////////
//
// InputHighlighter
//
/////////////////////////////////////////////////////////////////////

class InputHighlighter : public QSyntaxHighlighter
{
public:
    InputHighlighter(QTextDocument *document)
        : QSyntaxHighlighter(document)
    {}

private:
    void highlightBlock(const QString &text) override
    {
        if (text.size() > 3 && text.at(2) == ':') {
            QTextCharFormat format;
            format.setForeground(creatorColor(Theme::Debugger_LogWindow_LogTime));
            setFormat(1, text.size(), format);
        }
    }
};


/////////////////////////////////////////////////////////////////////
//
// DebbuggerPane base class
//
/////////////////////////////////////////////////////////////////////

class DebuggerPane : public QPlainTextEdit
{
public:
    explicit DebuggerPane()
    {
        setFrameStyle(QFrame::NoFrame);
        setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);

        m_clearContentsAction = new QAction(this);
        m_clearContentsAction->setText(Tr::tr("Clear Contents"));
        m_clearContentsAction->setEnabled(true);

        m_saveContentsAction = new QAction(this);
        m_saveContentsAction->setText(Tr::tr("Save Contents"));
        m_saveContentsAction->setEnabled(true);
        connect(m_saveContentsAction, &QAction::triggered,
                this, &DebuggerPane::saveContents);
    }

    void contextMenuEvent(QContextMenuEvent *ev) override
    {
        QMenu *menu = createStandardContextMenu();
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->addAction(m_clearContentsAction);
        menu->addAction(m_saveContentsAction); // X11 clipboard is unreliable for long texts
        menu->addAction(settings().logTimeStamps.action());
        menu->addAction(Core::ActionManager::command(Constants::RELOAD_DEBUGGING_HELPERS)->action());
        menu->addSeparator();
        menu->addAction(settings().settingsDialog.action());
        menu->exec(ev->globalPos());
    }

    void append(const QString &text)
    {
        const int N = 100000;
        const int bc = blockCount();
        if (bc > N) {
            QTextDocument *doc = document();
            QTextBlock block = doc->findBlockByLineNumber(bc * 9 / 10);
            QTextCursor tc(block);
            tc.movePosition(QTextCursor::Start, QTextCursor::KeepAnchor);
            tc.removeSelectedText();
            // Seems to be the only way to force shrinking of the
            // allocated data.
            QString contents = doc->toHtml();
            doc->clear();
            doc->setHtml(contents);
        }
        appendPlainText(text);
    }

    void clearUndoRedoStacks()
    {
        if (!isUndoRedoEnabled())
            return;
        setUndoRedoEnabled(false);
        setUndoRedoEnabled(true);
    }

    QAction *clearContentsAction() const { return m_clearContentsAction; }

private:
    void saveContents() { writeLogContents(this); }

    QAction *m_clearContentsAction;
    QAction *m_saveContentsAction;
};



/////////////////////////////////////////////////////////////////////
//
// InputPane
//
/////////////////////////////////////////////////////////////////////

class InputPane : public DebuggerPane
{
    Q_OBJECT
public:
    InputPane(LogWindow *logWindow)
    {
        connect(clearContentsAction(), &QAction::triggered,
                logWindow, &LogWindow::clearContents);
        (void) new InputHighlighter(document());
    }

signals:
    void executeLineRequested();
    void clearContentsRequested();
    void statusMessageRequested(const QString &, int);
    void commandSelected(int);

private:
    void keyPressEvent(QKeyEvent *ev) override
    {
        if (ev->modifiers() == Qt::ControlModifier && ev->key() == Qt::Key_Return)
            emit executeLineRequested();
        else if (ev->modifiers() == Qt::ControlModifier && ev->key() == Qt::Key_R)
            emit clearContentsRequested();
        else
            QPlainTextEdit::keyPressEvent(ev);
    }

    void mouseDoubleClickEvent(QMouseEvent *ev) override
    {
        emit commandSelected(
            commandTokenForLine(cursorForPosition(ev->pos()).block().text()));
    }

    void focusInEvent(QFocusEvent *ev) override
    {
        emit statusMessageRequested(
            Tr::tr("Press %1 to execute a line.")
                .arg(QKeySequence("Ctrl+Return").toString(QKeySequence::NativeText)),
            -1);
        QPlainTextEdit::focusInEvent(ev);
    }

    void focusOutEvent(QFocusEvent *ev) override
    {
        emit statusMessageRequested(QString(), -1);
        QPlainTextEdit::focusOutEvent(ev);
    }
};


/////////////////////////////////////////////////////////////////////
//
// CombinedPane
//
/////////////////////////////////////////////////////////////////////

// The debugger writes an answer three ways - "42^done", ">42^done" when it is
// echoing, and dtoken("42")@ - and the line has to *start* with one of them:
// those digits turn up in the middle of other lines constantly, and a search
// that took the first one it saw would land on an unrelated line.
QTextBlock blockForResult(const QTextDocument *document, int token)
{
    if (!document)
        return {};

    const QString needle = QString::number(token) + '^';
    const QString echoed = '>' + needle;
    const QString dtoken = QString("dtoken(\"%1\")@").arg(token);

    for (QTextBlock block = document->firstBlock(); block.isValid(); block = block.next()) {
        const QString line = block.text();
        if (line.startsWith(needle) || line.startsWith(echoed) || line.startsWith(dtoken))
            return block;
    }
    return {};
}

// Asked for when the menu is about to open rather than built once: the view a
// pane draws on does not exist until a Qt Quick front end has loaded, and both
// log windows are made while plugins are still initializing.
static void offerLogContextMenu(Core::OutputPaneView *pane,
                                const std::function<void()> &clearAll)
{
    auto clear = new QAction(Tr::tr("Clear Contents"), pane);
    QObject::connect(clear, &QAction::triggered, pane, clearAll);

    auto save = new QAction(Tr::tr("Save Contents"), pane);
    QObject::connect(save, &QAction::triggered, pane, [pane] {
        while (true) {
            const FilePath filePath = FileUtils::getSaveFilePath(Tr::tr("Log File"));
            if (filePath.isEmpty())
                return;
            if (pane->saveContentsTo(filePath))
                return;
        }
    });

    pane->setContextMenuActions({clear,
                                 save, // X11 clipboard is unreliable for long texts
                                 settings().logTimeStamps.action(),
                                 Core::ActionManager::command(
                                     Constants::RELOAD_DEBUGGING_HELPERS)->action(),
                                 nullptr,
                                 settings().settingsDialog.action()});
}

// The answer to command \a token, selected from its start down to the start of
// the line under it - so the answer is highlighted whole and what follows it,
// which is where a result's payload is written, is brought into view with it.
// A null cursor where there is no such answer.
QTextCursor cursorForResult(QTextDocument *document, int token)
{
    const QTextBlock answer = blockForResult(document, token);
    if (!answer.isValid())
        return {};

    QTextCursor cursor(answer);
    cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor);
    return cursor;
}


/////////////////////////////////////////////////////////////////////
//
// DebuggerOutputWindow
//
/////////////////////////////////////////////////////////////////////

LogWindow::LogWindow(DebuggerEngine *engine)
    : m_engine(engine)
{
    setWindowTitle(Tr::tr("Debugger &Log"));
    setObjectName("Log");

    m_ignoreNextInputEcho = false;

    auto m_splitter = new Core::MiniSplitter(Qt::Horizontal);
    m_splitter->setParent(this);

    // Mixed input/output.
    m_combinedText = new Core::OutputPaneView;
    m_combinedText->setMaxCharCount(maxLogCharCount);
    // The line colours are the channel markers the log writes in front of
    // every line, which no output format describes.
    (void) new OutputHighlighter(m_combinedText->sourceDocument(), m_combinedText);
    connect(m_combinedText, &Core::OutputPaneView::contextMenuAboutToShow,
            this, [this] { offerLogContextMenu(m_combinedText, [this] { clearContents(); }); });

    // Input only.
    m_inputText = new InputPane(this);
    m_inputText->setReadOnly(false);

    m_commandEdit = new Utils::FancyLineEdit(this);
    m_commandEdit->setFrame(false);
    m_commandEdit->setHistoryCompleter("DebuggerInput");

    auto repeatButton = new QToolButton(this);
    repeatButton->setIcon(Icons::STEP_OVER.icon());
    repeatButton->setFixedSize(QSize(18, 18));
    repeatButton->setToolTip(Tr::tr("Repeat last command for debug reasons."));

    auto commandBox = new QHBoxLayout;
    commandBox->addWidget(repeatButton);
    commandBox->addWidget(new QLabel(Tr::tr("Command:"), this));
    commandBox->addWidget(m_commandEdit);
    commandBox->setContentsMargins(2, 2, 2, 2);
    commandBox->setSpacing(6);

    auto leftBox = new QVBoxLayout;
    leftBox->addWidget(m_inputText);
    leftBox->addItem(commandBox);
    leftBox->setContentsMargins(0, 0, 0, 0);
    leftBox->setSpacing(0);

    auto leftDummy = new QWidget;
    leftDummy->setLayout(leftBox);

    m_splitter->addWidget(leftDummy);
    m_splitter->addWidget(m_combinedText);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 3);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_splitter);
    layout->addWidget(new Core::FindToolBarPlaceHolder(this));
    setLayout(layout);

    Aggregation::aggregate({m_inputText, new Core::BaseTextFind(m_inputText)});

    connect(m_inputText, &InputPane::statusMessageRequested,
            this, &LogWindow::statusMessageRequested);
    connect(m_inputText, &InputPane::commandSelected,
            this, &LogWindow::gotoResult);
    connect(m_commandEdit, &QLineEdit::returnPressed,
            this, &LogWindow::sendCommand);
    connect(m_inputText, &InputPane::executeLineRequested,
            this, &LogWindow::executeLine);
    connect(repeatButton, &QAbstractButton::clicked,
            this, &LogWindow::repeatLastCommand);

    connect(&m_outputTimer, &QTimer::timeout,
            this, &LogWindow::doOutput);

    setMinimumHeight(60);

    showOutput(
        LogWarning,
        Tr::tr(
            "Note: This log contains possibly confidential information about your machine, "
            "environment variables, in-memory data of the processes you are debugging, and more. "
            "It is never transferred over the internet by %1, and only stored "
            "to disk if you manually use the respective option from the context menu, or through "
            "mechanisms that are not under the control of %1's Debugger plugin, "
            "for instance in swap files, or other plugins you might use.\n"
            "You may be asked to share the contents of this log when reporting bugs related "
            "to debugger operation. In this case, make sure your submission does not "
            "contain data you do not want to or you are not allowed to share.\n\n")
            .arg(QGuiApplication::applicationDisplayName()));
}

LogWindow::~LogWindow()
{
    disconnect(&m_outputTimer, &QTimer::timeout, this, &LogWindow::doOutput);
    m_outputTimer.stop();
    doOutput();
}

void LogWindow::executeLine()
{
    m_ignoreNextInputEcho = true;
    m_engine->executeDebuggerCommand(m_inputText->textCursor().block().text());
}

void LogWindow::repeatLastCommand()
{
    m_engine->debugLastCommand();
}

DebuggerEngine *LogWindow::engine() const
{
    return m_engine;
}

void LogWindow::sendCommand()
{
    m_engine->executeDebuggerCommand(m_commandEdit->text());
}

void LogWindow::showOutput(int channel, const QString &output)
{
    if (output.isEmpty())
        return;

    m_queuedOutput.append(
        logText(channel, output, settings().logTimeStamps() ? logTimeStamp() : QString()));
    // flush the output if it exceeds 16k to prevent out of memory exceptions on regular output
    if (m_queuedOutput.size() > 16 * 1024) {
        m_outputTimer.stop();
        doOutput();
    } else {
        m_outputTimer.setSingleShot(true);
        m_outputTimer.start(80);
    }
}

void LogWindow::doOutput()
{
    if (m_queuedOutput.isEmpty())
        return;

    if (theGlobalLog)
        theGlobalLog->doOutput(m_queuedOutput);

    // Following the end is the view's own business: it keeps its place when
    // the reader has scrolled away and goes back to the end when they have
    // not, which is what the cursor dance here was for.
    m_combinedText->appendMessage(m_queuedOutput, Utils::NormalMessageFormat);
    m_queuedOutput.clear();
}

void LogWindow::gotoResult(int token)
{
    const QTextCursor answer = cursorForResult(m_combinedText->sourceDocument(), token);
    if (answer.isNull())
        return;

    m_combinedText->setFocus();
    m_combinedText->showCursor(answer);
}

void LogWindow::showInput(int channel, const QString &input)
{
    Q_UNUSED(channel)
    if (m_ignoreNextInputEcho) {
        m_ignoreNextInputEcho = false;
        QTextCursor cursor = m_inputText->textCursor();
        cursor.movePosition(QTextCursor::Down);
        cursor.movePosition(QTextCursor::EndOfLine);
        m_inputText->setTextCursor(cursor);
        return;
    }
    if (settings().logTimeStamps())
        m_inputText->append(logTimeStamp());
    m_inputText->append(input);
    QTextCursor cursor = m_inputText->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_inputText->setTextCursor(cursor);
    m_inputText->ensureCursorVisible();

    theGlobalLog->doInput(input);
}

void LogWindow::clearContents()
{
    m_combinedText->clear();
    m_inputText->clear();
    theGlobalLog->clearContents();
}

void LogWindow::setCursor(const QCursor &cursor)
{
    m_inputText->viewport()->setCursor(cursor);
    QWidget::setCursor(cursor);
}

QString LogWindow::combinedContents() const
{
    return m_combinedText->toPlainText();
}

QString LogWindow::inputContents() const
{
    return m_inputText->toPlainText();
}

void LogWindow::clearUndoRedoStacks()
{
    // Only what can be typed into has anything to undo.
    m_inputText->clearUndoRedoStacks();
}

QString LogWindow::logTimeStamp()
{
    // Cache the last log time entry by ms. If time progresses,
    // report the difference to the last time stamp in ms.
    static const QString logTimeFormat("hh:mm:ss.zzz");
    static QTime lastTime = QTime::currentTime();
    static QString lastTimeStamp = lastTime.toString(logTimeFormat);

    const QTime currentTime = QTime::currentTime();
    if (currentTime != lastTime) {
        const int elapsedMS = lastTime.msecsTo(currentTime);
        lastTime = currentTime;
        lastTimeStamp = lastTime.toString(logTimeFormat);
        // Append time elapsed
        QString rc = lastTimeStamp;
        rc += " [";
        rc += QString::number(elapsedMS);
        rc += "ms]";
        return rc;
    }
    return lastTimeStamp;
}

/////////////////////////////////////////////////////////////////////
//
// GlobalLogWindow
//
/////////////////////////////////////////////////////////////////////

GlobalLogWindow::GlobalLogWindow()
{
    theGlobalLog = this;

    setWindowTitle(Tr::tr("Global Debugger &Log"));
    setObjectName("GlobalLog");

    auto splitter = new Core::MiniSplitter(Qt::Horizontal);
    splitter->setParent(this);

    m_leftPane = new Core::OutputPaneView;
    m_rightPane = new Core::OutputPaneView;

    for (Core::OutputPaneView * const pane : {m_leftPane, m_rightPane}) {
        pane->setMaxCharCount(maxLogCharCount);
        // The line colours are the channel markers the log writes in front of
        // every line, which no output format describes.
        (void) new OutputHighlighter(pane->sourceDocument(), pane);
        connect(pane, &Core::OutputPaneView::contextMenuAboutToShow,
                this, [this, pane] { showContextMenuFor(pane); });
    }

    splitter->addWidget(m_leftPane);
    splitter->addWidget(m_rightPane);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(splitter);
    layout->addWidget(new Core::FindToolBarPlaceHolder(this));
    setLayout(layout);
}

void GlobalLogWindow::showContextMenuFor(Core::OutputPaneView *pane)
{
    offerLogContextMenu(pane, [this] { clearContents(); });
}


GlobalLogWindow::~GlobalLogWindow()
{
    theGlobalLog = nullptr;
}

void GlobalLogWindow::doOutput(const QString &output)
{
    // Following the end is the view's own business now: it keeps its place
    // when the reader has scrolled away and goes back to the end when they
    // have not, which is what the cursor dance here was for.
    m_rightPane->appendMessage(output, Utils::NormalMessageFormat);
}

void GlobalLogWindow::doInput(const QString &input)
{
    if (settings().logTimeStamps())
        m_leftPane->appendMessage(LogWindow::logTimeStamp() + '\n',
                                  Utils::NormalMessageFormat);
    m_leftPane->appendMessage(input + '\n', Utils::NormalMessageFormat);
}

void GlobalLogWindow::clearContents()
{
    m_rightPane->clear();
    m_leftPane->clear();
}

void GlobalLogWindow::setCursor(const QCursor &cursor)
{
    QWidget::setCursor(cursor);
}

void GlobalLogWindow::clearUndoRedoStacks()
{
    // Nothing to undo in a view that cannot be typed into.
}

#ifdef WITH_TESTS

class LogWindowTest final : public QObject
{
    Q_OBJECT

private slots:
    void testFindingTheAnswerToACommand()
    {
        // Clicking a command in the log jumps to its answer. The debugger
        // writes that answer three ways, and the digits of a token turn up in
        // the middle of other lines constantly.
        QTextDocument log;
        log.setPlainText(
            "sending 42^ to the inferior\n"   // mentions it, is not it
            ">42^done,value=\"1\"\n"          // the echo of the command
            "43^done,value=\"2\"\n"           // the plain form
            "some line about 43^ again\n"
            "dtoken(\"44\")@\n"
            "trailing\n");

        // The plain form, and the echoed one.
        QCOMPARE(blockForResult(&log, 43).text(), QString("43^done,value=\"2\""));
        QCOMPARE(blockForResult(&log, 42).text(), QString(">42^done,value=\"1\""));

        // And the third spelling.
        QCOMPARE(blockForResult(&log, 44).text(), QString("dtoken(\"44\")@"));

        // A token mentioned inside a line is not that line's answer. This is
        // the whole reason the search is not just "find these characters":
        // line one contains "42^" and is not what was asked for.
        QVERIFY(!blockForResult(&log, 42).text().startsWith("sending"));

        // What is not there is not found, rather than the nearest thing.
        QVERIFY(!blockForResult(&log, 99).isValid());
        QVERIFY(!blockForResult(nullptr, 1).isValid());
    }

    void testWhatOnePieceOfDebuggerOutputBecomes()
    {
        // Every line is marked with its channel, and the marker is what the
        // colouring reads back.
        QCOMPARE(logText(LogError, "boom", {}), QString("eboom\n"));
        QCOMPARE(logText(LogInput, "next", {}), QString("<next\n"));

        // The time stamp is a line of its own, on the time channel.
        QCOMPARE(logText(LogError, "boom", "12:00:00.000"),
                 QString("t12:00:00.000\neboom\n"));

        // Except for a console stream, which is the program talking and
        // carries no time of the debugger's to stamp.
        QCOMPARE(logText(LogOutput, "~said something", "12:00:00.000"),
                 QString(">~said something\n"));

        // The debugger's own prompt is dropped, and only when the line is
        // exactly that - a line that merely begins with it stays.
        QCOMPARE(logText(LogOutput, "a\n(gdb) \nb", {}), QString(">a\n>b\n"));
        QCOMPARE(logText(LogOutput, "(gdb) x", {}), QString(">(gdb) x\n"));

        // Nothing in, nothing out - not a bare marker.
        QCOMPARE(logText(LogError, {}, "12:00:00.000"), QString());
    }

    void testALineTooLongToDrawIsCutShort()
    {
        // A text document asserts on really long lines, so one line of 40000
        // characters becomes 30000 and a note saying so - and the rest of the
        // output still arrives.
        const QString huge(40000, 'x');
        const QString out = logText(LogOutput, huge + "\nafter", {});

        QVERIFY2(out.contains(" [...] <cut off>"), "an over-long line was written whole");
        QCOMPARE(out.count('\n'), 2);
        QCOMPARE(out.size(), 1 + 30000 + QString(" [...] <cut off>\n").size()
                                 + 1 + QString("after\n").size());
        QVERIFY2(out.endsWith(">after\n"), "what followed an over-long line was lost");

        // A line just under the limit is not touched.
        const QString big(29999, 'x');
        QCOMPARE(logText(LogOutput, big, {}), '>' + big + '\n');
    }

    void testWhichCommandALineOfTheInputPaneRefersTo()
    {
        QCOMPARE(commandTokenForLine("42^done"), 42);

        // With a time stamp in front of it. What is dropped is a fixed
        // *width* - eighteen characters - and not "up to the bracket", so this
        // works only for a stamp of exactly that length.
        QCOMPARE(QString("[12:00:00.000 5ms]").size(), 18);
        QCOMPARE(commandTokenForLine("[12:00:00.000 5ms]42^done"), 42);

        // A wider one leaves part of itself behind and the digits are never
        // reached. Asserted because it is what happens, not because it is
        // wanted: anything that changes the stamp breaks the double-click.
        QCOMPARE(commandTokenForLine("[12:00:00.000 [0ms]]42^done"), 0);

        // A line that names no command answers 0, as the double-click that
        // asks this has always done - there is no "no command" answer.
        QCOMPARE(commandTokenForLine("some prose"), 0);
        QCOMPARE(commandTokenForLine({}), 0);
    }

    void testEachChannelIsWrittenAsItsOwnCharacter()
    {
        // The marker is how a line says which channel it is on, so the two
        // have to agree - the colouring reads back what the writing put there.
        for (int channel : {LogDebug, LogWarning, LogError, LogInput, LogOutput,
                            LogStatus, LogTime, LogMisc}) {
            QCOMPARE(int(channelForChar(charForChannel(channel))), channel);
        }

        // Three of the colours were spelled out in the highlighter's paint;
        // a channel with nothing to say about itself has no colour of its own.
        QVERIFY(colorForChannel(LogError).isValid());
        QVERIFY(colorForChannel(LogWarning).isValid());
        QVERIFY(colorForChannel(LogError) != colorForChannel(LogWarning));
        QVERIFY2(!colorForChannel(LogOutput).isValid(),
                 "plain output was given a colour of its own");
    }

    void testTheChannelMarkerColoursTheLineAndIsNotSeen()
    {
        // The colouring is a highlighter over the document, so it has to still
        // work now that the document belongs to a Qt Quick view rather than to
        // a QPlainTextEdit.
        Core::OutputPaneView pane;
        (void) new OutputHighlighter(pane.sourceDocument(), &pane);

        pane.appendMessage(logText(LogError, "boom", {}), Utils::NormalMessageFormat);
        pane.flush();

        QTextDocument * const document = pane.sourceDocument();
        QTRY_COMPARE(document->firstBlock().text(), QString("eboom"));

        const QList<QTextLayout::FormatRange> formats = document->firstBlock().layout()->formats();
        QVERIFY2(!formats.isEmpty(), "nothing coloured the line at all");

        const auto colouredAs = [&formats](int start) -> QColor {
            for (const QTextLayout::FormatRange &range : formats) {
                if (range.start <= start && start < range.start + range.length)
                    return range.format.foreground().color();
            }
            return {};
        };

        // The message in its channel's colour...
        QCOMPARE(colouredAs(1), colorForChannel(LogError));
        // ...and the marker in front of it drawn to be unreadable.
        QCOMPARE(colouredAs(0), pane.palette().color(QPalette::Base));
    }

    void testTheGlobalLogKeepsWhatWasTypedApartFromEverything()
    {
        // Two panes side by side: everything on the right, and only what was
        // typed on the left. Building one installs it as *the* global log, so
        // the running instance's is put back afterwards.
        GlobalLogWindow * const running = theGlobalLog;
        {
            GlobalLogWindow window;
            const QList<Core::OutputPaneView *> panes
                = window.findChildren<Core::OutputPaneView *>();
            QCOMPARE(panes.size(), 2);

            window.doOutput(logText(LogError, "boom", {}));
            for (Core::OutputPaneView * const pane : panes)
                pane->flush();

            // Which is which is asked of them rather than assumed from the
            // order they were built in.
            Core::OutputPaneView *everything = nullptr;
            Core::OutputPaneView *typedOnly = nullptr;
            for (Core::OutputPaneView * const pane : panes)
                (pane->toPlainText().contains("boom") ? everything : typedOnly) = pane;
            QVERIFY2(everything && typedOnly, "both panes showed the same thing");

            window.doInput("next");
            for (Core::OutputPaneView * const pane : panes)
                pane->flush();

            QTRY_VERIFY(typedOnly->toPlainText().contains("next"));
            QVERIFY2(!everything->toPlainText().contains("next"),
                     "what was typed was echoed into the pane it is kept out of");

            window.clearContents();
            QCOMPARE(everything->toPlainText(), QString());
            QCOMPARE(typedOnly->toPlainText(), QString());
        }
        theGlobalLog = running;
    }

    void testTheAnswerToACommandIsShownWithWhatItReturned()
    {
        // Double-clicking a command in the input pane jumps the transcript to
        // its answer, and selects the line under it as well: a result's
        // payload is written there, and an answer without it says nothing.
        QTextDocument log;
        log.setPlainText(
            ">42^done,value=\"1\"\n"
            "payload of 42\n"
            "43^done,value=\"2\"\n"
            "payload of 43\n");

        const QTextCursor answer = cursorForResult(&log, 43);
        QVERIFY(!answer.isNull());

        // The answer line and the break after it: moving down from the start
        // of a line lands at the start of the next, so what is selected is the
        // answer, and the payload under it is what that scrolls into view.
        QCOMPARE(answer.selectedText().split(QChar(0x2029)),
                 QStringList({"43^done,value=\"2\"", ""}));
        // Exactly: from the start of the answer to the start of the line under
        // it. Stopping at the end of the answer would leave the payload off
        // the bottom of the view on a log scrolled to that point.
        const QTextBlock answerBlock = blockForResult(&log, 43);
        QCOMPARE(answer.selectionStart(), answerBlock.position());
        QCOMPARE(answer.selectionEnd(), answerBlock.position() + answerBlock.length());

        // Nothing there means no cursor, rather than one at the top of the
        // log - which would look like an answer.
        QVERIFY(cursorForResult(&log, 99).isNull());
        QVERIFY(cursorForResult(nullptr, 42).isNull());
    }

    void testDoubleClickingACommandReachesTheTranscript()
    {
        // The whole way through: the input pane says which command was picked,
        // and the transcript ends up showing that command's answer.
        GlobalLogWindow * const running = theGlobalLog;
        {
            LogWindow window(nullptr);
            const QList<Core::OutputPaneView *> panes
                = window.findChildren<Core::OutputPaneView *>();
            QCOMPARE(panes.size(), 1);
            Core::OutputPaneView * const combined = panes.first();

            const QList<InputPane *> inputs = window.findChildren<InputPane *>();
            QCOMPARE(inputs.size(), 1);

            combined->clear();
            combined->appendMessage(logText(LogOutput, "42^done\npayload of 42\n"
                                                       "43^done\npayload of 43", {}),
                                    Utils::NormalMessageFormat);
            combined->flush();
            QTRY_VERIFY(combined->toPlainText().contains("43^done"));

            Core::OutputView * const view = combined->view();
            QVERIFY2(view, "no front end drew the transcript");

            emit inputs.first()->commandSelected(43);

            const QTextCursor shown = view->textCursor();
            QVERIFY2(shown.hasSelection(), "the answer was not shown at all");
            // The marker character in front of every line is part of the text.
            QCOMPARE(shown.selectedText().split(QChar(0x2029)),
                     QStringList({">43^done", ""}));

            // And it is that answer, not the first one that mentions 43.
            QVERIFY2(!shown.selectedText().startsWith(">42"),
                     "the transcript jumped to the wrong command");
        }
        theGlobalLog = running;
    }
};

QObject *createLogWindowTest()
{
    return new LogWindowTest;
}

#endif // WITH_TESTS

} // namespace Debugger::Internal

#include "logwindow.moc"
