// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "vcseditordocument.h"

#include "diffandloghighlighter.h"
#include "vcsbaseeditorconfig.h"
#include "vcsbaseplugin.h"
#include "vcsbasetr.h"
#include "vcscommand.h"
#include "vcsoutputwindow.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/documentmodel.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/icore.h>
#include <coreplugin/vcsmanager.h>

#include <cpaster/codepasterservice.h>

#include <extensionsystem/pluginmanager.h>

#include <texteditor/syntaxhighlighter.h>
#include <texteditor/texteditor.h>
#include <texteditor/textdocumentlayout.h>

#include <utils/qtcassert.h>
#include <utils/stringutils.h>

#include <QAction>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QPointer>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>

#include <QtTaskTree/QSingleTaskTreeRunner>

using namespace QtTaskTree;
using namespace Utils;

namespace VcsBase {

namespace Internal {

// Whichever of \a pattern, a Jira key and a Gerrit Change-Id the cursor's
// column falls in, on the cursor's line; the widget editor's URL handler, as
// it was, minus the widget.
static UrlUnderCursor underCursorMatching(const QTextCursor &cursor,
                                          const QRegularExpression &pattern)
{
    static const QRegularExpression jira("(Fixes|Task-number): ([A-Z]+-[0-9]+)");
    static const QRegularExpression gerrit("Change-Id: (I[a-f0-9]{40})");

    UrlUnderCursor found;
    QTextCursor line = cursor;
    line.select(QTextCursor::LineUnderCursor);
    if (!line.hasSelection())
        return found;
    const QString text = line.selectedText();
    const int column = cursor.columnNumber();
    struct {
        const QRegularExpression &pattern;
        int matchNumber;
        QString urlPrefix;
    } const candidates[] = {
        {pattern, 0, ""},
        {jira, 2, QString("%1/browse/").arg(Core::Constants::QT_JIRA_URL)},
        {gerrit, 1, "https://codereview.qt-project.org/r/"},
    };
    for (const auto &candidate : candidates) {
        QRegularExpressionMatchIterator i = candidate.pattern.globalMatch(text);
        while (i.hasNext()) {
            const QRegularExpressionMatch match = i.next();
            const int start = match.capturedStart(candidate.matchNumber);
            const QString url = match.captured(candidate.matchNumber);
            if (start <= column && column < start + url.size()) {
                found.startColumn = start;
                found.length = int(url.size());
                found.url = candidate.urlPrefix + url;
                return found;
            }
        }
    }
    return found;
}

UrlUnderCursor urlUnderCursor(const QTextCursor &cursor)
{
    static const QRegularExpression http("https?\\://[^\\s]+");
    return underCursorMatching(cursor, http);
}

UrlUnderCursor emailUnderCursor(const QTextCursor &cursor)
{
    static const QRegularExpression mail("[a-zA-Z0-9_\\.-]+@[^@ ]+\\.[a-zA-Z]+");
    return underCursorMatching(cursor, mail);
}

} // namespace Internal

int VcsEditorSections::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant VcsEditorSections::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    switch (role) {
    case Qt::DisplayRole:
        return m_entries.at(index.row());
    case LineRole:
        return m_lines.at(index.row());
    default:
        return {};
    }
}

QHash<int, QByteArray> VcsEditorSections::roleNames() const
{
    return {{Qt::DisplayRole, "display"}, {LineRole, "line"}};
}

QStringList VcsEditorSections::entries() const
{
    return m_entries;
}

QList<int> VcsEditorSections::lines() const
{
    return m_lines;
}

int VcsEditorSections::sectionOfLine(int line) const
{
    const auto it = std::upper_bound(m_lines.cbegin(), m_lines.cend(), line);
    return int(std::distance(m_lines.cbegin(), it)) - 1;
}

void VcsEditorSections::reset(const QStringList &entries, const QList<int> &lines)
{
    QTC_ASSERT(entries.size() == lines.size(), return);
    beginResetModel();
    m_entries = entries;
    m_lines = lines;
    endResetModel();
}

namespace Internal {

// The editor to move for a document: the current one if it is showing it,
// else whichever the editor manager has for it, else none.
static Core::IEditor *editorShowing(Core::IDocument *document)
{
    Core::IEditor * const current = Core::EditorManager::currentEditor();
    if (current && current->document() == document)
        return current;
    return Core::DocumentModel::editorsForDocument(document).value(0);
}

// The sections as the choice the tool bar shows: the row the caret is in is
// current, choosing a row goes to its first line. Nothing here is a pick the
// language would otherwise have made differently, so there is nothing to
// clear and the view offers no way back.
class VcsSectionsChoice final : public TextEditor::ToolBarChoice
{
public:
    VcsSectionsChoice(VcsEditorDocument *document, VcsEditorSections *sections)
        : ToolBarChoice(document)
        , m_document(document)
        , m_sections(sections)
    {
        connect(sections, &QAbstractItemModel::modelReset, this, &ToolBarChoice::changed);
    }

    QAbstractItemModel *model() const override { return m_sections; }
    int currentIndex() const override
    {
        return qMax(0, m_sections->sectionOfLine(m_caretLine - 1));
    }
    QString toolTip() const override { return Tr::tr("Go to an entry"); }
    bool isAvailable() const override { return m_sections->rowCount() > 0; }
    bool isChosen() const override { return false; }
    void clearChoice() override {}

    // In the view showing the document - the current editor where that is
    // the one, else whichever the editor manager has - with a way back, as
    // the widget editor's combo box does it.
    void choose(int index) override
    {
        const QList<int> lines = m_sections->lines();
        if (index < 0 || index >= lines.size())
            return;
        Core::IEditor * const editor = editorShowing(m_document);
        if (!editor)
            return;
        const int lineNumber = lines.at(index) + 1;
        if (lineNumber == editor->currentLine())
            return;
        Core::EditorManager::addCurrentPositionToNavigationHistory();
        editor->gotoLine(lineNumber, 0);
    }

    void followCaret(int line) override
    {
        if (m_caretLine == line)
            return;
        const int was = currentIndex();
        m_caretLine = line;
        if (currentIndex() != was)
            emit changed();
    }

private:
    VcsEditorDocument * const m_document;
    VcsEditorSections * const m_sections;
    int m_caretLine = 1;
};

// What is under the cursor that a plain click can do something with, asked
// in the order the widget editor asks its handlers: a change to describe,
// a URL to open, an address to mail.
static TextEditor::ActionLink actionLinkAt(VcsEditorDocument *document, const QTextCursor &cursor)
{
    TextEditor::ActionLink action;
    const int blockStart = cursor.block().position();

    const VcsBaseEditorParameters &parameters = document->parameters();
    if (parameters.changeUnderCursor) {
        const QString change = parameters.changeUnderCursor(cursor);
        if (!change.isEmpty()) {
            QTextCursor word = cursor;
            word.select(QTextCursor::WordUnderCursor);
            action.linkTextStart = word.selectionStart();
            action.linkTextEnd = word.selectionEnd();
            action.activate = [document = QPointer<VcsEditorDocument>(document), change] {
                if (!document)
                    return;
                const auto &describe = document->parameters().describeFunc;
                if (describe)
                    describe(VcsBase::source(document), change);
            };
            return action;
        }
    }

    const UrlUnderCursor url = urlUnderCursor(cursor);
    if (url.isValid()) {
        action.linkTextStart = blockStart + url.startColumn;
        action.linkTextEnd = action.linkTextStart + url.length;
        action.activate = [url = url.url] { QDesktopServices::openUrl(QUrl(url)); };
        return action;
    }

    const UrlUnderCursor mail = emailUnderCursor(cursor);
    if (mail.isValid()) {
        action.linkTextStart = blockStart + mail.startColumn;
        action.linkTextEnd = action.linkTextStart + mail.length;
        action.activate = [address = mail.url] {
            QDesktopServices::openUrl(QUrl("mailto:" + address));
        };
        return action;
    }
    return action;
}

class VcsEditorDocumentPrivate
{
public:
    VcsBaseEditorParameters parameters;
    FilePath workingDirectory;
    int defaultLineNumber = -1;
    QString annotateRevisionTextFormat;
    QString annotatePreviousRevisionTextFormat;
    bool fileLogAnnotateEnabled = false;
    QPointer<VcsBaseEditorConfig> config;
    QRegularExpression diffFilePattern;
    QRegularExpression logEntryPattern;
    Annotation annotation;
    VcsEditorDocument::DiffFileResolver diffFileResolver;
    VcsEditorDocument::BlockToString revisionSubject;
    VcsEditorDocument::OutputHook outputHook;
    BaseAnnotationHighlighterCreator annotationHighlighterCreator;
    QSingleTaskTreeRunner taskTreeRunner;
    VcsEditorSections *sections = nullptr;
    VcsSectionsChoice *choice = nullptr;
    QList<QAction *> contextActions;
    // What the VCS adds of its own, through the QMenu-filling functions its
    // client code already has; the menu owns those actions.
    std::unique_ptr<QMenu> extraMenu;
};

} // namespace Internal

bool DiffChunk::isValid() const
{
    return !fileName.isEmpty() && !chunk.isEmpty();
}

QByteArray DiffChunk::asPatch(const FilePath &workingDirectory) const
{
    const FilePath relativeFile = workingDirectory.isEmpty() ?
                fileName : fileName.relativeChildPath(workingDirectory);
    const QByteArray fileNameBA = QFile::encodeName(relativeFile.toUrlishString());
    QByteArray rc = "--- ";
    rc += fileNameBA;
    rc += "\n+++ ";
    rc += fileNameBA;
    rc += '\n';
    rc += chunk;
    return rc;
}

// Check for a chunk of
//       - changes          :  "@@ -91,7 +95,7 @@"
//       - merged conflicts : "@@@ -91,7 +95,7 @@@"
// and return the modified line number (here 95).
// Note that git appends stuff after "  @@"/" @@@" (function names, etc.).
static inline bool checkChunkLine(const QString &line, int *modifiedLineNumber, int numberOfAts)
{
    const QString ats(numberOfAts, '@');
    if (!line.startsWith(ats + ' '))
        return false;
    const int len = ats.size() + 1;
    const int endPos = line.indexOf(' ' + ats, len);
    if (endPos == -1)
        return false;
    // the first chunk range applies to the original file, the second one to
    // the modified file, the one we're interested in
    const int plusPos = line.indexOf('+', len);
    if (plusPos == -1 || plusPos > endPos)
        return false;
    const int lineNumberPos = plusPos + 1;
    const int commaPos = line.indexOf(',', lineNumberPos);
    if (commaPos == -1 || commaPos > endPos) {
        // Git submodule appears as "@@ -1 +1 @@"
        *modifiedLineNumber = 1;
        return true;
    }
    const QString lineNumberStr = line.mid(lineNumberPos, commaPos - lineNumberPos);
    bool ok;
    *modifiedLineNumber = lineNumberStr.toInt(&ok);
    return ok;
}

static inline bool checkChunkLine(const QString &line, int *modifiedLineNumber)
{
    if (checkChunkLine(line, modifiedLineNumber, 2))
        return true;
    return checkChunkLine(line, modifiedLineNumber, 3);
}

static void regexpFromString(
        const QString &pattern,
        QRegularExpression *regexp,
        QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption)
{
    const QRegularExpression re(pattern, options);
    QTC_ASSERT(re.isValid() && re.captureCount() >= 1, return);
    *regexp = re;
}

