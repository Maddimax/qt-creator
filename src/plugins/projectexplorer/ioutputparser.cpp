// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "ioutputparser.h"

#include "task.h"
#include "taskhub.h"

#include <coreplugin/outputpaneview.h>
#include <coreplugin/outputview.h>
#ifdef WITH_TESTS
#include <QTest>
#endif
#include <QTextCursor>
#include <coreplugin/outputtasksink.h>
#include <texteditor/fontsettings.h>
#include <utils/algorithm.h>
#include <utils/ansiescapecodehandler.h>

#include <QPlainTextEdit>

#include <numeric>

/*!
    \class ProjectExplorer::OutputTaskParser

    \brief The OutputTaskParser class provides an interface for an output parser
    that emits issues (tasks).

    \sa ProjectExplorer::Task
*/

/*!
   \fn ProjectExplorer::OutputTaskParser::Status ProjectExplorer::OutputTaskParser::handleLine(const QString &line, Utils::OutputFormat type)

   Called once for each line of standard output or standard error to parse.
*/

/*!
   \fn bool ProjectExplorer::OutputTaskParser::hasFatalErrors() const

   This is mainly a Symbian specific quirk.
*/

/*!
   \fn void ProjectExplorer::OutputTaskParser::addTask(const ProjectExplorer::Task &task)

   Should be emitted for each task seen in the output.
*/

/*!
   \fn void ProjectExplorer::OutputTaskParser::flush()

   Instructs a parser to flush its state.
   Parsers may have state (for example, because they need to aggregate several
   lines into one task). This
   function is called when this state needs to be flushed out to be visible.
*/

