// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "giteditor.h"

#include "annotationhighlighter.h"
#include "gitclient.h"
#include "gitconstants.h"
#include "githighlighters.h"
#include "gitsettings.h"
#include "gittr.h"

#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/icore.h>
#include <coreplugin/vcsmanager.h>

#include <texteditor/syntaxhighlighter.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>

#include <vcsbase/commonvcssettings.h>
#include <vcsbase/vcsbaseplugin.h>
#include <vcsbase/vcsbaseeditorconfig.h>
#include <vcsbase/vcsoutputwindow.h>

#include <utils/algorithm.h>
#include <utils/ansiescapecodehandler.h>
#include <utils/commandline.h>
#include <utils/qtcassert.h>
#include <utils/temporaryfile.h>

#include <QKeyEvent>
#include <QMenu>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QTextBlock>
#include <QTextCursor>

#include <algorithm>

#define CHANGE_PATTERN "\\b[a-f0-9]{7,40}\\b"

using namespace Core;
using namespace Utils;
using namespace VcsBase;

namespace Git::Internal {

QStringList gitLogFilterArguments(const QString &author, const QString &grep,
                                  const QString &pickaxe, bool caseSensitive)
{
    QStringList arguments;
    if (!author.isEmpty())
        arguments << "--author=" + ProcessArgs::quoteArg(author);
    if (!grep.isEmpty())
        arguments << "--grep=" + ProcessArgs::quoteArg(grep);
    if (!pickaxe.isEmpty())
        arguments << "-S" << ProcessArgs::quoteArg(pickaxe);
    if (!caseSensitive)
        arguments << "-i";
    return arguments;
}

QString gitChangeUnderCursor(const QTextCursor &c)
{
    static const QRegularExpression changeNumberPattern(
        QRegularExpression::anchoredPattern(CHANGE_PATTERN));
    QTextCursor cursor = c;
    // Any number is regarded as change number.
    cursor.select(QTextCursor::WordUnderCursor);
    if (!cursor.hasSelection())
        return {};
    const QString change = cursor.selectedText();
    if (changeNumberPattern.match(change).hasMatch())
        return change;
    return {};
}

/**
 * Optionally remove path, author or date specification from annotation, which is tabular:
 * \code
 * 8ca887aa filepath <orig line> (author YYYY-MM-DD HH:MM:SS <offset> <line>) <content>
 * \endcode
 */
static QString sanitizeBlameOutput(const QString &b)
{
    static const char pattern[] =
        R"(^(\S+)\s(.+?)(\s+\d+)\s\((.*)\s+(\d{4}-\d{2}-\d{2}\s\d{2}:\d{2}:\d{2}\s[+-]\d{4}).*?\)(.*)$)";
    static const QRegularExpression re(pattern, QRegularExpression::MultilineOption);

    if (b.isEmpty())
        return b;

    const bool omitPath = settings().omitAnnotationPath();
    const bool omitAuthor = settings().omitAnnotationAuthor();
    const bool omitDate = settings().omitAnnotationDate();

    QString result;
    QRegularExpressionMatchIterator i = re.globalMatch(b);
    while (i.hasNext()) {
        static const QString sep = "  ";
        const QRegularExpressionMatch match = i.next();
        const QString hash   = match.captured(1) + sep;
        const QString line   = match.captured(3);
        const QString path   = omitPath   ? QString() : match.captured(2) + line;
        const QString author = omitAuthor ? QString() : match.captured(4) + sep;
        const QString date   = omitDate   ? QString() : match.captured(5);
        const QString code   = match.captured(6);
        result.append(hash + path + "  (" + author + date + ")  " + code + "\n");
    }
    return result;
}

void gitPutOutput(VcsBase::VcsEditorDocument *document, const QString &output)
{
    switch (document->contentType()) {
    case LogOutput:
        // git colours the log itself, so what arrives has escape codes in it.
        AnsiEscapeCodeHandler::setTextInDocument(document->document(), output);
        return;
    case AnnotateOutput:
        document->setPlainText(sanitizeBlameOutput(output));
        return;
    case DiffOutput:
    case OtherContent:
        document->setPlainText(output);
        return;
    }
}

QString gitRevisionSubject(const QTextBlock &inBlock)
{
    for (QTextBlock block = inBlock.next(); block.isValid(); block = block.next()) {
        const QString line = block.text().trimmed();
        if (line.isEmpty()) {
            block = block.next();
            return block.text().trimmed();
        }
    }
    return {};
}

void gitApplyDiffChunk(VcsBase::VcsEditorDocument *document, const DiffChunk &chunk,
                       PatchAction patchAction)
{
    TemporaryFile patchFile("git-apply-chunk");
    if (!patchFile.open())
        return;

    const FilePath baseDir = document->workingDirectory();
    patchFile.write(chunk.header);
    patchFile.write(chunk.chunk);
    patchFile.close();

    QStringList args = {"--cached"};
    if (patchAction == PatchAction::Revert)
        args << "--reverse";
    QString errorMessage;
    if (gitClient().synchronousApplyPatch(baseDir, patchFile.filePath().path(), &errorMessage, args)) {
        if (errorMessage.isEmpty())
            VcsOutputWindow::appendSilently(baseDir, Tr::tr("Chunk successfully staged"));
        else
            VcsOutputWindow::appendError(baseDir, errorMessage);
        if (patchAction == PatchAction::Revert)
            emit document->diffChunkReverted();
    } else {
        VcsOutputWindow::appendError(baseDir, errorMessage);
    }
}

// What Git wants known about the commit message or the rebase script as it
// opens, before the text is read: the repository - the file's directory is
// the git directory - and so the commit encoding the text is in, and the
// highlighter, which knows the repository's comment character.
static void gitAboutToOpen(VcsBase::VcsEditorDocument *document, const FilePath &filePath)
{
    const FilePath gitPath = filePath.absolutePath();
    VcsBase::setSource(document, gitPath);
    document->setEncoding(gitClient().encoding(GitClient::EncodingCommit, gitPath));
    const QString commentMarker = gitClient().commentMarker(gitPath);
    if (document->id() == Id(Git::Constants::GIT_COMMIT_TEXT_EDITOR_ID)) {
        document->resetSyntaxHighlighter([commentMarker] {
            auto highlighter = new GitSubmitHighlighter(commentMarker);
            highlighter->setSpellCheckLanguage(VcsBase::Internal::submitMessageSpellCheckLanguage());
            return highlighter;
        });
        QObject::connect(&VcsBase::Internal::commonSettings(), &AspectContainer::applied,
                         document, [document] {
            if (TextEditor::SyntaxHighlighter *highlighter = document->syntaxHighlighter()) {
                highlighter->setSpellCheckLanguage(
                    VcsBase::Internal::submitMessageSpellCheckLanguage());
            }
        });
    } else {
        document->resetSyntaxHighlighter(
            [commentMarker] { return new GitRebaseHighlighter(commentMarker); });
    }
}

/*!
    If the cursor is at the start of a rebase todo line, and the pressed key
    is the shortcut of a rebase action, this replaces the action keyword or
    shortcut that is already there with the one matching the pressed key.
*/
static bool gitReplaceRebaseAction(Core::IEditor *editor, QKeyEvent *e)
{
    const QTextCursor cursor = TextEditor::textCursorOf(editor);
    if (cursor.isNull() || cursor.hasSelection() || !cursor.atBlockStart()
        || e->text().size() != 1) {
        return false;
    }

    const QChar key = e->text().at(0);
    const QList<GitRebaseHighlighter::RebaseAction> &actions = GitRebaseHighlighter::actions();
    const auto pressedAction = std::find_if(
        actions.begin(), actions.end(),
        [key](const GitRebaseHighlighter::RebaseAction &action) {
            return action.shortcut == key;
        });
    if (pressedAction == actions.end())
        return false;

    static const QRegularExpression firstTokenPattern("^\\S+");
    const QRegularExpressionMatch firstTokenMatch = firstTokenPattern.match(cursor.block().text());
    if (!firstTokenMatch.hasMatch())
        return false;

    const QString currentToken = firstTokenMatch.captured();
    const bool currentTokenIsAction = Utils::anyOf(
        actions, [&currentToken](const GitRebaseHighlighter::RebaseAction &action) {
            return currentToken == action.action || currentToken == QString(action.shortcut);
        });
    if (!currentTokenIsAction)
        return false;

    // Insert the new action before removing the old token (rather than the
    // other way round) so that undoing the replacement leaves the cursor at
    // the start of the line: undo replays the insert last, and undoing an
    // insert places the cursor at its insertion point.
    const int blockPosition = cursor.block().position();
    QTextCursor tokenCursor = cursor;
    tokenCursor.beginEditBlock();
    tokenCursor.setPosition(blockPosition);
    tokenCursor.insertText(pressedAction->action);
    tokenCursor.setPosition(blockPosition + pressedAction->action.size());
    tokenCursor.setPosition(blockPosition + pressedAction->action.size() + currentToken.size(),
                            QTextCursor::KeepAnchor);
    tokenCursor.removeSelectedText();
    tokenCursor.setPosition(blockPosition);
    tokenCursor.endEditBlock();
    TextEditor::setTextCursorOf(editor, tokenCursor);
    return true;
}

// The rebase script's key handling, parented to each of its editors - which
// is where the view looks for what takes a key before it does.
class GitRebaseKeyHandler final : public TextEditor::EditHandler
{
public:
    explicit GitRebaseKeyHandler(Core::IEditor *editor)
        : EditHandler(editor)
        , m_editor(editor)
    {}

