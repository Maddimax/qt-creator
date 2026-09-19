// Copyright (C) 2016 Hugues Delorme
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "bazaareditor.h"

#include "annotationhighlighter.h"
#include "bazaartr.h"
#include "constants.h"

#include <vcsbase/vcsbaseeditor.h>

#include <QRegularExpression>
#include <QString>
#include <QTextCursor>

#define BZR_CHANGE_PATTERN "[0-9]+"

namespace Bazaar::Internal {

QString bazaarChangeUnderCursor(const QTextCursor &cursorIn)
{
    static const QRegularExpression changesetId(QLatin1String(Constants::CHANGESET_ID));
    static const QRegularExpression exactChangesetId(
        QLatin1String(Constants::CHANGESET_ID_EXACT));
    // The test is done in two steps: first we check if the line contains a
    // changesetId. Then we check if the cursor is over the changesetId itself
    // and not over "revno" or another part of the line.
    // The two steps are necessary because matching only for the changesetId
    // leads to many false-positives (a regex like "[0-9]+" matches a lot of text).
    const int cursorCol = cursorIn.columnNumber();
    QTextCursor cursor = cursorIn;
    cursor.select(QTextCursor::LineUnderCursor);
    if (cursor.hasSelection()) {
        const QString line = cursor.selectedText();
        const QRegularExpressionMatch match = changesetId.match(line);
        if (match.hasMatch()) {
            const int start = match.capturedStart();
            const int stop = match.capturedEnd();
            if (start <= cursorCol && cursorCol <= stop) {
                cursor = cursorIn;
                cursor.select(QTextCursor::WordUnderCursor);
                if (cursor.hasSelection()) {
                    const QString change = cursor.selectedText();
                    if (exactChangesetId.match(change).hasMatch())
                        return change;
                }
            }
        }
    }
    return {};
}

VcsBase::VcsBaseEditorParameters bazaarEditorParameters(
    VcsBase::EditorContentType type, Utils::Id id, const QString &displayName,
    const QString &mimeType,
    const std::function<void(const Utils::FilePath &, const QString &)> &describe)
{
    VcsBase::VcsBaseEditorParameters parameters{type, id, displayName, mimeType, {}, describe,
                                                bazaarChangeUnderCursor};
    parameters.annotationHighlighterCreator
        = VcsBase::getAnnotationHighlighterCreator<BazaarAnnotationHighlighter>();
    // Diff format:
    // === <change> <file|dir> 'mainwindow.cpp'
    parameters.diffFilePattern = "^=== [a-z]+ [a-z]+ '(.+)'\\s*";
    parameters.logEntryPattern = "^revno: (\\d+)";
    parameters.annotationEntryPattern = "^(" BZR_CHANGE_PATTERN ") ";
    parameters.annotateRevisionTextFormat = Tr::tr("&Annotate %1");
    parameters.annotatePreviousRevisionTextFormat = Tr::tr("Annotate &parent revision %1");
    return parameters;
}

} // Bazaar::Internal
