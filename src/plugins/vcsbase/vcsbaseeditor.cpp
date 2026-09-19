// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "vcsbaseeditor.h"

#include "vcsbaseplugin.h"

#include <coreplugin/editormanager/documentmodel.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/textdocument.h>

#include <diffeditor/diffeditorconstants.h>

#include <projectexplorer/editorconfiguration.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>

#include <utils/algorithm.h>

#include <QTextBlock>
#include <QTextDocument>

#include <memory>

/*!
    \enum VcsBase::EditorContentType

    This enum describes the contents of a VcsBaseEditor and its interaction.

    \value RegularCommandOutput  No special handling.
    \value LogOutput  Log of a file under revision control. Provide a
           description of the change that users can click to view detailed
           information about the change and \e Annotate for the log of a
           single file.
    \value AnnotateOutput  Color contents per change number and provide a
           clickable change description.
           Context menu offers annotate previous version functionality.
           Expected format:
           \code
           <change description>: file line
           \endcode
    \value DiffOutput  Diff output. Might include describe output, which consists of a
           header and diffs. Double-clicking the chunk opens the file. The context
           menu offers the functionality to revert the chunk.

    \sa VcsBase::VcsEditorDocument
*/

using namespace Core;
using namespace TextEditor;
using namespace Utils;

namespace VcsBase {

/*!
    \namespace VcsBase::VcsBaseEditor

    \brief The VcsBaseEditor namespace holds what the VCS plugins ask about
    editors and their sources.

    It was the widget editor's IEditor once; every VCS editor is the Qt Quick
    editor over a VcsEditorDocument now, and only the functions remain.
*/

/*!
    \class VcsBase::VcsBaseEditorParameters

    \brief The VcsBaseEditorParameters class is a helper class used to
    parametrize an editor with MIME type, context
    and id.

    The extension is currently only a suggestion when running
    version control commands with redirection.

    \sa VcsBase::VcsEditorDocument, VcsBase::VcsEditorFactory, VcsBase::EditorContentType
*/

namespace VcsBaseEditor {

static TextEncoding findFileCodec(const FilePath &source)
{
    IDocument *document = DocumentModel::documentForFilePath(source);
    if (auto textDocument = qobject_cast<BaseTextDocument *>(document))
        return textDocument->encoding();
    return {};
}

// Find the codec by checking the projects (root dir of project file)
static TextEncoding findProjectEncoding(const FilePath &dirPath)
{
    // Find the project dirPath relates to. A plain equality check would only
    // match when dirPath is exactly the project root, missing both files in
    // subdirectories (dirPath below the project) and repository directories
    // that host the project in a subdirectory (dirPath above the project, as
    // happens for "git show", which runs at the repository top level).
    const auto projects = ProjectExplorer::ProjectManager::projects();

    // Prefer the most specific project whose file tree contains dirPath.
    const ProjectExplorer::Project *containing = nullptr;
    for (const ProjectExplorer::Project *p : projects) {
        const FilePath projectDir = p->projectDirectory();
        if (dirPath != projectDir && !dirPath.isChildOf(projectDir))
            continue;
        if (!containing
            || containing->projectDirectory().path().size() < projectDir.path().size()) {
            containing = p;
        }
    }
    if (containing)
        return containing->editorConfiguration()->textEncoding();

    // Otherwise accept a project located below dirPath (repository hosting it).
    const auto *p = findOrDefault(projects, [&dirPath](const ProjectExplorer::Project *p) {
        return p->projectDirectory().isChildOf(dirPath);
    });
    return p ? p->editorConfiguration()->textEncoding() : TextEncoding();
}

TextEncoding getEncoding(const FilePath &source)
{
    if (!source.isEmpty()) {
        // Check file
        if (source.isFile())
            if (TextEncoding fc = findFileCodec(source); fc.isValid())
                return fc;
        // Find by project via directory
        if (TextEncoding pc = findProjectEncoding(source.isFile() ? source.absolutePath() : source); pc.isValid())
            return pc;
    }
    return QStringConverter::System;
}

TextEncoding getEncoding(const FilePath &workingDirectory, const QStringList &files)
{
    if (files.empty())
        return getEncoding(workingDirectory);
    return getEncoding(workingDirectory / files.front());
}

// Return line number of current editor if it matches.
int lineNumberOfCurrentEditor(const FilePath &currentFile)
{
    IEditor *ed = EditorManager::currentEditor();
    if (!ed)
        return -1;
    if (!currentFile.isEmpty()) {
        const IDocument *idocument  = ed->document();
        if (!idocument || idocument->filePath() != currentFile)
            return -1;
    }
    // Either view's caret - the widget's or the Qt Quick view's - where it is
    // on screen, and the middle of what is on screen where it is not: what
    // the reader is looking at, which is what an annotation should open on.
    const int cursorLine = lineColumnOf(ed).line;
    if (cursorLine <= 0)
        return -1;
    const auto [firstBlock, lastBlock] = visibleLinesIn(ed);
    if (firstBlock < 0)
        return cursorLine;
    const int firstLine = firstBlock + 1;
    const int lastLine = lastBlock + 1;
    if (firstLine <= cursorLine && cursorLine < lastLine)
        return cursorLine;
    return (firstLine + lastLine) / 2;
}

bool gotoLineOfEditor(IEditor *e, int lineNumber)
{
    if (lineNumber >= 0 && e) {
        e->gotoLine(lineNumber);
        return true;
    }
    return false;
}

// Return source file or directory string depending on parameters
// ('git diff XX' -> 'XX' , 'git diff XX file' -> 'XX/file').
FilePath getSource(const FilePath &workingDirectory, const QString &fileName)
{
    return workingDirectory.resolvePath(fileName);
}

FilePath getSource(const FilePath &workingDirectory, const QStringList &fileNames)
{
    return fileNames.size() == 1
            ? getSource(workingDirectory, fileNames.front())
            : workingDirectory;
}

QString getTitleId(const FilePath &workingDirectory,
                   const QStringList &fileNames,
                   const QString &revision)
{
    QStringList nonEmptyFileNames;
    for (const QString &fileName : fileNames) {
        if (!fileName.trimmed().isEmpty())
            nonEmptyFileNames.append(fileName);
    }

    QString rc;
    switch (nonEmptyFileNames.size()) {
    case 0:
        rc = workingDirectory.toUrlishString();
        break;
    case 1:
        rc = nonEmptyFileNames.front();
        break;
    default:
        rc = nonEmptyFileNames.join(", ");
        break;
    }
    if (!revision.isEmpty()) {
        rc += ':';
        rc += revision;
    }
    return rc;
}

// Tagging of editors for re-use.
QString editorTag(EditorContentType t, const FilePath &workingDirectory,
                  const QStringList &files, const QString &revision)
{
    const QChar colon = ':';
    QString rc = QString::number(t);
    rc += colon;
    if (!revision.isEmpty()) {
        rc += revision;
        rc += colon;
    }
    rc += workingDirectory.toUrlishString();
    if (!files.isEmpty()) {
        rc += colon;
        rc += files.join(colon);
    }
    return rc;
}

static const char tagPropertyC[] = "_q_VcsBaseEditorTag";

void tagEditor(IEditor *e, const QString &tag)
{
    e->document()->setProperty(tagPropertyC, QVariant(tag));
}

IEditor *locateEditorByTag(const QString &tag)
{
    const QList<IDocument *> documents = DocumentModel::openedDocuments();
    for (IDocument *document : documents) {
        const QVariant tagPropertyValue = document->property(tagPropertyC);
        if (tagPropertyValue.typeId() == QMetaType::QString && tagPropertyValue.toString() == tag)
            return DocumentModel::editorsForDocument(document).constFirst();
    }
    return nullptr;
}

} // namespace VcsBaseEditor

/*!
    \class VcsBase::VcsEditorFactory

    \brief The VcsEditorFactory class is the base class of the VCS plugins'
    editor factories: the Qt Quick editor over a VcsEditorDocument, with what
    the VCS's parameters say.

    \sa VcsBase::VcsEditorDocument, VcsBase::VcsBaseEditorParameters
*/

VcsEditorFactory::VcsEditorFactory(const VcsBaseEditorParameters &parameters)
{
    setId(parameters.id);
    setDisplayName(parameters.displayName);
    if (!parameters.mimeType.isEmpty()
        && parameters.mimeType != DiffEditor::Constants::DIFF_EDITOR_MIMETYPE) {
        addMimeType(parameters.mimeType);
    }

    setOptionalActionMask(OptionalActions::None);
    setDuplicatedSupported(false);

    setDocumentCreator([parameters] { return new VcsEditorDocument(parameters); });

    // The Qt Quick editor, for every VCS: what a widget subclass used to add
    // is in the parameters, and the document reads it.
    setUsesQuickEditor(true);
    setMarksVisible(false);
    setRevisionsVisible(false);
    // Output is not to be typed into, and is folded where it has chunks -
    // which is what the widget subclass asked for in init(). A file the VCS
    // hands over to be edited says so in the parameters, and whether an
    // editor reopened on it comes back where the last one was left.
    setReadOnly(parameters.readOnly);
    setRestoresState(parameters.restoresState);
    if (parameters.type == LogOutput || parameters.type == DiffOutput)
        setCodeFoldingSupported(true);
    if (parameters.decorateEditor)
        setEditorDecorator(parameters.decorateEditor);
}

VcsEditorFactory::~VcsEditorFactory() = default;

} // namespace VcsBase