VcsEditorDocument::VcsEditorDocument(const VcsBaseEditorParameters &parameters)
    : TextDocument(parameters.id)
    , d(new Internal::VcsEditorDocumentPrivate)
{
    d->parameters = parameters;
    d->annotateRevisionTextFormat = Tr::tr("Annotate \"%1\"");
    d->sections = new VcsEditorSections(this);
    setMimeType(parameters.mimeType);
    setSuspendAllowed(false);
    // What the widget subclasses declared in their constructors, where the
    // VCS has moved it here.
    d->revisionSubject = parameters.revisionSubject;
    if (!parameters.diffFilePattern.isEmpty())
        setDiffFilePattern(parameters.diffFilePattern);
    if (!parameters.logEntryPattern.isEmpty())
        setLogEntryPattern(parameters.logEntryPattern);
    if (!parameters.annotationEntryPattern.isEmpty())
        setAnnotationEntryPattern(parameters.annotationEntryPattern);
    if (!parameters.annotationSeparatorPattern.isEmpty())
        setAnnotationSeparatorPattern(parameters.annotationSeparatorPattern);
    if (!parameters.annotateRevisionTextFormat.isEmpty())
        setAnnotateRevisionTextFormat(parameters.annotateRevisionTextFormat);
    if (!parameters.annotatePreviousRevisionTextFormat.isEmpty())
        setAnnotatePreviousRevisionTextFormat(parameters.annotatePreviousRevisionTextFormat);
    if (parameters.syntaxHighlighterCreator)
        updateHighlighter();
    // The sections follow the text. A log or a diff has them; the others
    // have nothing to find, and so nothing to offer in the tool bar either.
    if (parameters.type == LogOutput || parameters.type == DiffOutput) {
        connect(this, &TextDocument::contentsChanged, this, &VcsEditorDocument::updateSections);
        d->choice = new Internal::VcsSectionsChoice(this, d->sections);
    }
    // An annotation is coloured by its changes once there are any. The text
    // arrives later, so this waits for it.
    if (parameters.type == AnnotateOutput)
        connect(this, &TextDocument::contentsChanged, this, &VcsEditorDocument::activateAnnotation);
    // A line of a diff goes somewhere: Ctrl+click, Follow Symbol and Return
    // in a read-only view follow it, in whichever view. The line is the link
    // text; where it goes is the chunk header's answer, or the VCS's.
    if (parameters.type == LogOutput || parameters.type == DiffOutput) {
        setLinkFinder([this](TextEditor::TextDocument *, const QTextCursor &cursor,
                             const Utils::LinkHandler &callback, bool, bool) {
            const DiffTarget target = diffTargetAt(cursor);
            if (!target.isValid()) {
                callback(Utils::Link());
                return;
            }
            Utils::Link link(target.filePath, target.line);
            link.linkTextStart = cursor.block().position();
            link.linkTextEnd = cursor.block().position() + cursor.block().length() - 1;
            if (d->parameters.resolveDiffTarget)
                d->parameters.resolveDiffTarget(this, target, link, callback);
            else
                callback(link);
        });
    }
    // What a plain click does on a change, a URL or an address - in a log or
    // an annotation, which is where the widget editor's handlers are asked,
    // and wherever else the VCS says its output has them.
    if (parameters.type == LogOutput || parameters.type == AnnotateOutput
        || parameters.changeLinksInOtherContent) {
        setActionLinkFinder([this](TextEditor::TextDocument *, const QTextCursor &cursor) {
            return Internal::actionLinkAt(this, cursor);
        });
    }
}

VcsEditorDocument::~VcsEditorDocument()
{
    delete d;
}

const VcsBaseEditorParameters &VcsEditorDocument::parameters() const
{
    return d->parameters;
}

EditorContentType VcsEditorDocument::contentType() const
{
    return d->parameters.type;
}

FilePath VcsEditorDocument::workingDirectory() const
{
    return d->workingDirectory;
}

void VcsEditorDocument::setWorkingDirectory(const FilePath &workingDirectory)
{
    d->workingDirectory = workingDirectory;
}

int VcsEditorDocument::defaultLineNumber() const
{
    return d->defaultLineNumber;
}

void VcsEditorDocument::setDefaultLineNumber(int line)
{
    d->defaultLineNumber = line;
}

void VcsEditorDocument::setHighlightingEnabled(bool enabled)
{
    if (TextEditor::SyntaxHighlighter * const highlighter = syntaxHighlighter())
        highlighter->setEnabled(enabled);
}

void VcsEditorDocument::gotoDefaultLine()
{
    if (d->defaultLineNumber < 0)
        return;
    if (Core::IEditor * const editor = Internal::editorShowing(this))
        editor->gotoLine(d->defaultLineNumber, 0);
}

void VcsEditorDocument::executeTask(const ExecutableItem &task,
                                    const Storage<CommandResult> &resultStorage)
{
    if (d->taskTreeRunner.isRunning())
        d->taskTreeRunner.cancel();

    const auto onSetup = [this] { setBusy(true); };
    const auto onDone = [this, resultStorage](DoneWith doneWith) {
        setBusy(false);
        if (doneWith != DoneWith::Success) {
            setPlainText(Tr::tr("Failed to retrieve data."));
            VcsOutputWindow::appendError(resultStorage->workingDirectory(),
                                         resultStorage->cleanedStdErr());
            return;
        }
        setOutput(resultStorage->cleanedStdOut());
        gotoDefaultLine();
    };

    const Group recipe {
        resultStorage,
        onGroupSetup(onSetup),
        task,
        onGroupDone(onDone)
    };
    d->taskTreeRunner.start(recipe);
}

void VcsEditorDocument::setOutputHook(const OutputHook &hook)
{
    d->outputHook = hook;
}

void VcsEditorDocument::setOutput(const QString &output)
{
    if (d->parameters.putOutput)
        d->parameters.putOutput(this, output);
    else if (d->outputHook)
        d->outputHook(output);
    else
        setPlainText(output);
}

QString VcsEditorDocument::annotateRevisionTextFormat() const
{
    return d->annotateRevisionTextFormat;
}

void VcsEditorDocument::setAnnotateRevisionTextFormat(const QString &format)
{
    d->annotateRevisionTextFormat = format;
}

QString VcsEditorDocument::annotatePreviousRevisionTextFormat() const
{
    return d->annotatePreviousRevisionTextFormat;
}

void VcsEditorDocument::setAnnotatePreviousRevisionTextFormat(const QString &format)
{
    d->annotatePreviousRevisionTextFormat = format;
}

bool VcsEditorDocument::isFileLogAnnotateEnabled() const
{
    return d->fileLogAnnotateEnabled;
}

void VcsEditorDocument::setFileLogAnnotateEnabled(bool enabled)
{
    d->fileLogAnnotateEnabled = enabled;
}

VcsBaseEditorConfig *VcsEditorDocument::editorConfig() const
{
    return d->config;
}

void VcsEditorDocument::setEditorConfig(VcsBaseEditorConfig *config)
{
    d->config = config;
    emit toolBarActionsChanged();
    emit toolBarChoicesChanged();
    emit toolBarFieldsChanged();
}

QList<QAction *> VcsEditorDocument::ownToolBarActions() const
{
    return d->config ? d->config->actions() : QList<QAction *>();
}

void VcsEditorDocument::setDiffFilePattern(const QString &pattern)
{
    regexpFromString(pattern, &d->diffFilePattern);
    updateHighlighter();
    updateSections();
}

QRegularExpression VcsEditorDocument::diffFilePattern() const
{
    return d->diffFilePattern;
}

void VcsEditorDocument::setLogEntryPattern(const QString &pattern)
{
    regexpFromString(pattern, &d->logEntryPattern);
    updateHighlighter();
    updateSections();
}

// A log or a diff is highlighted by its own patterns: the file headers and the
// entries, and the folding that says where a header ends and a chunk begins,
// which diffChunk() and diffTargetAt() read. Installed here rather than by a
// view, so that every view of the document has it.
void VcsEditorDocument::updateHighlighter()
{
    if (d->parameters.syntaxHighlighterCreator) {
        resetSyntaxHighlighter(d->parameters.syntaxHighlighterCreator);
        return;
    }
    if (d->parameters.type != LogOutput && d->parameters.type != DiffOutput)
        return;
    resetSyntaxHighlighter(
        [diffFilePattern = d->diffFilePattern, logEntryPattern = d->logEntryPattern] {
            return new DiffAndLogHighlighter(diffFilePattern, logEntryPattern);
        });
}

QRegularExpression VcsEditorDocument::logEntryPattern() const
{
    return d->logEntryPattern;
}

void VcsEditorDocument::setAnnotationEntryPattern(const QString &pattern)
{
    regexpFromString(pattern, &d->annotation.entryPattern, QRegularExpression::MultilineOption);
}

void VcsEditorDocument::setAnnotationSeparatorPattern(const QString &pattern)
{
    regexpFromString(pattern, &d->annotation.separatorPattern);
}

Annotation VcsEditorDocument::annotation() const
{
    return d->annotation;
}

QSet<QString> VcsEditorDocument::annotationChanges() const
{
    QSet<QString> changes;
    const QString text = plainText();
    QStringView txt = QStringView(text);
    if (txt.isEmpty())
        return changes;
    if (!d->annotation.separatorPattern.pattern().isEmpty()) {
        const QRegularExpressionMatch match = d->annotation.separatorPattern.match(txt);
        if (match.hasMatch())
            txt.truncate(match.capturedStart());
    }
    QRegularExpressionMatchIterator i = d->annotation.entryPattern.globalMatch(txt);
    while (i.hasNext()) {
        const QRegularExpressionMatch match = i.next();
        changes.insert(match.captured(1));
    }
    return changes;
}

void VcsEditorDocument::setAnnotationHighlighterCreator(
    const BaseAnnotationHighlighterCreator &creator)
{
    d->annotationHighlighterCreator = creator;
}

void VcsEditorDocument::activateAnnotation()
{
    if (annotationChanges().isEmpty())
        return;
    const BaseAnnotationHighlighterCreator creator = d->parameters.annotationHighlighterCreator
                                                         ? d->parameters.annotationHighlighterCreator
                                                         : d->annotationHighlighterCreator;
    if (!creator)
        return;
    // Once: from here on the highlighter follows the text by itself.
    disconnect(this, &TextDocument::contentsChanged, this, &VcsEditorDocument::activateAnnotation);
    // An annotation highlighter already there is told to look again; anything
    // else - the generic one a Qt Quick editor gives a document that has none
    // yet - is replaced.
    if (auto * const highlighter = qobject_cast<BaseAnnotationHighlighter *>(syntaxHighlighter())) {
        highlighter->rehighlight();
        return;
    }
    resetSyntaxHighlighter([creator, annotation = d->annotation] { return creator(annotation); });
}

