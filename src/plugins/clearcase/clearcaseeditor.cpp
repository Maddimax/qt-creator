// Copyright (C) 2016 AudioCodes Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "clearcaseeditor.h"

#include "annotationhighlighter.h"
#include "clearcasetr.h"

#include <utils/qtcassert.h>

#include <vcsbase/vcsbaseeditor.h>

#include <QRegularExpression>
#include <QTextCursor>

namespace ClearCase::Internal {

QString clearCaseChangeUnderCursor(const QTextCursor &c)
{
    static const QRegularExpression versionNumberPattern(
        QLatin1String("[\\\\/]main[\\\\/][^ \t\n\"]*"));
    QTC_ASSERT(versionNumberPattern.isValid(), return {});
    QTextCursor cursor = c;
    // Any number is regarded as change number.
    cursor.select(QTextCursor::BlockUnderCursor);
    if (!cursor.hasSelection())
        return QString();
    const QString change = cursor.selectedText();
    // Annotation output has number, log output has revision numbers
    // as r1, r2...
    const QRegularExpressionMatch match = versionNumberPattern.match(change);
    if (match.hasMatch())
        return match.captured();
    return QString();
}

VcsBase::VcsBaseEditorParameters clearCaseEditorParameters(
    VcsBase::EditorContentType type, Utils::Id id, const QString &displayName,
    const QString &mimeType,
    const std::function<void(const Utils::FilePath &, const QString &)> &describe)
{
    VcsBase::VcsBaseEditorParameters parameters{type, id, displayName, mimeType, {}, describe,
                                                clearCaseChangeUnderCursor};
    parameters.annotationHighlighterCreator
        = VcsBase::getAnnotationHighlighterCreator<ClearCaseAnnotationHighlighter>();
    // Diff formats:
    // "+++ D:\depot\...\mainwindow.cpp@@\main\3" (versioned)
    // "+++ D:\depot\...\mainwindow.cpp[TAB]Sun May 01 14:22:37 2011" (local)
    parameters.diffFilePattern = "^[-+]{3} ([^\\t]+?)(?:@@|\\t)";
    parameters.logEntryPattern = "version \"([^\"]+)\"";
    parameters.annotateRevisionTextFormat = Tr::tr("Annotate version \"%1\"");
    parameters.annotationEntryPattern = "([^|]*)\\|[^\\n]*\\n";
    parameters.annotationSeparatorPattern = "\\n-{30}";
    return parameters;
}

} // ClearCase::Internal
