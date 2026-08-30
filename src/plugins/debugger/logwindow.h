// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "debuggerconstants.h"

#include <QColor>
#include <QWidget>
#include <QTimer>

QT_BEGIN_NAMESPACE
class QCursor;
class QTextBlock;
class QTextDocument;
QT_END_NAMESPACE

namespace Core { class OutputPaneView; }

namespace Utils { class FancyLineEdit; }

namespace Debugger::Internal {

// Which channel a line of the log belongs to is written as its first
// character, so that the transcript stays plain text and still says what each
// line is. The marker is drawn invisibly; colorForChannel() is what it is
// drawn as, which the highlighter used to decide while painting.
LogChannel channelForChar(QChar c);
QChar charForChannel(int channel);
QColor colorForChannel(int channel);

// What one piece of debugger output becomes in the log: every line marked with
// its channel, the debugger's own "(gdb) " prompt dropped, over-long lines cut
// short, and \a timeStamp put in front on its own line unless the output is a
// console stream (which starts with '~') or there is no time stamp to put.
//
// Kept out of the window because it is a question about text, and inside one
// the only way to ask it was to run a debugger and read the answer.
QString logText(int channel, const QString &output, const QString &timeStamp);

// The command a line of the input pane refers to, which is the number it
// starts with once the time stamp in front of it is dropped. Answers 0 for a
// line that names no command, as the double-click that asks has always done.
int commandTokenForLine(const QString &line);

// Where the answer to command \a token is in a transcript.
QTextBlock blockForResult(const QTextDocument *document, int token);

class DebuggerEngine;
class DebuggerPane;
class InputPane;

class LogWindow final : public QWidget
{
    Q_OBJECT

public:
    explicit LogWindow(DebuggerEngine *engine);
    ~LogWindow() final;

    DebuggerEngine *engine() const;

    void setCursor(const QCursor &cursor);

    QString combinedContents() const;
    QString inputContents() const;

    void clearUndoRedoStacks();

    static QString logTimeStamp();

    void clearContents();
    void sendCommand();
    void executeLine();
    void showOutput(int channel, const QString &output);
    void showInput(int channel, const QString &input);
    void doOutput();
    void repeatLastCommand();

signals:
    void statusMessageRequested(const QString &msg, int);

private:
    void gotoResult(int token);

    Core::OutputPaneView *m_combinedText; // combined input/output
    InputPane *m_inputText;               // scriptable input alone
    QTimer m_outputTimer;
    QString m_queuedOutput;
    Utils::FancyLineEdit *m_commandEdit;
    bool m_ignoreNextInputEcho;
    DebuggerEngine *m_engine;
};

class GlobalLogWindow final : public QWidget
{
public:
    explicit GlobalLogWindow();
    ~GlobalLogWindow() final;

    void setCursor(const QCursor &cursor);

    void clearUndoRedoStacks();
    void clearContents();
    void doInput(const QString &input);
    void doOutput(const QString &output);

private:
    void showContextMenuFor(Core::OutputPaneView *pane);

    Core::OutputPaneView *m_rightPane; // everything
    Core::OutputPaneView *m_leftPane;  // combined input
};

#ifdef WITH_TESTS
QObject *createLogWindowTest();
#endif

} // Debugger::Internla