QString VcsEditorDocument::resolveDiffFile(const QString &fileName) const
{
    // Check if file is absolute
    const QFileInfo in(fileName);
    if (in.isAbsolute())
        return in.isFile() ? fileName : QString();

    // 1) Try base dir
    if (!d->workingDirectory.isEmpty()) {
        const FilePath baseFileInfo = d->workingDirectory.pathAppended(fileName);
        if (baseFileInfo.isFile())
            return baseFileInfo.absoluteFilePath().toUrlishString();
    }
    // 2) Try in source (which can be file or directory)
    const FilePath sourcePath = VcsBase::source(const_cast<VcsEditorDocument *>(this));
    if (!sourcePath.isEmpty()) {
        const FilePath sourceDir = sourcePath.isDir() ? sourcePath.absoluteFilePath()
                                                      : sourcePath.absolutePath();
        const FilePath sourceFileInfo = sourceDir.pathAppended(fileName);
        if (sourceFileInfo.isFile())
            return sourceFileInfo.absoluteFilePath().toUrlishString();

        const FilePath topLevel = Core::VcsManager::findTopLevelForDirectory(sourceDir);
        if (topLevel.isEmpty())
            return {};

        const FilePath topLevelFile = topLevel.pathAppended(fileName);
        if (topLevelFile.isFile())
            return topLevelFile.absoluteFilePath().toUrlishString();
    }

    // 3) Try working directory
    if (in.isFile())
        return in.absoluteFilePath();

    // 4) remove trailing tab char and try again: At least git appends \t when the
    //    filename contains spaces. Since the diff command does use \t all of a sudden,
    //    too, when seeing spaces in a filename, I expect the same behavior in other
    //    version control systems.
    if (fileName.endsWith('\t'))
        return resolveDiffFile(fileName.left(fileName.size() - 1));

    return {};
}

void VcsEditorDocument::setDiffFileResolver(const DiffFileResolver &resolver)
{
    d->diffFileResolver = resolver;
    updateSections();
}

QString VcsEditorDocument::findDiffFile(const QString &fileName) const
{
    return d->diffFileResolver ? d->diffFileResolver(fileName) : resolveDiffFile(fileName);
}

QString VcsEditorDocument::fileNameFromDiffSpecification(const QTextBlock &inBlock,
                                                         QString *header) const
{
    // Go back chunks
    QString fileName;
    for (QTextBlock block = inBlock; block.isValid(); block = block.previous()) {
        const QString line = block.text();
        const QRegularExpressionMatch match = d->diffFilePattern.match(line);
        if (match.hasMatch()) {
            const QString cap = match.captured(1);
            if (header)
                header->prepend(line + "\n");
            if (fileName.isEmpty() && !cap.isEmpty())
                fileName = cap;
        } else if (!fileName.isEmpty()) {
            return findDiffFile(fileName);
        } else if (header) {
            header->clear();
        }
    }
    return fileName.isEmpty() ? QString() : findDiffFile(fileName);
}

// cut out chunk and determine file name.
DiffChunk VcsEditorDocument::diffChunk(const QTextCursor &cursor) const
{
    DiffChunk rc;
    if (d->parameters.type != LogOutput && d->parameters.type != DiffOutput)
        return rc;
    // Search back for start of chunk.
    QTextBlock block = cursor.block();
    if (block.isValid() && TextEditor::TextBlockUserData::foldingIndent(block) <= 1) {
        // We are in a diff header, not in a chunk!
        // DiffAndLogHighlighter sets the foldingIndent for us.
        return rc;
    }

    int chunkStart = 0;
    for ( ; block.isValid() ; block = block.previous()) {
        if (checkChunkLine(block.text(), &chunkStart))
            break;
    }
    if (!chunkStart || !block.isValid())
        return rc;
    QString header;
    rc.fileName = FilePath::fromUserInput(findDiffFile(fileNameFromDiffSpecification(block, &header)));
    if (rc.fileName.isEmpty())
        return rc;
    // Concatenate chunk and convert
    QString unicode = block.text();
    if (!unicode.endsWith('\n')) // Missing in case of hg.
        unicode.append('\n');
    for (block = block.next() ; block.isValid() ; block = block.next()) {
        const QString line = block.text();
        if (checkChunkLine(line, &chunkStart) || d->diffFilePattern.match(line).capturedStart() == 0)
            break;
        unicode += line;
        unicode += '\n';
    }
    const TextEncoding textEncoding = encoding();
    if (textEncoding.isValid()) {
        rc.chunk = textEncoding.encode(unicode);
        rc.header = textEncoding.encode(header);
    } else {
        rc.chunk = unicode.toLocal8Bit();
        rc.header = header.toLocal8Bit();
    }
    return rc;
}

bool VcsEditorDocument::canApplyDiffChunk(const DiffChunk &chunk) const
{
    if (!chunk.isValid())
        return false;
    // Default implementation using patch.exe relies on absolute paths.
    return chunk.fileName.isFile() && chunk.fileName.isAbsolutePath()
           && chunk.fileName.isWritableFile();
}

// Default implementation of revert: Apply a chunk by piping it into patch,
// (passing '-R' for revert), assuming we got absolute paths from the version control plugins.
bool VcsEditorDocument::applyDiffChunk(const DiffChunk &chunk, Core::PatchAction patchAction) const
{
    return Core::PatchTool::runPatch(chunk.asPatch(d->workingDirectory), d->workingDirectory,
                                     0, patchAction);
}

// Asks first, saves the file's open document, applies, and says so when a
// chunk was reverted - whoever showed the diff reloads it then.
void VcsEditorDocument::applyChunk(const DiffChunk &chunk, Core::PatchAction patchAction)
{
    auto * const target = qobject_cast<TextEditor::TextDocument *>(
        Core::DocumentModel::documentForFilePath(chunk.fileName));
    const bool isModified = target && target->isModified();
    if (!Core::PatchTool::confirmPatching(Core::ICore::dialogParent(), patchAction, isModified))
        return;
    if (target && !Core::EditorManager::saveDocument(target))
        return;
    if (applyDiffChunk(chunk, patchAction) && patchAction == Core::PatchAction::Revert)
        emit diffChunkReverted();
}

DiffTarget VcsEditorDocument::diffTargetAt(const QTextCursor &cursor) const
{
    DiffTarget target;
    int chunkStart = 0;
    int lineCount = -1;
    const QChar deletionIndicator = '-';
    // find nearest change hunk
    QTextBlock block = cursor.block();
    if (TextEditor::TextBlockUserData::foldingIndent(block) <= 1) {
        // We are in a diff header, do not jump anywhere.
        // DiffAndLogHighlighter sets the foldingIndent for us.
        return target;
    }
    for ( ; block.isValid() ; block = block.previous()) {
        const QString line = block.text();
        if (checkChunkLine(line, &chunkStart))
            break;
        if (!line.startsWith(deletionIndicator))
            ++lineCount;
    }

    if (chunkStart == -1 || lineCount < 0 || !block.isValid())
        return target;

    // find the filename in previous line, map depot name back
    block = block.previous();
    if (!block.isValid())
        return target;
    const QString fileName = findDiffFile(fileNameFromDiffSpecification(block));

    const bool exists = fileName.isEmpty() ? false : QFileInfo::exists(fileName);
    if (!exists)
        return target;

    target.filePath = FilePath::fromUserInput(fileName);
    target.line = chunkStart + lineCount;
    target.contextBlock = block;
    return target;
}

QString VcsEditorDocument::revisionForLine(int line) const
{
    const int section = d->sections->sectionOfLine(line);
    if (section >= 0 && section < d->sections->rowCount()) {
        const int sectionLine = d->sections->lines().at(section);
        const QTextBlock sectionBlock = document()->findBlockByLineNumber(sectionLine);
        if (sectionBlock.isValid()) {
            const QRegularExpressionMatch match = d->logEntryPattern.match(sectionBlock.text());
            if (match.hasMatch())
                return match.captured(1);
        }
    }

    for (QTextBlock block = document()->findBlockByLineNumber(line); block.isValid();
         block = block.previous()) {
        const QRegularExpressionMatch match = d->logEntryPattern.match(block.text());
        if (match.hasMatch())
            return match.captured(1);
    }
    return {};
}

void VcsEditorDocument::setRevisionSubjectHook(const BlockToString &revisionSubject)
{
    d->revisionSubject = revisionSubject;
    updateSections();
}

VcsEditorSections *VcsEditorDocument::sections() const
{
    return d->sections;
}

QList<TextEditor::ToolBarChoice *> VcsEditorDocument::toolBarChoices() const
{
    QList<TextEditor::ToolBarChoice *> choices;
    if (d->choice)
        choices.append(d->choice);
    if (d->config) {
        const QList<VcsBaseEditorChoice *> own = d->config->choices();
        for (VcsBaseEditorChoice * const choice : own)
            choices.append(choice);
    }
    return choices;
}

QList<TextEditor::ToolBarField *> VcsEditorDocument::toolBarFields() const
{
    return d->config ? d->config->fields() : QList<TextEditor::ToolBarField *>();
}

QList<QAction *> VcsEditorDocument::contextMenuActions(const QTextCursor &cursor)
{
    // The previous click's are gone with its menu; these live until the next.
    qDeleteAll(d->contextActions);
    d->contextActions.clear();
    if (!d->extraMenu)
        d->extraMenu.reset(new QMenu);
    d->extraMenu->clear();
    if (cursor.isNull())
        return {};

    const auto add = [this](const QString &text, const std::function<void()> &act) {
        auto * const action = new QAction(text, this);
        connect(action, &QAction::triggered, this, act);
        d->contextActions.append(action);
    };
    // The VCS's own entries, appended in place: filled into the menu that
    // owns them, and only the ones this fill added handed back.
    QList<QAction *> extras;
    const auto addExtras = [this, &extras](const std::function<void(QMenu *)> &fill) {
        const int before = int(d->extraMenu->actions().size());
        fill(d->extraMenu.get());
        extras += d->extraMenu->actions().mid(before);
    };
    const VcsBaseEditorParameters &p = d->parameters;
    const EditorContentType type = p.type;

    // What is under the pointer, in the order the widget's handlers are asked.
    if (type == LogOutput || type == AnnotateOutput || p.changeLinksInOtherContent) {
        const QString change = d->parameters.changeUnderCursor
                                   ? d->parameters.changeUnderCursor(cursor) : QString();
        Internal::UrlUnderCursor url;
        Internal::UrlUnderCursor mail;
        if (change.isEmpty())
            url = Internal::urlUnderCursor(cursor);
        if (change.isEmpty() && !url.isValid())
            mail = Internal::emailUnderCursor(cursor);
        if (!change.isEmpty()) {
            const bool valid = !p.isValidRevision || p.isValidRevision(change);
            const auto decorated = [this, &p](const QString &revision) {
                return p.decorateVersion ? p.decorateVersion(this, revision) : revision;
            };
            const int line = cursor.blockNumber() + 1;
            const auto annotateEntry = [this, &add, line](const QString &text, const QString &revision) {
                add(text, [this, revision, line] { requestAnnotation(revision, line); });
            };
            const auto describeEntry = [this, &add, change] {
                add(Tr::tr("&Describe Change %1").arg(change), [this, change] {
                    if (d->parameters.describeFunc)
                        d->parameters.describeFunc(VcsBase::source(this), change);
                });
            };
            add(Tr::tr("Copy \"%1\"").arg(change), [change] { Utils::setClipboardAndSelection(change); });
            if (type == AnnotateOutput) {
                // The revision itself, where it is one, and the revisions
                // before it as the VCS names them - as the widget's handler
                // built it for an annotation.
                if (valid) {
                    describeEntry();
                    annotateEntry(d->annotateRevisionTextFormat.arg(decorated(change)), change);
                }
                const QStringList previous = p.annotationPreviousVersions
                                                 ? p.annotationPreviousVersions(this, change)
                                                 : QStringList();
                const QString previousFormat = d->annotatePreviousRevisionTextFormat.isEmpty()
                                                   ? d->annotateRevisionTextFormat
                                                   : d->annotatePreviousRevisionTextFormat;
                for (const QString &revision : previous)
                    annotateEntry(previousFormat.arg(decorated(revision)), revision);
            } else {
                // A log offers the file annotated at the change, where the
                // VCS has said its log can.
                describeEntry();
                if (d->fileLogAnnotateEnabled)
                    annotateEntry(d->annotateRevisionTextFormat.arg(change), change);
            }
            if (p.addChangeActions) {
                addExtras([this, &p, change, line](QMenu *menu) {
                    p.addChangeActions(menu, this, change, line);
                });
            }
        } else if (url.isValid()) {
            add(Tr::tr("Open URL in Browser..."),
                [address = url.url] { QDesktopServices::openUrl(QUrl(address)); });
            add(Tr::tr("Copy URL Location"),
                [address = url.url] { Utils::setClipboardAndSelection(address); });
        } else if (mail.isValid()) {
            add(Tr::tr("Send Email To..."), [address = mail.url] {
                QDesktopServices::openUrl(QUrl("mailto:" + address));
            });
            add(Tr::tr("Copy Email Address"),
                [address = mail.url] { Utils::setClipboardAndSelection(address); });
        }
    }

    // A log or a diff can be pasted, where there is somewhere to paste it to;
    // and the chunk the click is in can be applied or reverted, where there
    // is a file to do it to. Queued, as the widget's were: the confirmation
    // is a dialog, and the menu is still closing when the entry fires.
    if (type == LogOutput || type == DiffOutput) {
        if (auto * const paste = ExtensionSystem::PluginManager::getObject<CodePaster::Service>())
            add(Tr::tr("Send to CodePaster..."), [paste] { paste->postCurrentEditor(); });
        const DiffChunk chunk = diffChunk(cursor);
        if (canApplyDiffChunk(chunk)) {
            add(Tr::tr("Apply Chunk..."), [this, chunk] {
                QMetaObject::invokeMethod(this, [this, chunk] {
                    applyChunk(chunk, Core::PatchAction::Apply);
                }, Qt::QueuedConnection);
            });
            add(Tr::tr("Revert Chunk..."), [this, chunk] {
                QMetaObject::invokeMethod(this, [this, chunk] {
                    applyChunk(chunk, Core::PatchAction::Revert);
                }, Qt::QueuedConnection);
            });
            if (p.addDiffActions)
                addExtras([this, &p, chunk](QMenu *menu) { p.addDiffActions(menu, this, chunk); });
        }
    }
    return d->contextActions + extras;
}

