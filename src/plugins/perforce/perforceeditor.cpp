// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "perforceeditor.h"

#include "annotationhighlighter.h"
#include "perforceplugin.h"
#include "perforcetr.h"

#include <utils/qtcassert.h>

#include <vcsbase/vcsbaseeditor.h>

#include <QRegularExpression>
#include <QTextCursor>

namespace Perforce::Internal {

QString perforceChangeUnderCursor(const QTextCursor &c)
{
    static const QRegularExpression changeNumberPattern("^\\d+$");
    QTC_CHECK(changeNumberPattern.isValid());
    QTextCursor cursor = c;
    // Any number is regarded as change number.
    cursor.select(QTextCursor::WordUnderCursor);
    if (!cursor.hasSelection())
        return {};
    const QString change = cursor.selectedText();
    return changeNumberPattern.match(change).hasMatch() ? change : QString();
}

static QString perforceFindDiffFile(VcsBase::VcsEditorDocument *, const QString &f)
{
    return fileNameFromPerforceName(f.trimmed(), false);
}

static QStringList perforceAnnotationPreviousVersions(VcsBase::VcsEditorDocument *,
                                                      const QString &v)
{
    bool ok;
    const int changeList = v.toInt(&ok);
    if (!ok || changeList < 2)
        return {};
    return QStringList(QString::number(changeList - 1));
}

VcsBase::VcsBaseEditorParameters perforceEditorParameters(
    VcsBase::EditorContentType type, Utils::Id id, const QString &displayName,
    const QString &mimeType,
    const std::function<void(const Utils::FilePath &, const QString &)> &describe)
{
    VcsBase::VcsBaseEditorParameters parameters{type, id, displayName, mimeType, {}, describe,
                                                perforceChangeUnderCursor};
    parameters.annotationPreviousVersions = perforceAnnotationPreviousVersions;
    parameters.findDiffFile = perforceFindDiffFile;
    parameters.annotationHighlighterCreator
        = VcsBase::getAnnotationHighlighterCreator<PerforceAnnotationHighlighter>();
    // Diff format:
    // 1) "==== //depot/.../mainwindow.cpp#2 - /depot/.../mainwindow.cpp ====" (created by p4 diff)
    // 2) "==== //depot/.../mainwindow.cpp#15 (text) ====" (created by p4 describe)
    // 3) --- //depot/XXX/closingkit/trunk/source/cui/src/cui_core.cpp<tab>2012-02-08 13:54:01.000000000 0100
    //    +++ P:/XXX\closingkit\trunk\source\cui\src\cui_core.cpp<tab>2012-02-08 13:54:01.000000000 0100
    parameters.diffFilePattern = "^(?:={4}|\\+{3}) (.+)(?:\\t|#\\d)";
    parameters.logEntryPattern = "^... #\\d change (\\d+) ";
    parameters.annotateRevisionTextFormat = Tr::tr("Annotate change list \"%1\"");
    parameters.annotationEntryPattern = "^(\\d+):";
    return parameters;
}

} // Perforce::Internal
