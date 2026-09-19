// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "vcsbase_global.h"
#include "baseannotationhighlighter.h"

#include <texteditor/textdocument.h>

#include <utils/filepath.h>
#include <utils/id.h>

#include <QAbstractListModel>
#include <QRegularExpression>
#include <QSet>

#include <functional>

QT_BEGIN_NAMESPACE
class QTextBlock;
class QWidget;
QT_END_NAMESPACE

namespace TextEditor { class ToolBarChoice; }

namespace VcsBase {

class VcsBaseEditorConfig;

namespace Internal { class VcsEditorDocumentPrivate; }

enum EditorContentType
{
    LogOutput,
    AnnotateOutput,
    DiffOutput,
    OtherContent
};

class VCSBASE_EXPORT VcsBaseEditorParameters
{
public:
    EditorContentType type;
    Utils::Id id;
    QString displayName;
    QString mimeType;
    std::function<QWidget *()> editorWidgetCreator;
    std::function<void (const Utils::FilePath &, const QString &)> describeFunc;
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

    // Where the gutter starts counting: an annotation of part of a file
    // numbers its lines as the file does.
    int firstLineNumber() const;
    void setFirstLineNumber(int firstLineNumber);
    // Where to go once the output has arrived.
    int defaultLineNumber() const;
    void setDefaultLineNumber(int line);

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

    // The sections, found from the text with the patterns above whenever the
    // text changes. Two things the document cannot know come from whoever
    // does: the file on disk a diff header stands for, and a log entry's
    // subject. Until they are given a diff has no sections and a log's
    // entries have no subject.
    using BlockToString = std::function<QString(const QTextBlock &)>;
    void setSectionHooks(const BlockToString &fileNameForDiffHeader,
                         const BlockToString &revisionSubject);
    VcsEditorSections *sections() const;
    void updateSections();

    // The sections as what the tool bar offers: the entry the caret is in,
    // and a jump to any other. A log or a diff has one; the others none.
    TextEditor::ToolBarChoice *toolBarChoice() const override;

private:
    Internal::VcsEditorDocumentPrivate *const d;
};

#ifdef WITH_TESTS
QObject *createVcsEditorDocumentTest();
#endif

} // namespace VcsBase