// What the widget's slotAnnotateRevision() works out: the working directory
// from this document or the VCS above the source, and the file relative to
// it. The source stands in for the widget's fileNameForLine(), which only Git
// answers differently.
void VcsEditorDocument::requestAnnotation(const QString &change, int line)
{
    const FilePath fileName = (d->parameters.fileNameForLine
                                   ? d->parameters.fileNameForLine(this, line)
                                   : VcsBase::source(this)).canonicalPath();
    const FilePath ownWorkingDirectory = d->workingDirectory;
    const FilePath workingDirectory = ownWorkingDirectory.isEmpty()
            ? Core::VcsManager::findTopLevelForDirectory(fileName.parentDir())
            : ownWorkingDirectory;
    const FilePath relativePath = fileName.isRelativePath()
            ? fileName
            : fileName.relativeChildPath(workingDirectory);
    emit annotateRevisionRequested(workingDirectory, relativePath.toUrlishString(), change, line);
}

void VcsEditorDocument::updateSections()
{
    QStringList entries;
    QList<int> lines;
    QTextDocument * const text = document();
    const QTextBlock end = text->end();
    int lineNumber = 0;
    switch (d->parameters.type) {
    case DiffOutput: {
        // One section per diffed file, headers skipped, a file repeated
        // under the last one counted once.
        QString lastFileName;
        for (QTextBlock it = text->begin(); it != end; it = it.next(), ++lineNumber) {
            if (d->diffFilePattern.match(it.text()).capturedStart() != 0)
                continue;
            const QString file = fileNameFromDiffSpecification(it);
            if (file.isEmpty() || lastFileName == file)
                continue;
            lastFileName = file;
            lines.push_back(lines.empty() ? 0 : lineNumber);
            entries.append(FilePath::fromString(file).fileName());
        }
        break;
    }
    case LogOutput:
        // One section per log entry, named by what the pattern captures and
        // the entry's subject if there is one.
        for (QTextBlock it = text->begin(); it != end; it = it.next(), ++lineNumber) {
            const QRegularExpressionMatch match = d->logEntryPattern.match(it.text());
            if (!match.hasMatch())
                continue;
            lines.push_back(lines.empty() ? 0 : lineNumber);
            QString entry = match.captured(1);
            QString subject = d->revisionSubject ? d->revisionSubject(it) : QString();
            if (!subject.isEmpty()) {
                if (subject.size() > 100) {
                    subject.truncate(97);
                    subject.append("...");
                }
                entry.append(" - ").append(subject);
            }
            entries.append(entry);
        }
        break;
    default:
        break;
    }
    d->sections->reset(entries, lines);
}

} // namespace VcsBase

#ifdef WITH_TESTS

#include "vcsbaseclient.h"
#include "vcsbaseclientsettings.h"
#include "vcsbaseeditor.h"

#include <solutions/spinner/spinner.h>

#include <utils/algorithm.h>
#include <utils/aspects.h>
#include <utils/environment.h>
#include <utils/fancylineedit.h>
#include <utils/temporarydirectory.h>

#include <QClipboard>
#include <QComboBox>
#include <QGuiApplication>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>
#include <QTextLayout>
#include <QToolBar>

namespace VcsBase {

class VcsEditorDocumentTest final : public QObject
{
    Q_OBJECT

private slots:
    // What a VCS editor is configured with lives on its document, and the
    // widget editor's accessors are a view on it: set on either side, seen
    // on the other. What a subclass declares in its constructor, before there
    // is a document, reaches the document too. And the sections follow the
    // text, with the one thing the document cannot know - a log entry's
    // subject - asked of the widget that knows.
    void testTheWidgetEditorsStateIsTheDocuments()
    {
        class LogWidget final : public VcsBaseEditorWidget
        {
        public:
            LogWidget() { setLogEntryPattern("^entry ([0-9]+)"); }

        private:
            QString changeUnderCursor(const QTextCursor &) const final { return {}; }
            QString revisionSubject(const QTextBlock &block) const final
            {
                return block.next().text().trimmed();
            }
        };
        const VcsBaseEditorParameters parameters{LogOutput,
                                                 "VcsEditorDocumentTest.Log",
                                                 "VCS document test log",
                                                 "text/vnd.qtcreator.vcs-document-test",
                                                 [] { return new LogWidget; },
                                                 [](const FilePath &, const QString &) {}};
        VcsEditorFactory factory(parameters);
        // What the widget subclass says about its gutter in its constructor,
        // said by the factory too, for the view that has no widget.
        QVERIFY2(!factory.marksVisible(), "a VCS view has a column for marks it cannot carry");
        QVERIFY2(!factory.revisionsVisible(), "a VCS view marks edited lines nobody saves");
        // Through the editor manager, as a client opens one: the choice below
        // jumps in the editor the manager knows for the document.
        QString title = "VCS document test";
        Core::IEditor * const editor
            = Core::EditorManager::openEditorWithContents(parameters.id, &title, QByteArray());
        QVERIFY(editor);
        const QScopeGuard closeIt([editor] { Core::EditorManager::closeEditors({editor}, false); });
        VcsBaseEditorWidget * const widget = VcsBaseEditor::getVcsBaseEditor(editor);
        QVERIFY2(widget, "the factory did not build a VCS editor");
        VcsEditorDocument * const document = widget->vcsDocument();
        QVERIFY2(document, "the VCS editor's document is not a VCS document");
        QCOMPARE(document->contentType(), LogOutput);
        QCOMPARE(widget->contentType(), LogOutput);

        widget->setWorkingDirectory(FilePath::fromString("/somewhere/checked/out"));
        QCOMPARE(document->workingDirectory(), FilePath::fromString("/somewhere/checked/out"));
        document->setFirstLineNumber(41);
        QCOMPARE(widget->firstLineNumber(), 41);
        widget->setDefaultLineNumber(7);
        QCOMPARE(document->defaultLineNumber(), 7);

        QVERIFY2(!document->logEntryPattern().pattern().isEmpty(),
                 "the pattern the constructor declared never reached the document");

        document->setPlainText("entry 1\n  first\nmore\nentry 2\n  second\n");
        QCOMPARE(document->sections()->entries(), QStringList({"1 - first", "2 - second"}));
        QCOMPARE(document->sections()->lines(), QList<int>({0, 3}));
        QCOMPARE(document->sections()->sectionOfLine(2), 0);
        QCOMPARE(document->sections()->sectionOfLine(4), 1);
        QCOMPARE(document->sections()->data(document->sections()->index(1), VcsEditorSections::LineRole).toInt(), 3);

        // And the sections are what the tool bar offers: the row the caret is
        // in is current, and choosing a row goes there. Not a pick to undo.
        TextEditor::ToolBarChoice * const choice = document->toolBarChoices().value(0);
        QVERIFY2(choice, "a log document offers the tool bar no choice");
        QCOMPARE(choice->model(), document->sections());
        QVERIFY2(choice->isAvailable(), "two sections, and nothing to choose between");
        QVERIFY2(!choice->isChosen(), "a jump is offered as a pick to undo");
        choice->followCaret(5);
        QCOMPARE(choice->currentIndex(), 1);
        choice->followCaret(2);
        QCOMPARE(choice->currentIndex(), 0);
        // Setting the text left the widget editor's caret at the end; what
        // matters is that a choice moves it, in both directions.
        QVERIFY2(editor->currentLine() != 4, "the caret starts where the jump would land");
        choice->choose(1);
        QVERIFY2(editor->currentLine() == 4,
                 qPrintable(QString("choosing the second entry left the caret on line %1")
                                .arg(editor->currentLine())));
        choice->choose(0);
        QVERIFY2(editor->currentLine() == 1,
                 qPrintable(QString("choosing the first entry left the caret on line %1")
                                .arg(editor->currentLine())));
    }

