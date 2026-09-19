// Copyright (C) 2016 Brian McGillion
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "mercurialeditor.h"

#include "annotationhighlighter.h"
#include "constants.h"
#include "mercurialclient.h"
#include "mercurialtr.h"

#include <vcsbase/vcsbaseeditor.h>
#include <vcsbase/vcsbaseplugin.h>

#include <QRegularExpression>
#include <QString>
#include <QTextCursor>

using namespace Utils;

namespace Mercurial::Internal {

QString mercurialChangeUnderCursor(const QTextCursor &cursorIn)
{
    static const QRegularExpression exactIdentifier12(
        QRegularExpression::anchoredPattern(Constants::CHANGEIDEXACT12));
    static const QRegularExpression exactIdentifier40(
        QRegularExpression::anchoredPattern(Constants::CHANGEIDEXACT40));
    QTextCursor cursor = cursorIn;
    cursor.select(QTextCursor::WordUnderCursor);
    if (cursor.hasSelection()) {
        const QString change = cursor.selectedText();
        if (exactIdentifier12.match(change).hasMatch())
            return change;
        if (exactIdentifier40.match(change).hasMatch())
            return change;
    }
    return {};
}

static QString mercurialDecorateVersion(VcsBase::VcsEditorDocument *document,
                                        const QString &revision)
{
    const FilePath workingDirectory = VcsBase::source(document).absolutePath();
    // Format with short summary
    return mercurialClient().shortDescriptionSync(workingDirectory, revision);
}

static QStringList mercurialAnnotationPreviousVersions(VcsBase::VcsEditorDocument *document,
                                                       const QString &revision)
{
    const FilePath filePath = VcsBase::source(document);
    const FilePath workingDirectory = filePath.absolutePath();
    // Retrieve parent revisions
    return mercurialClient().parentRevisionsSync(workingDirectory, filePath.fileName(), revision);
}

VcsBase::VcsBaseEditorParameters mercurialEditorParameters(
    VcsBase::EditorContentType type, Id id, const QString &displayName, const QString &mimeType,
    const std::function<void(const FilePath &, const QString &)> &describe)
{
    VcsBase::VcsBaseEditorParameters parameters{type, id, displayName, mimeType, describe,
                                                mercurialChangeUnderCursor};
    parameters.decorateVersion = mercurialDecorateVersion;
    parameters.annotationPreviousVersions = mercurialAnnotationPreviousVersions;
    parameters.annotationHighlighterCreator
        = VcsBase::getAnnotationHighlighterCreator<MercurialAnnotationHighlighter>();
    parameters.diffFilePattern = Constants::DIFFIDENTIFIER;
    parameters.logEntryPattern = "^changeset:\\s+(\\S+)$";
    parameters.annotationEntryPattern = Constants::CHANGESETID12;
    parameters.annotateRevisionTextFormat = Tr::tr("&Annotate %1");
    parameters.annotatePreviousRevisionTextFormat = Tr::tr("Annotate &parent revision %1");
    return parameters;
}

} // Mercurial::Internal
