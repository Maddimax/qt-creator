// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "vcsbase_global.h"
#include "baseannotationhighlighter.h"

#include <coreplugin/patchtool.h>

#include <texteditor/textdocument.h>

#include <utils/filepath.h>
#include <utils/id.h>
#include <utils/link.h>

#include <QAbstractListModel>
#include <QRegularExpression>
#include <QSet>
#include <QTextBlock>

#include <functional>

QT_BEGIN_NAMESPACE
class QAction;
class QMenu;
class QTextBlock;
class QTextCursor;
class QWidget;

namespace QtTaskTree {
class ExecutableItem;
template <typename StorageStruct>
class Storage;
}
QT_END_NAMESPACE

namespace TextEditor { class ToolBarChoice; }

namespace VcsBase {

class CommandResult;
class VcsBaseEditorConfig;

namespace Internal {

class VcsEditorDocumentPrivate;

// A URL under the cursor, as a log shows them: an http(s) URL, a Jira key
// after "Fixes:" or "Task-number:", a Gerrit Change-Id - or an e-mail address.
// The span is within the cursor's block; the url is what to open.
class UrlUnderCursor
{
public:
    bool isValid() const { return startColumn >= 0; }

    int startColumn = -1;
    int length = 0;
    QString url;
};
UrlUnderCursor urlUnderCursor(const QTextCursor &cursor);
UrlUnderCursor emailUnderCursor(const QTextCursor &cursor);

} // namespace Internal

enum EditorContentType
{
    LogOutput,
    AnnotateOutput,
    DiffOutput,
    OtherContent
};

using BaseAnnotationHighlighterCreator
    = std::function<BaseAnnotationHighlighter *(const Annotation &annotation)>;
template<typename T>
BaseAnnotationHighlighterCreator getAnnotationHighlighterCreator()
{
    return [](const Annotation &annotation) { return new T(annotation); };
}

class DiffChunk;
class DiffTarget;
class VcsEditorDocument;

class VCSBASE_EXPORT VcsBaseEditorParameters
{
public:
    EditorContentType type;
    Utils::Id id;
    QString displayName;
    QString mimeType;
    std::function<QWidget *()> editorWidgetCreator;
    std::function<void (const Utils::FilePath &, const QString &)> describeFunc;
    // The change under a cursor - a revision, a change number - or nothing.
    // What a plain click on it describes, in a view that is not the widget
    // editor; a VCS that leaves it unset offers only its URLs there.
    std::function<QString(const QTextCursor &)> changeUnderCursor;
    // Where a line of a diff goes when followed, better than the file and
    // line the chunk header names, where the VCS can tell - Git resolves the
    // line for the revision. Given the target as found and the link that
    // stands for it; answers through the handler, since the VCS may have to
    // ask a process. Unset, the link is followed as found.
    std::function<void(VcsEditorDocument *document, const DiffTarget &target,
                       const Utils::Link &link, const Utils::LinkHandler &callback)>
        resolveDiffTarget;
    // The rest of what the widget subclasses answered through virtuals, for
    // the menu a right click on a change or a chunk offers. Unset means the
    // base answer: every revision valid, shown as it is, no previous
    // revisions, no entries of the VCS's own, and the source as the file a
    // line is of.
    std::function<bool(const QString &revision)> isValidRevision;
    std::function<QString(VcsEditorDocument *document, const QString &revision)> decorateVersion;
    std::function<QStringList(VcsEditorDocument *document, const QString &revision)>
        annotationPreviousVersions;
    std::function<void(QMenu *menu, VcsEditorDocument *document, const QString &change, int line)>
        addChangeActions;
    std::function<void(QMenu *menu, VcsEditorDocument *document, const DiffChunk &chunk)>
        addDiffActions;
    std::function<Utils::FilePath(VcsEditorDocument *document, int line)> fileNameForLine;
    // What colours an annotation: a highlighter over its changes, the VCS's own.
    BaseAnnotationHighlighterCreator annotationHighlighterCreator;
    // Whether an output of neither log nor annotation type has change links
    // all the same - Git's commit and rebase editors do.
    bool changeLinksInOtherContent = false;
};

class VCSBASE_EXPORT DiffChunk
{
public:
    bool isValid() const;
    QByteArray asPatch(const Utils::FilePath &workingDirectory) const;

    Utils::FilePath fileName;
    QByteArray chunk;
    QByteArray header;
};

// The place in a file a line of a diff stands for, and the block it was
// found from, which is what a VCS with more to say about the jump (Git
// resolves the line for the revision) reads the revision from.
class VCSBASE_EXPORT DiffTarget
{
public:
    bool isValid() const { return !filePath.isEmpty() && line > 0; }

    Utils::FilePath filePath;
    int line = 0;
    QTextBlock contextBlock;
};

// What the tool bar's entries browser offers: one row per file in a diff, or
// per entry in a log, and the line each starts on.
class VCSBASE_EXPORT VcsEditorSections : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role { LineRole = Qt::UserRole + 1 };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QStringList entries() const;
    QList<int> lines() const;
    // The row whose section \a line, counted from zero, falls in; -1 before
    // the first.
    int sectionOfLine(int line) const;

    void reset(const QStringList &entries, const QList<int> &lines);

private:
    QStringList m_entries;
    QList<int> m_lines;
};

// What a VCS editor is about, apart from drawing it: which command's output
// this is, where it came from, what its lines mean. The widget editor keeps
// a view on this; another view can keep its own.
class VCSBASE_EXPORT VcsEditorDocument : public TextEditor::TextDocument
{
    Q_OBJECT

public:
    explicit VcsEditorDocument(const VcsBaseEditorParameters &parameters);
    ~VcsEditorDocument() override;