    // What a log holds that a plain click can do something with: a change,
    // a URL, a Jira key, an address - each offered over exactly itself, the
    // change described with the document's source. Nothing over anything
    // else, and nothing at all in a diff, where the widget editor's handlers
    // were never asked either.
    // What a VCS client does once it has an editor: hands it a command whose
    // output becomes the text. That ran on the widget, with a spinner over it
    // meanwhile and a jump to the line asked for after. It is the document's
    // now, so that a view of any kind can show it; the widget still shows the
    // spinner, and still shapes the output through its virtual setPlainText().
    void testACommandsOutputBecomesTheDocumentsText()
    {
        const FilePath sh = FilePath::fromString("sh").searchInPath();
        if (sh.isEmpty())
            QSKIP("no sh to run a command with");

        class CommandWidget final : public VcsBaseEditorWidget
        {
        public:
            CommandWidget() = default;
            void setPlainText(const QString &text) final
            {
                VcsBaseEditorWidget::setPlainText(text.toUpper());
            }

        private:
            QString changeUnderCursor(const QTextCursor &) const final { return {}; }
        };
        const VcsBaseEditorParameters parameters{OtherContent,
                                                 "VcsEditorDocumentTest.Command",
                                                 "VCS document test command",
                                                 "text/vnd.qtcreator.vcs-document-test-command",
                                                 [] { return new CommandWidget; },
                                                 [](const FilePath &, const QString &) {}};
        VcsEditorFactory factory(parameters);
        QString title = "VCS document test command";
        Core::IEditor * const editor
            = Core::EditorManager::openEditorWithContents(parameters.id, &title, QByteArray());
        QVERIFY(editor);
        const QScopeGuard closeIt([editor] { Core::EditorManager::closeEditors({editor}, false); });
        VcsBaseEditorWidget * const widget = VcsBaseEditor::getVcsBaseEditor(editor);
        QVERIFY2(widget, "the factory did not build a VCS editor");
        VcsEditorDocument * const document = widget->vcsDocument();
        QVERIFY(document);
        auto * const spinner = widget->findChild<SpinnerSolution::Spinner *>();
        QVERIFY2(spinner, "the widget has no spinner to show over a running command");
        QVERIFY(!spinner->isVisible());
        QVERIFY(!document->isBusy());

        TemporaryDirectory dir("vcs-command-test");
        QVERIFY(dir.isValid());
        // The command waits for a file the test writes once it has seen the
        // spinner, so that "shown while running" is not a race against how
        // fast the shell answers.
        const FilePath gate = dir.filePath("gate");
        widget->setDefaultLineNumber(3);
        {
            const Storage<CommandResult> resultStorage;
            const ProcessTask task = vcsProcessTask(
                {.runData = {{sh, {"-c", "until [ -e \"$0\" ]; do sleep 0.02; done; "
                                        "printf 'one\\ntwo\\nthree\\n'",
                                  gate.path()}},
                             dir.path(),
                             Environment::systemEnvironment()}},
                resultStorage);
            widget->executeTask(task, resultStorage);
        }
        QVERIFY2(document->isBusy(), "starting a command did not make the document busy");
        QTRY_VERIFY2(spinner->isVisible(), "the widget shows no spinner over a running command");
        QVERIFY(gate.writeFileContents("go"));
        QTRY_VERIFY(!document->isBusy());
        QVERIFY2(!spinner->isVisible(), "the spinner outlived the command");
        // Through the widget's setPlainText(), which this one upper-cases.
        QCOMPARE(document->plainText(), QString("ONE\nTWO\nTHREE\n"));
        QCOMPARE(editor->currentLine(), 3);

        // A command that fails leaves a line saying so - as it is, not through
        // the hook: it is not output.
        {
            const Storage<CommandResult> resultStorage;
            const ProcessTask task = vcsProcessTask(
                {.runData = {{sh, {"-c", "exit 3"}}, dir.path(), Environment::systemEnvironment()}},
                resultStorage);
            widget->executeTask(task, resultStorage);
        }
        QVERIFY(document->isBusy());
        QTRY_VERIFY(!document->isBusy());
        QCOMPARE(document->plainText(), Tr::tr("Failed to retrieve data."));
    }

    // An annotation's lines are coloured by change. The widget did that by
    // installing the highlighter its VCS subclass named, once the text had
    // changes to colour; the document does it now - from the parameters where
    // the VCS has put it there, and from the widget's answer where it has not.
    void testAnAnnotationIsColouredByTheDocument()
    {
        class TestAnnotationHighlighter final : public BaseAnnotationHighlighter
        {
        public:
            using BaseAnnotationHighlighter::BaseAnnotationHighlighter;

        private:
            QString changeNumber(const QString &block) const final { return block.left(8); }
        };
        const auto colourOfLine = [](VcsEditorDocument *document, int line) -> std::optional<QColor> {
            const QTextBlock block = document->document()->findBlockByNumber(line);
            const QList<QTextLayout::FormatRange> formats = block.layout()->formats();
            if (formats.isEmpty())
                return {};
            return formats.first().format.foreground().color();
        };
        const QString text = "aaaaaaaa 1 (x) one\nbbbbbbbb 2 (y) two\naaaaaaaa 3 (x) three\n";

        // From the parameters, with no widget anywhere near.
        VcsBaseEditorParameters parameters{AnnotateOutput,
                                           "VcsEditorDocumentTest.Coloured",
                                           "VCS document test coloured annotation",
                                           "text/vnd.qtcreator.vcs-document-coloured-test",
                                           {},
                                           [](const FilePath &, const QString &) {}};
        parameters.annotationHighlighterCreator
            = getAnnotationHighlighterCreator<TestAnnotationHighlighter>();
        VcsEditorDocument document(parameters);
        document.setAnnotationEntryPattern("^([0-9a-f]{8}) ");
        QVERIFY2(!document.syntaxHighlighter(), "an empty annotation already has a highlighter");
        document.setPlainText(text);
        QCOMPARE(document.annotationChanges().size(), 2);
        QVERIFY2(qobject_cast<BaseAnnotationHighlighter *>(document.syntaxHighlighter()),
                 "the text arrived and no annotation highlighter was installed");
        QTRY_VERIFY2(colourOfLine(&document, 0), "the first line got no colour");
        QTRY_VERIFY2(colourOfLine(&document, 1), "the second line got no colour");
        QCOMPARE(colourOfLine(&document, 2), colourOfLine(&document, 0));
        QVERIFY2(colourOfLine(&document, 1) != colourOfLine(&document, 0),
                 "two changes got the same colour");

        // From the widget's answer, for a VCS whose parameters do not say yet.
        class AnswersWidget final : public VcsBaseEditorWidget
        {
        public:
            AnswersWidget() { setAnnotationEntryPattern("^([0-9a-f]{8}) "); }

        private:
            QString changeUnderCursor(const QTextCursor &) const final { return {}; }
            BaseAnnotationHighlighterCreator annotationHighlighterCreator() const final
            {
                return getAnnotationHighlighterCreator<TestAnnotationHighlighter>();
            }
        };
        const VcsBaseEditorParameters widgetParameters{
            AnnotateOutput,
            "VcsEditorDocumentTest.ColouredByWidget",
            "VCS document test coloured by widget",
            "text/vnd.qtcreator.vcs-document-coloured-widget-test",
            [] { return new AnswersWidget; },
            [](const FilePath &, const QString &) {}};
        VcsEditorFactory factory(widgetParameters);
        QString title = "VCS document test coloured";
        Core::IEditor * const editor
            = Core::EditorManager::openEditorWithContents(widgetParameters.id, &title, QByteArray());
        QVERIFY(editor);
        const QScopeGuard closeIt([editor] { Core::EditorManager::closeEditors({editor}, false); });
        auto * const answered = qobject_cast<VcsEditorDocument *>(editor->document());
        QVERIFY(answered);
        answered->setPlainText(text);
        QVERIFY2(qobject_cast<BaseAnnotationHighlighter *>(answered->syntaxHighlighter()),
                 "the widget's highlighter never reached the document");
        QTRY_VERIFY2(colourOfLine(answered, 1),
                     "the second line got no colour through the widget's answer");
    }

    // What a VCS command can be told - toggles and choices - used to build
    // itself into the widget's QToolBar. It lists itself now and the views
    // draw the list: the toggles through the document's tool bar actions,
    // which both views draw already, and a choice as a combo box on the
    // widget; the Qt Quick tool bar's combo is the next batch's.
    void testAnEditorConfigListsItselfAndTheViewsDrawIt()
    {
        VcsBaseEditorConfig config;
        config.setBaseArguments({"log"});
        QAction * const whitespace = config.addToggleButton("-w", "Ignore Whitespace");
        QAction * const firstParent
            = config.addToggleButton({"-m", "--first-parent"}, "First Parent");
        VcsBaseEditorChoice * const moves = config.addChoices(
            "Move detection", {}, {{"None", ""}, {"Within", "-M"}, {"Between", "-M -C"}});
        QAction * const reload = config.addReloadButton();
        QSignalSpy changed(&config, &VcsBaseEditorConfig::argumentsChanged);

        // What it lists.
        QCOMPARE(config.actions(), (QList<QAction *>{whitespace, firstParent, reload}));
        QCOMPARE(config.choices(), QList<VcsBaseEditorChoice *>{moves});
        QCOMPARE(moves->count(), 3);
        QCOMPARE(moves->model()->rowCount(), 3);
        QCOMPARE(moves->model()->index(2, 0).data().toString(), QString("Between"));
        QCOMPARE(moves->toolTip(), QString("Move detection"));
        QCOMPARE(moves->currentIndex(), 0);
        QVERIFY(moves->isAvailable());
        QVERIFY(!moves->isChosen());

        // What it amounts to.
        QCOMPARE(config.arguments(), QStringList{"log"});
        whitespace->setChecked(true);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(config.arguments(), (QStringList{"log", "-w"}));
        moves->choose(2);
        QCOMPARE(changed.count(), 2);
        QCOMPARE(moves->currentValue().toString(), QString("-M -C"));
        QCOMPARE(config.arguments(), (QStringList{"log", "-w", "-M", "-C"}));
        moves->choose(2);
        QCOMPARE(changed.count(), 2);

        // A setting says where to start, without running anything; a change
        // is written back to it.
        BoolAspect parentSetting;
        parentSetting.setValue(true);
        config.mapSetting(firstParent, &parentSetting);
        QVERIFY(firstParent->isChecked());
        QCOMPARE(changed.count(), 2);
        firstParent->setChecked(false);
        QCOMPARE(changed.count(), 3);
        QVERIFY(!parentSetting.value());
        StringAspect moveSetting;
        moveSetting.setValue("-M");
        config.mapSetting(moves, &moveSetting);
        QCOMPARE(moves->currentIndex(), 1);
        QCOMPARE(changed.count(), 3);
        moves->choose(0);
        QCOMPARE(changed.count(), 4);
        QCOMPARE(moveSetting.value(), QString());

        // A field: typing announces nothing, committing does. What it amounts
        // to is the VCS's business.
        TextEditor::ToolBarField * const grep = config.addTextField("Filter by message", "By message");
        QCOMPARE(config.fields(), QList<TextEditor::ToolBarField *>{grep});
        grep->setText("fix");
        QCOMPARE(changed.count(), 4);
        grep->commit();
        QCOMPARE(changed.count(), 5);

        // On a document: the toggles are its tool bar actions, and the views
        // are told. On the widget: a combo box per choice, in step both ways.
        class ConfigWidget final : public VcsBaseEditorWidget
        {
        public:
            ConfigWidget() = default;

        private:
            QString changeUnderCursor(const QTextCursor &) const final { return {}; }
        };
        const VcsBaseEditorParameters parameters{OtherContent,
                                                 "VcsEditorDocumentTest.Config",
                                                 "VCS document test config",
                                                 "text/vnd.qtcreator.vcs-document-config-test",
                                                 [] { return new ConfigWidget; },
                                                 [](const FilePath &, const QString &) {}};
        VcsEditorFactory factory(parameters);
        QString title = "VCS document test config";
        Core::IEditor * const editor
            = Core::EditorManager::openEditorWithContents(parameters.id, &title, QByteArray());
        QVERIFY(editor);
        const QScopeGuard closeIt([editor] { Core::EditorManager::closeEditors({editor}, false); });
        VcsBaseEditorWidget * const widget = VcsBaseEditor::getVcsBaseEditor(editor);
        QVERIFY(widget);
        VcsEditorDocument * const document = widget->vcsDocument();
        QVERIFY(document);
        QVERIFY(!document->toolBarActions().contains(whitespace));
        QVERIFY(document->toolBarChoices().isEmpty());
        QVERIFY(document->toolBarFields().isEmpty());
        QSignalSpy told(document, &TextEditor::TextDocument::toolBarActionsChanged);
        QSignalSpy toldChoices(document, &TextEditor::TextDocument::toolBarChoicesChanged);
        QSignalSpy toldFields(document, &TextEditor::TextDocument::toolBarFieldsChanged);
        widget->setEditorConfig(&config);
        QCOMPARE(told.count(), 1);
        QCOMPARE(toldChoices.count(), 1);
        QCOMPARE(toldFields.count(), 1);
        QCOMPARE(document->toolBarChoices(), QList<TextEditor::ToolBarChoice *>{moves});
        QCOMPARE(document->toolBarFields(), QList<TextEditor::ToolBarField *>{grep});
        const QList<QAction *> offered = document->toolBarActions();
        QVERIFY2(offered.contains(whitespace) && offered.contains(firstParent)
                     && offered.contains(reload),
                 "the config's toggles are not the document's tool bar actions");
        QVERIFY2(widget->toolBar()->actions().contains(whitespace),
                 "the widget's tool bar does not draw the document's toggle");
        auto * const combo = widget->toolBar()->findChild<QComboBox *>();
        QVERIFY2(combo, "the widget's tool bar has no combo box for the choice");
        QCOMPARE(combo->count(), 3);
        QCOMPARE(combo->currentIndex(), 0);
        combo->setCurrentIndex(2);
        QCOMPARE(moves->currentIndex(), 2);
        QCOMPARE(changed.count(), 6);
        moves->choose(1);
        QCOMPARE(combo->currentIndex(), 1);
        QCOMPARE(changed.count(), 7);

        // And a line edit per field, typing into which is typing into the
        // field, Return committing, and shown as the field is.
        auto * const edit = widget->toolBar()->findChild<FancyLineEdit *>();
        QVERIFY2(edit, "the widget's tool bar has no line edit for the field");
        QCOMPARE(edit->placeholderText(), QString("Filter by message"));
        QCOMPARE(edit->text(), QString("fix"));
        edit->setText("bug");
        QCOMPARE(grep->text(), QString("bug"));
        QCOMPARE(changed.count(), 7);
        QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(changed.count(), 8);
        grep->setText("feature");
        QCOMPARE(edit->text(), QString("feature"));
        QAction *wrapping = nullptr;
        const QList<QAction *> inBar = widget->toolBar()->actions();
        for (QAction * const action : inBar) {
            if (widget->toolBar()->widgetForAction(action) == edit)
                wrapping = action;
        }
        QVERIFY2(wrapping, "the line edit is not in the tool bar as an action");
        QVERIFY(wrapping->isVisible());
        grep->setVisible(false);
        QVERIFY2(!wrapping->isVisible(), "hiding the field left its line edit in the tool bar");
    }