namespace ProjectExplorer {

class OutputTaskParser::Private
{
public:
    QList<TaskInfo> scheduledTasks;
    Task currentTask;
    LinkSpecs linkSpecs;
    QString origin;
    int lineCount = 0;
    bool targetLinkFixed = false;
};

OutputTaskParser::OutputTaskParser() : d(new Private) { }

OutputTaskParser::~OutputTaskParser() { delete d; }

const QList<OutputTaskParser::TaskInfo> OutputTaskParser::taskInfo() const
{
    return d->scheduledTasks;
}

void OutputTaskParser::scheduleTask(const Task &task, int outputLines, int skippedLines)
{
    TaskInfo ts(task, outputLines, skippedLines);
    if (ts.task.isError() && demoteErrorsToWarnings())
        ts.task.setType(Task::Warning);
    d->scheduledTasks << ts;
    QTC_CHECK(d->scheduledTasks.size() <= 2);
}

void OutputTaskParser::setDetailsFormat(Task &task, const LinkSpecs &linkSpecs)
{
    task.setFormats({});
    addDetailsFormat(task, linkSpecs);
}

void OutputTaskParser::addDetailsFormat(Task &task, const LinkSpecs &linkSpecs)
{
    if (!task.hasDetails())
        return;

    Utils::FormattedText monospacedText(task.details().join('\n'));
    monospacedText.format.setFont(TextEditor::globalFontSettings().data().font());
    monospacedText.format.setFontStyleHint(QFont::Monospace);
    const QList<Utils::FormattedText> linkifiedText
        = Utils::OutputFormatter::linkifiedText({monospacedText}, linkSpecs);
    QList<QTextLayout::FormatRange> formats;
    int offset = task.summary().size() + 1;
    for (const Utils::FormattedText &ft : linkifiedText) {
        formats << QTextLayout::FormatRange{offset, int(ft.text.size()), ft.format};
        offset += ft.text.size();
    }
    task.setFormats(task.formats() + formats);
}

void OutputTaskParser::fixTargetLink()
{
    d->targetLinkFixed = true;
}

void OutputTaskParser::runPostPrintActions(QObject *sink)
{
    int offset = 0;
    // Whatever the output is drawn with, so long as it remembers positions.
    if (const auto view = dynamic_cast<Core::OutputTaskSink *>(sink)) {
        Utils::reverseForeach(taskInfo(), [view, &offset](const TaskInfo &ti) {
            view->registerPositionOf(
                ti.task.id(),
                ti.linkedLines,
                ti.skippedLines,
                offset,
                Core::OutputTaskSink::TaskSource::Parsed);
            offset += ti.linkedLines;
        });
    }

    for (const TaskInfo &t : std::as_const(d->scheduledTasks))
        TaskHub::addTask(t.task);
    d->scheduledTasks.clear();
}

void OutputTaskParser::createOrAmendTask(
    Task::TaskType type,
    const QString &description,
    const QString &originalLine,
    bool forceAmend,
    const Utils::FilePath &file,
    int line,
    int column,
    const LinkSpecs &linkSpecs)
{
    const bool amend = !d->currentTask.isNull() && (forceAmend || isContinuation(originalLine));
    if (!amend) {
        flush();
        d->currentTask = CompileTask(type, description, file, line, column);
        d->currentTask.addToDetails(originalLine);
        d->currentTask.setOrigin(d->origin);
        d->linkSpecs = linkSpecs;
        d->lineCount = 1;
        return;
    }

    LinkSpecs adaptedLinkSpecs = linkSpecs;
    const int offset = std::accumulate(
        d->currentTask.details().cbegin(),
        d->currentTask.details().cend(),
        0,
        [](int total, const QString &line) { return total + line.size() + 1; });
    for (LinkSpec &ls : adaptedLinkSpecs)
        ls.startPos += offset;
    d->linkSpecs << adaptedLinkSpecs;
    d->currentTask.addToDetails(originalLine);

    // Check whether the new line is more relevant than the previous ones.
    if ((!d->currentTask.isError() && type == Task::Error)
        || (!d->currentTask.hasKnownType() && type != Task::Unknown)) {
        d->currentTask.setType(type);
        d->currentTask.setSummary(description);
        if (!file.isEmpty() && !d->targetLinkFixed) {
            d->currentTask.setFile(file);
            d->currentTask.setLine(line);
            d->currentTask.setColumn(column);
        }
    }

    ++d->lineCount;
}

void OutputTaskParser::setCurrentTask(const Task &task)
{
    flush();
    d->currentTask = task;
    d->lineCount = 1;
}

Task &OutputTaskParser::currentTask()
{
    return d->currentTask;
}

const Task &OutputTaskParser::currentTask() const
{
    return d->currentTask;
}

bool OutputTaskParser::isContinuation(const QString &line) const
{
    Q_UNUSED(line)
    return false;
}

void OutputTaskParser::flush()
{
    if (d->currentTask.isNull())
        return;

    // If there is only one line of details, then it is the line that we generated
    // the summary from. Remove it, because it does not add any information.
    if (d->currentTask.details().count() == 1)
        d->currentTask.clearDetails();

    setDetailsFormat(d->currentTask, d->linkSpecs);
    Task t = d->currentTask;
    d->currentTask.clear();
    d->linkSpecs.clear();
    scheduleTask(t, d->lineCount, 1);
    d->lineCount = 0;
    d->targetLinkFixed = false;
}

void OutputTaskParser::setOrigin(const QString &source)
{
    d->origin = source;
}

#ifdef WITH_TESTS
namespace Internal {

// Reports one task for the line it recognises, the way a compiler parser does.
class TaskReportingParser final : public OutputTaskParser
{
public:
    explicit TaskReportingParser(const Task &task)
        : m_task(task)
    {}

private:
    Result handleLine(const QString &line, Utils::OutputFormat) override
    {
        if (!line.contains("error:"))
            return Status::NotHandled;
        scheduleTask(m_task, 1);
        return Status::Done;
    }

    const Task m_task;
};

class OutputTaskParserTest final : public QObject
{
    Q_OBJECT

private slots:
    void testAParserTellsAQuickDrawnViewWhereItsTaskIs()
    {
        // runPostPrintActions() used to ask whether the sink was a
        // Core::OutputWindow, so a pane drawn with anything else registered
        // nothing at all - silently, and only noticeable by clicking a task in
        // the Issues pane and having nothing happen.
        const Task task(Task::Error, "an error", Utils::FilePath::fromString("main.cpp"), 1,
                        Utils::Id("Test.Category"));

        Core::OutputPaneView view;
        view.formatter()->setLineParsers({new TaskReportingParser(task)});

        // A process's own output, which is the only kind the formatter runs
        // line parsers over.
        view.appendMessage("configuring\n", Utils::StdErrFormat);
        view.appendMessage("main.cpp:1: error: no\n", Utils::StdErrFormat);

        // The view queues what it is told, and the parser only sees a line
        // once it has actually been written.
        view.flush();

        QVERIFY2(view.knowsPositionOf(task.id()),
                 "the parser never told the view where its task was");

        // And at the right line: the one the parser read, not the end.
        view.showPositionOf(task.id());
        QCOMPARE(view.view()->textCursor().selectedText(), QString("main.cpp:1: error: no"));
    }
};

} // namespace Internal

QObject *createOutputTaskParserTest()
{
    return new Internal::OutputTaskParserTest;
}
#endif // WITH_TESTS

} // namespace ProjectExplorer

#ifdef WITH_TESTS
#include "ioutputparser.moc"
#endif
