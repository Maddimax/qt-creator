// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "vcsbase_global.h"
#include "vcseditordocument.h"

#include <texteditor/texteditor.h>

namespace VcsBase {

class VcsEditorFactory;

// What the VCS plugins ask about editors and their sources: which encoding
// output about a file is in, which editor shows what, where the caret is.
// Every VCS editor is the Qt Quick editor over a VcsEditorDocument, and
// these answer for the widget editor too where a file is open in one.
namespace VcsBaseEditor {

// Utility to find the codec for a source (file or directory), querying
// the editor manager and the project managers (defaults to system codec).
// The codec should be set on editors displaying diff or annotation
// output.
VCSBASE_EXPORT Utils::TextEncoding getEncoding(const Utils::FilePath &source);
VCSBASE_EXPORT Utils::TextEncoding getEncoding(const Utils::FilePath &workingDirectory,
                                               const QStringList &files);

// Utility to find the line number of the current editor. Optionally,
// pass in the file name to match it. To be used when jumping to current
// line number in a 'annnotate current file' slot, which checks if the
// current file originates from the current editor or the project selection.
VCSBASE_EXPORT int lineNumberOfCurrentEditor(const Utils::FilePath &currentFile = {});

//Helper to go to line of editor if it is a text editor
VCSBASE_EXPORT bool gotoLineOfEditor(Core::IEditor *e, int lineNumber);

// Convenience functions to determine the source to pass on to a diff
// editor if one has a call consisting of working directory and file arguments.
// ('git diff XX' -> 'XX' , 'git diff XX file' -> 'XX/file').
VCSBASE_EXPORT Utils::FilePath getSource(const Utils::FilePath &workingDirectory,
                                         const QString &fileName);
VCSBASE_EXPORT Utils::FilePath getSource(const Utils::FilePath &workingDirectory,
                                         const QStringList &fileNames);
// Convenience functions to determine an title/id to identify the editor
// from the arguments (','-joined arguments or directory) + revision.
VCSBASE_EXPORT QString getTitleId(const Utils::FilePath &workingDirectory,
                                  const QStringList &fileNames,
                                  const QString &revision = {});

/* Tagging editors: Sometimes, an editor should be re-used, for example, when showing
 * a diff of the same file with different diff-options. In order to be able to find
 * the editor, they get a 'tag' containing type and parameters (dynamic property string). */
VCSBASE_EXPORT void tagEditor(Core::IEditor *e, const QString &tag);
VCSBASE_EXPORT Core::IEditor *locateEditorByTag(const QString &tag);
VCSBASE_EXPORT QString editorTag(EditorContentType t, const Utils::FilePath &workingDirectory,
                                 const QStringList &files, const QString &revision = {});

#ifdef WITH_TESTS
// What every VCS plugin's tests ask of an editor its factory builds: that
// the document finds a diff's file and a log's entries.
VCSBASE_EXPORT void testDiffFileResolving(const VcsEditorFactory &factory);
VCSBASE_EXPORT void testLogResolving(const VcsEditorFactory &factory,
                                     const QByteArray &data,
                                     const QByteArray &entry1,
                                     const QByteArray &entry2);
#endif

} // namespace VcsBaseEditor

class VCSBASE_EXPORT VcsEditorFactory : public TextEditor::TextEditorFactory
{
public:
    explicit VcsEditorFactory(const VcsBaseEditorParameters &parameters);
    ~VcsEditorFactory() override;
};

} // namespace VcsBase