    // What a VCS client is handed for the editor it is about to fill was the
    // widget; it is the document now, so that the same client code fills a
    // view of any kind. Opened temporary, with its source and its default
    // line, its annotate requests reaching the client; the same tag is the
    // same document, filled afresh; and a command fills it. (Read-only it is
    // from the factory, whoever opens it - not this code's doing.)
    void testAClientsHandleIsTheDocument()
    {
        class HandleClient final : public VcsBaseClientImpl
        {
        public:
            explicit HandleClient(VcsBaseSettings *settings)
                : VcsBaseClientImpl(settings)
            {}
            void annotate(const FilePath &workingDir, const QString &file, int lineNumber,
                          const QString &revision, const QStringList &, int) final
            {
                m_annotated << QStringList{workingDir.toUrlishString(), file,
                                           QString::number(lineNumber), revision};
            }
            QList<QStringList> m_annotated;
        };
        class HandleWidget final : public VcsBaseEditorWidget
        {
        public:
            HandleWidget() = default;

        private:
            QString changeUnderCursor(const QTextCursor &) const final { return {}; }
        };
        const VcsBaseEditorParameters parameters{AnnotateOutput,
                                                 "VcsEditorDocumentTest.Handle",
                                                 "VCS document test handle",
                                                 "text/vnd.qtcreator.vcs-document-handle-test",
                                                 [] { return new HandleWidget; },
                                                 [](const FilePath &, const QString &) {}};
        VcsEditorFactory factory(parameters);
        VcsBaseSettings settings;
        HandleClient client(&settings);
        const FilePath source = FilePath::fromString("/repo/sub/file.cpp");

        VcsEditorDocument * const document = client.createVcsDocument(
            parameters.id, "Handle test", source, TextEncoding(), "HandleTestTag", "one");
        QVERIFY2(document, "the client got no document");
        const QScopeGuard closeIt(
            [document] { Core::EditorManager::closeDocuments({document}, false); });
        Core::IEditor * const editor = Core::DocumentModel::editorsForDocument(document).value(0);
        QVERIFY2(editor, "the document is in no editor");
        QVERIFY2(document->isTemporary(), "version control output is offered for saving");
        VcsBaseEditorWidget * const widget = VcsBaseEditor::getVcsBaseEditor(editor);
        QVERIFY(widget);
        QVERIFY2(widget->isReadOnly(), "a VCS editor from the factory can be typed into");
        QCOMPARE(VcsBase::source(document), source);
        QCOMPARE(document->defaultLineNumber(), 1);
        QCOMPARE(document->plainText(), Tr::tr("Working..."));

        // The same tag is the same document, told to wait again.
        document->setPlainText("old output");
        QCOMPARE(client.createVcsDocument(parameters.id, "Handle test", source, TextEncoding(),
                                          "HandleTestTag", "one"),
                 document);
        QCOMPARE(document->plainText(), Tr::tr("Working..."));
        QCOMPARE(Core::DocumentModel::editorsForDocument(document).size(), 1);

        // An annotation asked for in the document reaches the client's
        // annotate(), the change trimmed to its first word as ever.
        emit document->annotateRevisionRequested(FilePath::fromString("/repo"), "sub/file.cpp",
                                                 "1234abcd someone a subject", 7);
        QCOMPARE(client.m_annotated,
                 (QList<QStringList>{{"/repo", "sub/file.cpp", "7", "1234abcd"}}));

        // And a command's output becomes its text.
        const FilePath sh = FilePath::fromString("sh").searchInPath();
        if (sh.isEmpty())
            QSKIP("no sh to run a command with");
        client.executeInEditor(FilePath::fromString("/"),
                               CommandLine{sh, {"-c", "printf 'filled\\n'"}}, document);
        QVERIFY2(document->isBusy(), "the command was handed to nobody");
        QTRY_VERIFY(!document->isBusy());
        QCOMPARE(document->plainText(), QString("filled\n"));
    }

    // The generic client - what Bazaar, Fossil, Mercurial and Subversion
    // build on - holds the document too: its diff and log configs are made
    // with the document as parent and set on it, a chunk reverted in the
    // document re-runs the diff through the config, and the command runs on
    // the document. There is no VCS binary here, so every command fails at
    // once with the document saying so; what is asserted is where things
    // went, not what came back.
    void testTheGenericClientHoldsTheDocument()
    {
        class GenericClient final : public VcsBaseClient
        {
        public:
            GenericClient(VcsBaseSettings *settings, Id kind)
                : VcsBaseClient(settings)
                , m_kind(kind)
            {
                setDiffConfigCreator([this](QObject *parent) {
                    m_diffParents << parent;
                    return new VcsBaseEditorConfig(parent);
                });
                setLogConfigCreator([this](QObject *parent) {
                    m_logParents << parent;
                    return new VcsBaseEditorConfig(parent);
                });
            }
            Id vcsEditorKind(VcsCommandTag) const final { return m_kind; }
            const Id m_kind;
            QList<QObject *> m_diffParents;
            QList<QObject *> m_logParents;
        };
        class GenericWidget final : public VcsBaseEditorWidget
        {
        public:
            GenericWidget() = default;

        private:
            QString changeUnderCursor(const QTextCursor &) const final { return {}; }
        };
        const VcsBaseEditorParameters parameters{DiffOutput,
                                                 "VcsEditorDocumentTest.Generic",
                                                 "VCS document test generic client",
                                                 "text/vnd.qtcreator.vcs-document-generic-test",
                                                 [] { return new GenericWidget; },
                                                 [](const FilePath &, const QString &) {}};
        VcsEditorFactory factory(parameters);
        VcsBaseSettings settings;
        GenericClient client(&settings, parameters.id);
        TemporaryDirectory dir("vcs-generic-client");
        QVERIFY(dir.isValid());
        const QString failed = Tr::tr("Failed to retrieve data.");

        client.diff(dir.path(), {"a.txt"});
        QCOMPARE(client.m_diffParents.size(), 1);
        auto * const diffDocument = qobject_cast<VcsEditorDocument *>(client.m_diffParents.first());
        QVERIFY2(diffDocument, "the diff config was not made with the document as parent");
        const QScopeGuard closeDiff(
            [diffDocument] { Core::EditorManager::closeDocuments({diffDocument}, false); });
        QVERIFY2(diffDocument->editorConfig(), "the diff config was not set on the document");
        QCOMPARE(diffDocument->editorConfig()->parent(), diffDocument);
        QCOMPARE(diffDocument->workingDirectory(), dir.path());
        QTRY_COMPARE(diffDocument->plainText(), failed);

        // A chunk reverted in the document re-runs the diff through the
        // config: the same document, the config it has, the command again.
        diffDocument->setPlainText("reverted");
        emit diffDocument->diffChunkReverted();
        QCOMPARE(client.m_diffParents.size(), 1);
        QTRY_COMPARE(diffDocument->plainText(), failed);

        client.log(dir.path(), {"a.txt"});
        QCOMPARE(client.m_logParents.size(), 1);
        auto * const logDocument = qobject_cast<VcsEditorDocument *>(client.m_logParents.first());
        QVERIFY2(logDocument, "the log config was not made with the document as parent");
        const QScopeGuard closeLog(
            [logDocument] { Core::EditorManager::closeDocuments({logDocument}, false); });
        QVERIFY(logDocument != diffDocument);
        QVERIFY2(logDocument->editorConfig(), "the log config was not set on the document");
        QTRY_COMPARE(logDocument->plainText(), failed);
    }

