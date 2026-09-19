// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <vcsbase/vcsbaseeditor.h>

namespace Utils { class FilePath; }

namespace Git::Internal {


// The change under \a cursor: any word of seven to forty hex digits. What
// the Git editors' parameters carry, so that a view that is not the widget
// editor can offer to describe it.
QString gitChangeUnderCursor(const QTextCursor &cursor);

// Where a line of a diff in \a document goes: the file and line as found,
// unless the line belongs to a revision in a repository, in which case git
// says where that line is in the working tree now. Answers \a callback
// either way; the Git editors' parameters carry it, and the widget's own
// jump goes through it.
void gitResolveDiffTarget(VcsBase::VcsEditorDocument *document,
                          const VcsBase::DiffTarget &target,
                          const Utils::Link &link,
                          const Utils::LinkHandler &callback);

// The rest of what a Git editor answers about a change or a chunk, for the
// menu: whether a revision is one, its short description, its parents, Git's
// own entries for a change and for a chunk (staging it), and the file a blame
// line is of. The widget's virtuals call these; the parameters carry them.
bool gitIsValidRevision(const QString &revision);
QString gitDecorateVersion(VcsBase::VcsEditorDocument *document, const QString &revision);
QStringList gitAnnotationPreviousVersions(VcsBase::VcsEditorDocument *document,
                                          const QString &revision);
void gitAddChangeActions(QMenu *menu, VcsBase::VcsEditorDocument *document,
                         const QString &change, int line);
void gitAddDiffActions(QMenu *menu, VcsBase::VcsEditorDocument *document,
                       const VcsBase::DiffChunk &chunk);
void gitApplyDiffChunk(VcsBase::VcsEditorDocument *document, const VcsBase::DiffChunk &chunk,
                       Core::PatchAction patchAction);
Utils::FilePath gitFileNameForLine(VcsBase::VcsEditorDocument *document, int line);

// The parameters of a Git editor of \a type: the six above and the earlier
// two, with the describe function the caller supplies.
VcsBase::VcsBaseEditorParameters gitEditorParameters(
    VcsBase::EditorContentType type, Utils::Id id, const QString &displayName,
    const QString &mimeType,
    const std::function<void(const Utils::FilePath &, const QString &)> &describe);

// What a log is narrowed by, as git log arguments: the author, the message
// and the pickaxe where given, and -i unless case matters.
QStringList gitLogFilterArguments(const QString &author, const QString &grep,
                                  const QString &pickaxe, bool caseSensitive);
// The log editor's config - its toggles, its filter fields and its reload -
// which reads its filter in arguments().
VcsBase::VcsBaseEditorConfig *createGitLogConfig(bool fileRelated, QObject *parent);
// What a command's output becomes in a Git document: a log coloured from its
// escape codes, a blame trimmed as the settings say, anything else as it is.
void gitPutOutput(VcsBase::VcsEditorDocument *document, const QString &output);
// A log entry's subject: the first line after the first blank one.
QString gitRevisionSubject(const QTextBlock &block);

} // Git::Internal