    const VcsBaseEditorParameters &parameters() const;
    EditorContentType contentType() const;

    Utils::FilePath workingDirectory() const;
    void setWorkingDirectory(const Utils::FilePath &workingDirectory);

    // Where to go once the output has arrived.
    int defaultLineNumber() const;
    void setDefaultLineNumber(int line);
    void gotoDefaultLine();

    // Runs \a task - a VCS command - and makes its output this document's
    // text, through setOutput(); busy meanwhile, at the default line after.
    // A failure puts a line saying so in the text and the error in the VCS
    // output pane. Starting another cancels the one running.
    void executeTask(const QtTaskTree::ExecutableItem &task,
                     const QtTaskTree::Storage<CommandResult> &resultStorage);
    // What a command's output becomes: the text as it is, unless a hook says
    // otherwise - the widget editor's virtual setPlainText(), which Git
    // overrides to colour a log and to trim a blame.
    using OutputHook = std::function<void(const QString &output)>;
    void setOutputHook(const OutputHook &hook);
    void setOutput(const QString &output);

    QString annotateRevisionTextFormat() const;
    void setAnnotateRevisionTextFormat(const QString &format);
    QString annotatePreviousRevisionTextFormat() const;
    void setAnnotatePreviousRevisionTextFormat(const QString &format);
    bool isFileLogAnnotateEnabled() const;
    void setFileLogAnnotateEnabled(bool enabled);

    VcsBaseEditorConfig *editorConfig() const;
    void setEditorConfig(VcsBaseEditorConfig *config);

    // The shape of the output, declared by the VCS: how a diff names a file,
    // how a log starts an entry, how an annotation names a change. Each has
    // to capture what it names.
    void setDiffFilePattern(const QString &pattern);
    QRegularExpression diffFilePattern() const;
    void setLogEntryPattern(const QString &pattern);
    QRegularExpression logEntryPattern() const;
    void setAnnotationEntryPattern(const QString &pattern);
    void setAnnotationSeparatorPattern(const QString &pattern);
    Annotation annotation() const;
    // Every change an annotation names, up to its separator if it has one.
    QSet<QString> annotationChanges() const;
    // What colours the annotation, where the parameters do not say: the widget
    // subclass's answer, until its VCS's parameters carry it.
    void setAnnotationHighlighterCreator(const BaseAnnotationHighlighterCreator &creator);

    // The file a diff names, on disk: looked for under the working directory,
    // beside the source, at the VCS top level above the source, and as given
    // - what VcsBaseEditorWidget::findDiffFile() always did.
    QString resolveDiffFile(const QString &fileName) const;
    // The same, through the VCS where it knows better - Perforce maps depot
    // paths - and resolveDiffFile() where it does not.
    using DiffFileResolver = std::function<QString(const QString &)>;
    void setDiffFileResolver(const DiffFileResolver &resolver);
    QString findDiffFile(const QString &fileName) const;
    // The file the diff header above \a inBlock names, resolved; \a header
    // takes the header's lines, for a patch.
    QString fileNameFromDiffSpecification(const QTextBlock &inBlock,
                                          QString *header = nullptr) const;

    // The chunk of a diff around \a cursor - nothing in a header - and what
    // can be done with it: applied or reverted with patch, after asking.
    DiffChunk diffChunk(const QTextCursor &cursor) const;
    bool canApplyDiffChunk(const DiffChunk &chunk) const;
    bool applyDiffChunk(const DiffChunk &chunk, Core::PatchAction patchAction) const;
    void applyChunk(const DiffChunk &chunk, Core::PatchAction patchAction);
    // The place in the file a line of a diff stands for, counted through the
    // chunk header above it; invalid in a header or where the file is not
    // there.
    DiffTarget diffTargetAt(const QTextCursor &cursor) const;
    // The revision the log entry \a line, counted from zero, belongs to -
    // what the entry's pattern captured - or nothing outside any entry.
    QString revisionForLine(int line) const;

    // The sections, found from the text with the patterns above whenever the
    // text changes. A log entry's subject is the one thing the document
    // cannot know; until it is told, a log's entries have none.
    using BlockToString = std::function<QString(const QTextBlock &)>;
    void setRevisionSubjectHook(const BlockToString &revisionSubject);
    VcsEditorSections *sections() const;
    void updateSections();

    // The sections as what the tool bar offers - the entry the caret is in,
    // and a jump to any other; a log or a diff has one - and then the editor
    // config's choices, once there is a config.
    QList<TextEditor::ToolBarChoice *> toolBarChoices() const override;
    // The editor config's fields, once there is a config.
    QList<TextEditor::ToolBarField *> toolBarFields() const override;

    // What a right click at \a cursor offers, as the widget editor's handlers
    // offered it: for a change, to copy, describe and annotate it; for a URL
    // or an address, to open and to copy; and for a log or a diff, to paste
    // it. Built for the click and kept until the next.
    QList<QAction *> contextMenuActions(const QTextCursor &cursor) override;
    // The editor config's toggles, for whichever view draws the document's
    // tool bar actions - both do.
    QList<QAction *> ownToolBarActions() const override;

signals:
    // An annotation of \a file at \a change was asked for, from \a line of
    // this output. What the widget editor's signal of the same name says,
    // for a view that is not it.
    void annotateRevisionRequested(const Utils::FilePath &workingDirectory,
                                   const QString &file, const QString &change, int line);
    // A chunk was reverted with patch; whoever showed the diff reloads it.
    void diffChunkReverted();

private:
    void requestAnnotation(const QString &change, int line);
    void updateHighlighter();
    void activateAnnotation();

    Internal::VcsEditorDocumentPrivate *const d;
};

#ifdef WITH_TESTS
QObject *createVcsEditorDocumentTest();
#endif

} // namespace VcsBase
