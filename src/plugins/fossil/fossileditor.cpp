// Copyright (c) 2018 Artur Shepilko
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "fossileditor.h"

#include "annotationhighlighter.h"
#include "constants.h"
#include "fossilclient.h"
#include "fossiltr.h"

#include <utils/qtcassert.h>

#include <vcsbase/vcsbaseeditor.h>
#include <vcsbase/vcsbaseplugin.h>

#include <QRegularExpression>
#include <QTextCursor>

namespace Fossil::Internal {

QString fossilChangeUnderCursor(const QTextCursor &cursorIn)
{
    static const QRegularExpression exactChangesetId(Constants::CHANGESET_ID_EXACT);
    QTC_CHECK(exactChangesetId.isValid());
    QTextCursor cursor = cursorIn;
    cursor.select(QTextCursor::WordUnderCursor);
    if (cursor.hasSelection()) {
        const QString change = cursor.selectedText();
        const QRegularExpressionMatch exactChangesetIdMatch = exactChangesetId.match(change);
        if (exactChangesetIdMatch.hasMatch())
            return change;
    }
    return {};
}

static QString fossilDecorateVersion(VcsBase::VcsEditorDocument *document, const QString &revision)
{
    static const int shortChangesetIdSize(10);
    static const int maxTextSize(120);

    const Utils::FilePath workingDirectory = VcsBase::source(document).parentDir();
    const RevisionInfo revisionInfo =
        fossilClient().synchronousRevisionQuery(workingDirectory, revision, true);

    // format: 'revision (committer "comment...")'
    QString output = revision.left(shortChangesetIdSize)
            + " (" + revisionInfo.committer
            + " \"" + revisionInfo.commentMsg.left(maxTextSize);

    if (output.size() > maxTextSize) {
        output.truncate(maxTextSize - 3);
        output.append("...");
    }
    output.append("\")");
    return output;
}

static QStringList fossilAnnotationPreviousVersions(VcsBase::VcsEditorDocument *document,
                                                    const QString &revision)
{
    const Utils::FilePath workingDirectory = VcsBase::source(document).parentDir();
    const RevisionInfo revisionInfo =
        fossilClient().synchronousRevisionQuery(workingDirectory, revision);
    if (revisionInfo.parentId.isEmpty())
        return {};

    QStringList revisions{revisionInfo.parentId};
    revisions.append(revisionInfo.mergeParentIds);
    return revisions;
}

VcsBase::VcsBaseEditorParameters fossilEditorParameters(
    VcsBase::EditorContentType type, Utils::Id id, const QString &displayName,
    const QString &mimeType,
    const std::function<void(const Utils::FilePath &, const QString &)> &describe)
{
    VcsBase::VcsBaseEditorParameters parameters{type, id, displayName, mimeType, describe,
                                                fossilChangeUnderCursor};
    parameters.decorateVersion = fossilDecorateVersion;
    parameters.annotationPreviousVersions = fossilAnnotationPreviousVersions;
    parameters.annotationHighlighterCreator
        = VcsBase::getAnnotationHighlighterCreator<FossilAnnotationHighlighter>();
    parameters.annotateRevisionTextFormat = Tr::tr("&Annotate %1");
    parameters.annotatePreviousRevisionTextFormat = Tr::tr("Annotate &Parent Revision %1");
    parameters.diffFilePattern = Constants::DIFFFILE_ID_EXACT;
    parameters.logEntryPattern = "^.*\\[([0-9a-f]{5,40})\\]";
    parameters.annotationEntryPattern = QString("^") + Constants::CHANGESET_ID + " ";
    return parameters;
}

} // namespace Fossil::Internal