    // Everything the widget subclasses declared in their constructors - the
    // patterns, the annotate texts, a log entry's subject, a highlighter of
    // the VCS's own, what a command's output becomes - is the parameters',
    // for a VCS with no widget subclass left. And a factory given no widget
    // creator builds the Qt Quick editor, read-only and folding a log.
    void testTheParametersDeclareWhatTheWidgetConstructorsDid()
    {
        TextEditor::SyntaxHighlighter *made = nullptr;
        QStringList put;
        VcsBaseEditorParameters parameters{LogOutput,
                                           "VcsEditorDocumentTest.Declared",
                                           "VCS document test declared",
                                           "text/vnd.qtcreator.vcs-document-declared-test",
                                           {},
                                           [](const FilePath &, const QString &) {}};
        parameters.logEntryPattern = "^entry ([0-9]+)";
        parameters.annotationEntryPattern = "^([0-9a-f]{8}) ";
        parameters.annotationSeparatorPattern = "^(---)$";
        parameters.annotateRevisionTextFormat = "Blame %1";
        parameters.annotatePreviousRevisionTextFormat = "Blame parent %1";
        parameters.revisionSubject = [](const QTextBlock &block) {
            return block.next().text().trimmed();
        };
        parameters.syntaxHighlighterCreator = [&made] {
            made = new TextEditor::SyntaxHighlighter;
            return made;
        };
        parameters.putOutput = [&put](VcsEditorDocument *document, const QString &output) {
            put << output;
            document->setPlainText(output + "-- put --\n");
        };

        VcsEditorDocument document(parameters);
        QCOMPARE(document.logEntryPattern().pattern(), QString("^entry ([0-9]+)"));
        QCOMPARE(document.annotation().entryPattern.pattern(), QString("^([0-9a-f]{8}) "));
        QCOMPARE(document.annotation().separatorPattern.pattern(), QString("^(---)$"));
        QCOMPARE(document.annotateRevisionTextFormat(), QString("Blame %1"));
        QCOMPARE(document.annotatePreviousRevisionTextFormat(), QString("Blame parent %1"));
        QVERIFY2(made, "the VCS's own highlighter was not made");
        QCOMPARE(document.syntaxHighlighter(), made);
        const QString output = "entry 1\nfirst subject\nentry 2\nsecond subject\n";
        document.setOutput(output);
        QCOMPARE(put, QStringList{output});
        QCOMPARE(document.plainText(), output + "-- put --\n");
        QCOMPARE(document.sections()->entries().size(), 2);
        QVERIFY2(document.sections()->entries().first().contains("first subject"),
                 qPrintable(document.sections()->entries().join(", ")));

        // No widget creator: the Qt Quick editor, with what the widget's
        // factory step and its init() used to set.
        VcsEditorFactory factory(parameters);
        QVERIFY2(factory.usesQuickEditor(), "a VCS with no widget still gets the widget editor");
        QVERIFY2(factory.readOnly(), "version control output is offered for typing into");
        QVERIFY2(factory.codeFoldingSupported(), "a log's entries cannot be folded");
        const std::unique_ptr<Core::IEditor> editor(factory.createEditor());
        QVERIFY2(editor.get(), "the factory built nothing");
        QVERIFY2(!TextEditor::TextEditorWidget::fromEditor(editor.get()),
                 "the factory built the widget editor with no widget to build it from");
        const QList<QWidget *> children = editor->widget()->findChildren<QWidget *>();
        QVERIFY2(Utils::anyOf(children, [](QWidget *w) { return w->inherits("QQuickWidget"); }),
                 "the factory built no Qt Quick view either");
        QVERIFY2(qobject_cast<VcsEditorDocument *>(editor->document()),
                 "the Qt Quick editor's document is not a VCS document");
    }

    void testALogOffersWhatIsUnderThePointerToDoSomethingWith()
    {
        FilePath describedSource;
        QString describedChange;
        const VcsBaseEditorParameters parameters{
            LogOutput,
            "VcsEditorDocumentTest.Links",
            "VCS document test links",
            "text/vnd.qtcreator.vcs-document-links-test",
            {},
            [&describedSource, &describedChange](const FilePath &source, const QString &change) {
                describedSource = source;
                describedChange = change;
            },
            [](const QTextCursor &cursor) {
                static const QRegularExpression eightHex(
                    QRegularExpression::anchoredPattern("[0-9a-f]{8}"));
                QTextCursor word = cursor;
                word.select(QTextCursor::WordUnderCursor);
                const QString text = word.selectedText();
                return eightHex.match(text).hasMatch() ? text : QString();
            }};
        VcsEditorDocument document(parameters);
        VcsBase::setSource(&document, FilePath::fromString("/repo/file.cpp"));
        const QString text = "commit 1234abcd\n"
                             "see https://example.org/x?a=1 now\n"
                             "Task-number: QTCREATORBUG-123\n"
                             "mail me@example.com\n";
        document.setPlainText(text);

        const TextEditor::ActionLinkFinder finder
            = TextEditor::TextEditorFactory::actionLinkFinderFor(&document);
        QVERIFY2(finder, "a log offers nothing to do under the pointer");
        const auto at = [&document, &finder](int position) {
            QTextCursor cursor(document.document());
            cursor.setPosition(position);
            return finder(&document, cursor);
        };
        const auto spanOf = [&text](const QString &what) {
            const int start = int(text.indexOf(what));
            return std::make_pair(start, start + int(what.size()));
        };

        const auto change = spanOf("1234abcd");
        const TextEditor::ActionLink onChange = at(change.first + 3);
        QVERIFY2(onChange.isValid(), "the change is not offered");
        QCOMPARE(onChange.linkTextStart, change.first);
        QCOMPARE(onChange.linkTextEnd, change.second);
        onChange.activate();
        QCOMPARE(describedChange, QString("1234abcd"));
        QCOMPARE(describedSource, FilePath::fromString("/repo/file.cpp"));

        // Not activated: that opens a browser.
        for (const QString &what : {QString("https://example.org/x?a=1"),
                                    QString("QTCREATORBUG-123"),
                                    QString("me@example.com")}) {
            const auto span = spanOf(what);
            const TextEditor::ActionLink on = at(span.first + 2);
            QVERIFY2(on.isValid(), qPrintable(what + " is not offered"));
            QCOMPARE(on.linkTextStart, span.first);
            QCOMPARE(on.linkTextEnd, span.second);
        }
        QVERIFY2(!at(spanOf("commit").first + 1).isValid(), "a plain word is offered");
        QVERIFY2(!at(spanOf("now").first).isValid(), "the word after the URL is offered");

        VcsBaseEditorParameters diffParameters = parameters;
        diffParameters.type = DiffOutput;
        diffParameters.id = "VcsEditorDocumentTest.DiffLinks";
        VcsEditorDocument diff(diffParameters);
        QVERIFY2(!TextEditor::TextEditorFactory::actionLinkFinderFor(&diff),
                 "a diff offers something to do under the pointer");
    }

    // A right click in a log offers what the widget editor's handlers put in
    // its menu: for a change, to copy it, describe it and - where the VCS says
    // its log can - annotate the file at it; for a URL or an address, to open
    // and to copy. Each entry is tried, not only counted.
    void testALogsRightClickOffersWhatTheWidgetsHandlersDid()
    {
        FilePath describedSource;
        QString describedChange;
        const VcsBaseEditorParameters parameters{
            LogOutput,
            "VcsEditorDocumentTest.Menu",
            "VCS document test menu",
            "text/vnd.qtcreator.vcs-document-menu-test",
            {},
            [&describedSource, &describedChange](const FilePath &source, const QString &change) {
                describedSource = source;
                describedChange = change;
            },
            [](const QTextCursor &cursor) {
                static const QRegularExpression eightHex(
                    QRegularExpression::anchoredPattern("[0-9a-f]{8}"));
                QTextCursor word = cursor;
                word.select(QTextCursor::WordUnderCursor);
                const QString text = word.selectedText();
                return eightHex.match(text).hasMatch() ? text : QString();
            }};
        VcsEditorDocument document(parameters);
        VcsBase::setSource(&document, FilePath::fromString("/repo/sub/file.cpp"));
        document.setWorkingDirectory(FilePath::fromString("/repo"));
        const QString text = "commit 1234abcd\n"
                             "see https://example.org/x?a=1 now\n"
                             "Task-number: QTCREATORBUG-123\n"
                             "mail me@example.com\n";
        document.setPlainText(text);

        const auto cursorAt = [&document, &text](const QString &what) {
            QTextCursor cursor(document.document());
            cursor.setPosition(int(text.indexOf(what)) + 1);
            return cursor;
        };
        const auto textsOf = [](const QList<QAction *> &actions) {
            return Utils::transform(actions, &QAction::text);
        };
        const auto trigger = [](const QList<QAction *> &actions, const QString &text) {
            for (QAction * const action : actions) {
                if (action->text() == text) {
                    action->trigger();
                    return true;
                }
            }
            return false;
        };
        QClipboard * const clipboard = QGuiApplication::clipboard();

        // The change: copy, describe - and no annotation from a log that has
        // not said it can.
        QList<QAction *> onChange = document.contextMenuActions(cursorAt("1234abcd"));
        QStringList texts = textsOf(onChange);
        QCOMPARE(texts.mid(0, 2), QStringList({"Copy \"1234abcd\"", "&Describe Change 1234abcd"}));
        QVERIFY2(!texts.contains("Annotate \"1234abcd\""), "a log offered to annotate before it said it could");
        QVERIFY(trigger(onChange, "&Describe Change 1234abcd"));
        QCOMPARE(describedChange, QString("1234abcd"));
        QCOMPARE(describedSource, FilePath::fromString("/repo/sub/file.cpp"));
        clipboard->clear();
        QVERIFY(trigger(onChange, "Copy \"1234abcd\""));
        QCOMPARE(clipboard->text(), QString("1234abcd"));

        // Once it has: the annotation, asked for with the file relative to the
        // working directory and the line the click was on.
        document.setFileLogAnnotateEnabled(true);
        QSignalSpy annotated(&document, &VcsEditorDocument::annotateRevisionRequested);
        onChange = document.contextMenuActions(cursorAt("1234abcd"));
        QVERIFY(trigger(onChange, "Annotate \"1234abcd\""));
        QCOMPARE(annotated.count(), 1);
        QCOMPARE(annotated.first().at(0).value<FilePath>(), FilePath::fromString("/repo"));
        QCOMPARE(annotated.first().at(1).toString(), QString("sub/file.cpp"));
        QCOMPARE(annotated.first().at(2).toString(), QString("1234abcd"));
        QCOMPARE(annotated.first().at(3).toInt(), 1);

        // A URL and a Jira key: open and copy, the key copied as the URL it
        // stands for. An address: send and copy.
        QList<QAction *> onUrl = document.contextMenuActions(cursorAt("https://example.org"));
        QCOMPARE(textsOf(onUrl).mid(0, 2), QStringList({"Open URL in Browser...", "Copy URL Location"}));
        QVERIFY(trigger(onUrl, "Copy URL Location"));
        QCOMPARE(clipboard->text(), QString("https://example.org/x?a=1"));
        QList<QAction *> onKey = document.contextMenuActions(cursorAt("QTCREATORBUG-123"));
        QVERIFY(trigger(onKey, "Copy URL Location"));
        QCOMPARE(clipboard->text(), QString(Core::Constants::QT_JIRA_URL) + "/browse/QTCREATORBUG-123");
        QList<QAction *> onMail = document.contextMenuActions(cursorAt("me@example.com"));
        QCOMPARE(textsOf(onMail).mid(0, 2), QStringList({"Send Email To...", "Copy Email Address"}));
        QVERIFY(trigger(onMail, "Copy Email Address"));
        QCOMPARE(clipboard->text(), QString("me@example.com"));

        // Plain text: none of those.
        const QStringList onPlain = textsOf(document.contextMenuActions(cursorAt("commit")));
        for (const QString &entry : onPlain)
            QVERIFY2(!entry.startsWith("Copy") && !entry.startsWith("&Describe") && !entry.startsWith("Open"),
                     qPrintable("plain text offered: " + entry));

        // An annotation offers to annotate its revision without being told.
        VcsBaseEditorParameters annotateParameters = parameters;
        annotateParameters.type = AnnotateOutput;
        annotateParameters.id = "VcsEditorDocumentTest.AnnotateMenu";
        VcsEditorDocument annotation(annotateParameters);
        annotation.setPlainText("1234abcd (someone) line\n");
        QTextCursor onRevision(annotation.document());
        onRevision.setPosition(2);
        QVERIFY2(textsOf(annotation.contextMenuActions(onRevision)).contains("Annotate \"1234abcd\""),
                 "an annotation does not offer to annotate the revision under the pointer");
    }