    bool handleKeyPress(QKeyEvent *event, const std::function<void()> &) final
    {
        return gitReplaceRebaseAction(m_editor, event);
    }

private:
    Core::IEditor * const m_editor;
};

void gitAddDiffActions(QMenu *menu, VcsBase::VcsEditorDocument *document, const DiffChunk &chunk)
{
    menu->addSeparator();

    const QPointer<VcsBase::VcsEditorDocument> held(document);
    QAction *stageAction = menu->addAction(Tr::tr("Stage Chunk..."));
    QObject::connect(stageAction, &QAction::triggered, document, [held, chunk] {
        if (held)
            gitApplyDiffChunk(held, chunk, PatchAction::Apply);
    });

    QAction *unstageAction = menu->addAction(Tr::tr("Unstage Chunk..."));
    QObject::connect(unstageAction, &QAction::triggered, document, [held, chunk] {
        if (held)
            gitApplyDiffChunk(held, chunk, PatchAction::Revert);
    });
}

static FilePath gitSourceWorkingDirectory(VcsBase::VcsEditorDocument *document)
{
    return GitClient::fileWorkingDirectory(VcsBase::source(document));
}

QString gitDecorateVersion(VcsBase::VcsEditorDocument *document, const QString &revision)
{
    // Format verbose, hash being first token
    return gitClient().synchronousShortDescription(gitSourceWorkingDirectory(document), revision);
}

QStringList gitAnnotationPreviousVersions(VcsBase::VcsEditorDocument *document,
                                          const QString &revision)
{
    const Utils::FilePath repository = gitSourceWorkingDirectory(document);
    QStringList revisions;
    QString errorMessage;
    // Get the hashes of the file.
    if (!gitClient().synchronousParentRevisions(repository, revision, &revisions, &errorMessage)) {
        VcsOutputWindow::appendSilently(repository, errorMessage);
        return {};
    }
    return revisions;
}

bool gitIsValidRevision(const QString &revision)
{
    return gitClient().isValidRevision(revision);
}

void gitAddChangeActions(QMenu *menu, VcsBase::VcsEditorDocument *document,
                         const QString &change, int line)
{
    const EditorContentType type = document->contentType();
    if (type == OtherContent)
        return;

    if (type == LogOutput)
        line = 1;

    GitClient::addChangeActions(menu, VcsBase::source(document), change, line);
}

FilePath gitFileNameForLine(VcsBase::VcsEditorDocument *document, int line)
{
    // 7971b6e7 share/qtcreator/dumper/dumper.py  228  (hjk
    const FilePath source = VcsBase::source(document);
    QTextBlock block = document->document()->findBlockByLineNumber(line - 1);
    QTC_ASSERT(block.isValid(), return source);
    static const QRegularExpression renameExp(
        "^" CHANGE_PATTERN "\\s+(.+?)(?:\\s+\\d+)?\\s{2,}\\(");
    const QRegularExpressionMatch match = renameExp.match(block.text());
    if (match.hasMatch()) {
        const QString fileName = match.captured(1).trimmed();
        if (!fileName.isEmpty())
            return FilePath::fromString(fileName);
    }
    return source;
}

VcsBase::VcsBaseEditorParameters gitEditorParameters(
    EditorContentType type, Id id, const QString &displayName, const QString &mimeType,
    const std::function<void(const FilePath &, const QString &)> &describe)
{
    VcsBase::VcsBaseEditorParameters parameters{type, id, displayName, mimeType, describe,
                                                gitChangeUnderCursor, gitResolveDiffTarget};
    parameters.isValidRevision = gitIsValidRevision;
    parameters.decorateVersion = gitDecorateVersion;
    parameters.annotationPreviousVersions = gitAnnotationPreviousVersions;
    parameters.addChangeActions = gitAddChangeActions;
    parameters.addDiffActions = gitAddDiffActions;
    parameters.fileNameForLine = gitFileNameForLine;
    parameters.annotationHighlighterCreator
        = VcsBase::getAnnotationHighlighterCreator<GitAnnotationHighlighter>();
    // The commit message and the rebase script are files Git hands over to
    // be edited: typed into, opened at the top each time - new text under
    // the same old name - their repository and encoding learned as they open;
    // and, like a log, they name changes.
    const bool commit = id == Id(Git::Constants::GIT_COMMIT_TEXT_EDITOR_ID);
    const bool rebase = id == Id(Git::Constants::GIT_REBASE_EDITOR_ID);
    if (commit || rebase) {
        parameters.readOnly = false;
        parameters.restoresState = false;
        parameters.aboutToOpen = gitAboutToOpen;
        parameters.changeLinksInOtherContent = true;
    }
    // The script's keys: an action's shortcut at the start of a line replaces
    // the action there.
    if (rebase)
        parameters.decorateEditor = [](Core::IEditor *editor) { new GitRebaseKeyHandler(editor); };
    /* Diff format:
        diff --git a/src/plugins/git/giteditor.cpp b/src/plugins/git/giteditor.cpp
        index 40997ff..4e49337 100644
        --- a/src/plugins/git/giteditor.cpp
        +++ b/src/plugins/git/giteditor.cpp
    */
    parameters.diffFilePattern = "^(?:diff --git a/|index |[+-]{3} (?:/dev/null|[ab]/(.+$)))";
    parameters.logEntryPattern = "^commit ([0-9a-f]{8})[0-9a-f]{32}";
    parameters.annotationEntryPattern = "^(" CHANGE_PATTERN ") ";
    parameters.annotateRevisionTextFormat = Tr::tr("&Blame %1");
    parameters.annotatePreviousRevisionTextFormat = Tr::tr("Blame &Parent Revision %1");
    parameters.revisionSubject = gitRevisionSubject;
    parameters.putOutput = gitPutOutput;
    // The reflog is a log of its own shape: its entries, their subjects and
    // its colours are its highlighter's.
    if (id == Id(Git::Constants::GIT_REFLOG_EDITOR_ID)) {
        parameters.logEntryPattern = GitReflogHighlighter::entryPattern().pattern();
        parameters.revisionSubject = [](const QTextBlock &block) {
            const QString text = block.text();
            return text.mid(text.indexOf(' ') + 1);
        };
        parameters.syntaxHighlighterCreator = [] { return new GitReflogHighlighter; };
    }
    return parameters;
}

void gitResolveDiffTarget(VcsBase::VcsEditorDocument *document,
                          const VcsBase::DiffTarget &target,
                          const Utils::Link &link,
                          const Utils::LinkHandler &callback)
{
    const QString revision = document->revisionForLine(target.contextBlock.blockNumber());

    const FilePath contextPath = !document->workingDirectory().isEmpty()
            ? document->workingDirectory()
            : GitClient::fileWorkingDirectory(VcsBase::source(document));
    const FilePath topLevel = VcsManager::findTopLevelForDirectory(contextPath);
    if (topLevel.isEmpty() || revision.isEmpty()) {
        callback(link);
        return;
    }

    const FilePath relativePath = target.filePath.relativeChildPath(topLevel);
    if (relativePath.isEmpty()) {
        callback(link);
        return;
    }

    gitClient().resolveLine(topLevel,
                            relativePath.toUrlishString(),
                            target.line,
                            revision,
                            [topLevel, link, callback]
                            (const QString &resolvedFilePath, int resolvedLine) {
        Utils::Link resolved = link;
        resolved.targetFilePath = topLevel.pathAppended(resolvedFilePath);
        if (resolvedLine > 0)
            resolved.target.line = resolvedLine;
        callback(resolved);
    });
}


} // Git::Internal