#ifdef WITH_TESTS
#include <QTest>

namespace VcsBase {

namespace VcsBaseEditor {

// On the document of the editor the factory builds - the Qt Quick one, for
// every VCS.
void testDiffFileResolving(const VcsEditorFactory &factory)
{
    const std::unique_ptr<IEditor> editor(factory.createEditor());
    QVERIFY(editor);
    auto * const document = qobject_cast<VcsEditorDocument *>(editor->document());
    QVERIFY(document);

    QFETCH(QByteArray, header);
    QFETCH(QByteArray, fileName);
    QTextDocument doc(QString::fromLatin1(header));
    QTextBlock block = doc.lastBlock();
    // set source root for shadow builds
    VcsBase::setSource(document, FilePath::fromString(QString::fromLatin1(SRC_DIR)));
    QVERIFY(document->fileNameFromDiffSpecification(block).endsWith(QString::fromLatin1(fileName)));
}

void testLogResolving(const VcsEditorFactory &factory,
                      const QByteArray &data,
                      const QByteArray &entry1,
                      const QByteArray &entry2)
{
    const std::unique_ptr<IEditor> editor(factory.createEditor());
    QVERIFY(editor);
    auto * const document = qobject_cast<VcsEditorDocument *>(editor->document());
    QVERIFY(document);

    document->setPlainText(QLatin1String(data));
    // The document finds the entries; that the tool bar's browser shows them
    // is TextEditor's to test.
    QCOMPARE(document->sections()->entries().value(0), QString::fromLatin1(entry1));
    QCOMPARE(document->sections()->entries().value(1), QString::fromLatin1(entry2));
}

} // namespace VcsBaseEditor
} // namespace VcsBase

#endif
