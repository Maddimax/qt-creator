// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "subversioneditor.h"

#include "annotationhighlighter.h"
#include "subversiontr.h"

#include <utils/qtcassert.h>

#include <vcsbase/vcsbaseeditor.h>

#include <QRegularExpression>
#include <QTextCursor>

namespace Subversion::Internal {

QString subversionChangeUnderCursor(const QTextCursor &c)
{
    static const QRegularExpression changeNumberPattern("^\\s*(?<area>(?<rev>\\d+))\\s+.*$");
    static const QRegularExpression revisionNumberPattern(
        "\\b(?<area>(r|[rR]evision )(?<rev>\\d+))\\b");
    QTC_ASSERT(changeNumberPattern.isValid(), return {});
    QTC_ASSERT(revisionNumberPattern.isValid(), return {});

    QTextCursor cursor = c;
    // Any number is regarded as change number.
    cursor.select(QTextCursor::LineUnderCursor);
    if (!cursor.hasSelection())
        return {};
    const QString change = cursor.selectedText();
    const int pos = c.position() - cursor.selectionStart() + 1;
    // Annotation output has number, log output has revision numbers,
    // both at the start of the line.
    auto matchIter = changeNumberPattern.globalMatch(change);
    if (!matchIter.hasNext())
        matchIter = revisionNumberPattern.globalMatch(change);
    // We may have several matches of our regexp and we way have
    // several () in the regexp
    const QString areaName = "area";
    while (matchIter.hasNext()) {
        auto match = matchIter.next();
        const QString rev = match.captured("rev");
        if (rev.isEmpty())
            continue;
        const QString area = match.captured(areaName);
        QTC_ASSERT(area.contains(rev), continue);
        const int start = match.capturedStart(areaName);
        const int end = match.capturedEnd(areaName);
        if (pos > start && pos <= end)
            return rev;
    }
    return {};
}

static QStringList subversionAnnotationPreviousVersions(VcsBase::VcsEditorDocument *,
                                                        const QString &v)
{
    bool ok;
    const int revision = v.toInt(&ok);
    if (!ok || revision < 2)
        return {};
    return {QString::number(revision - 1)};
}

VcsBase::VcsBaseEditorParameters subversionEditorParameters(
    VcsBase::EditorContentType type, Utils::Id id, const QString &displayName,
    const QString &mimeType,
    const std::function<void(const Utils::FilePath &, const QString &)> &describe)
{
    VcsBase::VcsBaseEditorParameters parameters{type, id, displayName, mimeType, {}, describe,
                                                subversionChangeUnderCursor};
    parameters.annotationPreviousVersions = subversionAnnotationPreviousVersions;
    parameters.annotationHighlighterCreator
        = VcsBase::getAnnotationHighlighterCreator<SubversionAnnotationHighlighter>();
    /* Diff pattern:
    \code
        Index: main.cpp
    ===================================================================
    --- main.cpp<tab>(revision 2)
    +++ main.cpp<tab>(working copy)
    @@ -6,6 +6,5 @@
    \endcode
    */
    parameters.diffFilePattern = "^[-+]{3} ([^\\t]+)|^Index: .*|^=+$";
    parameters.logEntryPattern = "^(r\\d+) \\|";
    parameters.annotateRevisionTextFormat = Tr::tr("Annotate revision \"%1\"");
    parameters.annotationEntryPattern = "^(\\d+):";
    return parameters;
}

} // namespace Subversion::Internal