    // What a VCS answers through its parameters shapes the menu the way its
    // widget virtuals did: which revisions are valid, how a revision is shown,
    // which revisions came before, what entries of its own it adds, and which
    // file a line is of. Each is tried; the base answers where a parameter is
    // unset are the other tests' business.
    void testTheVcssOwnAnswersShapeTheMenu()
    {
        VcsBaseEditorParameters parameters{AnnotateOutput,
                                           "VcsEditorDocumentTest.Answers",
                                           "VCS document test answers",
                                           "text/vnd.qtcreator.vcs-document-answers-test",
                                           {},
                                           [](const FilePath &, const QString &) {},
                                           [](const QTextCursor &cursor) {
                                               static const QRegularExpression eightHex(
                                                   QRegularExpression::anchoredPattern("[0-9a-f]{8}"));
                                               QTextCursor word = cursor;
                                               word.select(QTextCursor::WordUnderCursor);
                                               const QString text = word.selectedText();
                                               return eightHex.match(text).hasMatch() ? text : QString();
                                           }};
        parameters.isValidRevision = [](const QString &revision) { return revision != "deadbeef"; };
        parameters.decorateVersion = [](VcsEditorDocument *, const QString &revision) {
            return revision + "!";
        };
        parameters.annotationPreviousVersions = [](VcsEditorDocument *, const QString &) {
            return QStringList{"0000abcd"};
        };
        parameters.addChangeActions = [](QMenu *menu, VcsEditorDocument *, const QString &change, int line) {
            menu->addAction(QString("Extra for %1 at %2").arg(change).arg(line));
        };
        parameters.fileNameForLine = [](VcsEditorDocument *, int) {
            return FilePath::fromString("/repo/renamed.cpp");
        };
        VcsEditorDocument document(parameters);
        VcsBase::setSource(&document, FilePath::fromString("/repo/sub/file.cpp"));
        document.setWorkingDirectory(FilePath::fromString("/repo"));
        document.setAnnotatePreviousRevisionTextFormat("Previous \"%1\"");
        const QString text = "1234abcd (someone) line one\n"
                             "deadbeef (nobody) line two\n";
        document.setPlainText(text);
        const auto cursorAt = [&document, &text](const QString &what) {
            QTextCursor cursor(document.document());
            cursor.setPosition(int(text.indexOf(what)) + 1);
            return cursor;
        };
        const auto textsOf = [](const QList<QAction *> &actions) {
            return Utils::transform(actions, &QAction::text);
        };

        // A valid revision: copy, describe, itself decorated, the one before
        // it in the previous format and decorated, then the VCS's own.
        const QList<QAction *> onValid = document.contextMenuActions(cursorAt("1234abcd"));
        QCOMPARE(textsOf(onValid), QStringList({"Copy \"1234abcd\"", "&Describe Change 1234abcd",
                                                "Annotate \"1234abcd!\"", "Previous \"0000abcd!\"",
                                                "Extra for 1234abcd at 1"}));
        // Annotating the one before goes to the file the VCS names for the
        // line, relative to the working directory, from that line.
        QSignalSpy annotated(&document, &VcsEditorDocument::annotateRevisionRequested);
        for (QAction * const action : onValid) {
            if (action->text() == "Previous \"0000abcd!\"")
                action->trigger();
        }
        QCOMPARE(annotated.count(), 1);
        QCOMPARE(annotated.first().at(1).toString(), QString("renamed.cpp"));
        QCOMPARE(annotated.first().at(2).toString(), QString("0000abcd"));
        QCOMPARE(annotated.first().at(3).toInt(), 1);

        // One the VCS calls invalid: copy, the ones before it, the VCS's own -
        // and neither describe nor annotate itself.
        const QStringList onInvalid = textsOf(document.contextMenuActions(cursorAt("deadbeef")));
        QCOMPARE(onInvalid, QStringList({"Copy \"deadbeef\"", "Previous \"0000abcd!\"",
                                         "Extra for deadbeef at 2"}));

        // An output of another type has change links only if the VCS says so.
        VcsBaseEditorParameters other = parameters;
        other.type = OtherContent;
        other.id = "VcsEditorDocumentTest.OtherLinks";
        VcsEditorDocument plain(other);
        QVERIFY2(!TextEditor::TextEditorFactory::actionLinkFinderFor(&plain),
                 "an output of another type offers change links unasked");
        other.changeLinksInOtherContent = true;
        other.id = "VcsEditorDocumentTest.OtherLinksAsked";
        VcsEditorDocument commitLike(other);
        QVERIFY2(TextEditor::TextEditorFactory::actionLinkFinderFor(&commitLike),
                 "an output the VCS said has change links offers none");
    }

    // A diff's chunk, and the place in the file a line of it stands for, are
    // the document's to work out: from its patterns, its working directory
    // and its source, with the highlighter it installs itself saying where
    // the headers end. The widget editor asks it; a view that is not the
    // widget can too.
    void testADiffsChunkAndItsTargetAreTheDocuments()
    {
        TemporaryDirectory dir("vcs-document-diff");
        QVERIFY(dir.isValid());
        const FilePath file = dir.filePath("a.txt");
        QVERIFY(file.writeFileContents("one\ntwo\nthree\n"));

        VcsBaseEditorParameters parameters{DiffOutput,
                                           "VcsEditorDocumentTest.Diff",
                                           "VCS document test diff",
                                           "text/vnd.qtcreator.vcs-document-diff-test",
                                           {},
                                           [](const FilePath &, const QString &) {},
                                           {}};
        parameters.addDiffActions = [](QMenu *menu, VcsEditorDocument *, const DiffChunk &chunk) {
            menu->addAction("Extra chunk " + chunk.fileName.fileName());
        };
        VcsEditorDocument document(parameters);
        document.setWorkingDirectory(file.parentDir());
        document.setDiffFilePattern("^(?:diff --git a/|index |[+-]{3} (?:/dev/null|[ab]/(.+$)))");
        document.setLogEntryPattern("^commit ([0-9a-f]{8})[0-9a-f]{32}");
        const QString text = "diff --git a/a.txt b/a.txt\n"
                             "index 1..2 100644\n"
                             "--- a/a.txt\n"
                             "+++ b/a.txt\n"
                             "@@ -1,3 +1,3 @@\n"
                             " one\n"
                             "-two\n"
                             "+TWO\n"
                             " three\n";
        document.setPlainText(text);
        const auto cursorAt = [&document, &text](const QString &what) {
            QTextCursor cursor(document.document());
            cursor.setPosition(int(text.indexOf(what)) + 1);
            return cursor;
        };

        // The headers are told from the chunk by the highlighter the document
        // installed; the chunk waits for it to have run.
        QTRY_VERIFY2(document.diffChunk(cursorAt("+TWO")).isValid(), "no chunk around a changed line");
        const DiffChunk chunk = document.diffChunk(cursorAt("+TWO"));
        QCOMPARE(chunk.fileName, file);
        QVERIFY2(chunk.chunk.startsWith("@@ -1,3 +1,3 @@"), chunk.chunk.constData());
        QVERIFY(chunk.chunk.contains("-two\n+TWO\n"));
        QVERIFY(chunk.header.contains("--- a/a.txt"));
        QVERIFY2(!document.diffChunk(cursorAt("--- a/a.txt")).isValid(), "a header line is inside a chunk");
        QVERIFY2(document.canApplyDiffChunk(chunk), "the chunk of a file that is there cannot be applied");

        const QStringList onChunk
            = Utils::transform(document.contextMenuActions(cursorAt("+TWO")), &QAction::text);
        QVERIFY2(onChunk.contains("Apply Chunk...") && onChunk.contains("Revert Chunk..."),
                 qPrintable(onChunk.join(", ")));
        const QStringList onHeader
            = Utils::transform(document.contextMenuActions(cursorAt("--- a/a.txt")), &QAction::text);
        QVERIFY2(!onHeader.contains("Apply Chunk..."), "a header offers to apply a chunk");
        // The VCS's own entries for the chunk, after Apply and Revert.
        QVERIFY2(onChunk.indexOf("Extra chunk a.txt") > onChunk.indexOf("Revert Chunk..."),
                 qPrintable(onChunk.join(", ")));
        QVERIFY2(!onHeader.contains("Extra chunk a.txt"), "a header gets the VCS's chunk entries");

        const DiffTarget onChanged = document.diffTargetAt(cursorAt("+TWO"));
        QVERIFY2(onChanged.isValid(), "a changed line stands for no place in the file");
        QCOMPARE(onChanged.filePath, file);
        QCOMPARE(onChanged.line, 2);
        QVERIFY2(!document.diffTargetAt(cursorAt("--- a/a.txt")).isValid(),
                 "a header line stands for a place in the file");

        // And that place is what following the line goes to, in either view:
        // the document's link finder answers the file and line, with the whole
        // line as the link's text; a header line is no link.
        const TextEditor::LinkFinder finder = TextEditor::TextEditorFactory::linkFinderFor(&document);
        QVERIFY2(finder, "a diff's lines are not links");
        Utils::Link followed;
        finder(&document, cursorAt("+TWO"), [&followed](const Utils::Link &link) { followed = link; },
               true, false);
        QCOMPARE(followed.targetFilePath, file);
        QCOMPARE(followed.target.line, 2);
        const QTextBlock changed = cursorAt("+TWO").block();
        QCOMPARE(followed.linkTextStart, changed.position());
        QCOMPARE(followed.linkTextEnd, changed.position() + changed.length() - 1);
        Utils::Link fromHeader = Utils::Link(file, 99);
        finder(&document, cursorAt("--- a/a.txt"),
               [&fromHeader](const Utils::Link &link) { fromHeader = link; }, true, false);
        QVERIFY2(!fromHeader.hasValidTarget(), "a header line is followed somewhere");

        // Where the VCS knows better, the parameters' resolver answers - given
        // the target and the link as found, and free to move it.
        VcsBaseEditorParameters resolving = parameters;
        resolving.id = "VcsEditorDocumentTest.ResolvedDiff";
        int askedForLine = 0;
        resolving.resolveDiffTarget = [&askedForLine](VcsEditorDocument *, const DiffTarget &target,
                                                      const Utils::Link &link,
                                                      const Utils::LinkHandler &callback) {
            askedForLine = target.line;
            Utils::Link moved = link;
            moved.target.line = link.target.line + 10;
            callback(moved);
        };
        VcsEditorDocument resolved(resolving);
        resolved.setWorkingDirectory(file.parentDir());
        resolved.setDiffFilePattern("^(?:diff --git a/|index |[+-]{3} (?:/dev/null|[ab]/(.+$)))");
        resolved.setLogEntryPattern("^commit ([0-9a-f]{8})[0-9a-f]{32}");
        resolved.setPlainText(text);
        QTRY_VERIFY(resolved.diffTargetAt(cursorAt("+TWO")).isValid());
        QTextCursor onChangedLine(resolved.document());
        onChangedLine.setPosition(int(text.indexOf("+TWO")) + 1);
        Utils::Link viaVcs;
        TextEditor::TextEditorFactory::linkFinderFor(&resolved)(
            &resolved, onChangedLine, [&viaVcs](const Utils::Link &link) { viaVcs = link; }, true, false);
        QCOMPARE(askedForLine, 2);
        QCOMPARE(viaVcs.target.line, 12);
        QCOMPARE(viaVcs.targetFilePath, file);
    }
};

QObject *createVcsEditorDocumentTest()
{
    return new VcsEditorDocumentTest;
}

} // namespace VcsBase

#endif // WITH_TESTS

#include "vcseditordocument.moc"
