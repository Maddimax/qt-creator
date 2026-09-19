// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <vcsbase/vcsbaseeditor.h>

#include <QRegularExpression>

QT_FORWARD_DECLARE_CLASS(QKeyEvent)

namespace Utils {
class FancyLineEdit;
class FilePath;
} // Utils

namespace Git::Internal {

class GitLogFilterWidget;

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
// two, with the widget and the describe function the caller supplies.
VcsBase::VcsBaseEditorParameters gitEditorParameters(
    VcsBase::EditorContentType type, Utils::Id id, const QString &displayName,
    const QString &mimeType, const std::function<QWidget *()> &editorWidgetCreator,
    const std::function<void(const Utils::FilePath &, const QString &)> &describe);

class GitEditorWidget : public VcsBase::VcsBaseEditorWidget
{
    Q_OBJECT

public:
    GitEditorWidget();

    void setPlainText(const QString &text) override;
    QWidget *addFilterWidget();
    void setPickaxeLineEdit(Utils::FancyLineEdit *lineEdit);
    QString grepValue() const;
    QString pickaxeValue() const;
    QString authorValue() const;
    bool caseSensitive() const;
    void refresh();

    void restoreState(const QByteArray &state) override;

signals:
    void toggleFilters(bool value);

private:
    void applyDiffChunk(const VcsBase::DiffChunk& chunk, Core::PatchAction patchAction);

    void init() override;
    void keyPressEvent(QKeyEvent *e) override;
    bool replaceRebaseAction(QKeyEvent *e);
    void addDiffActions(QMenu *menu, const VcsBase::DiffChunk &chunk) override;
    void aboutToOpen(const Utils::FilePath &filePath, const Utils::FilePath &realFilePath) override;
    QString changeUnderCursor(const QTextCursor &) const override;
    int originalLineUnderCursor(const QTextCursor &) const override;
    QString decorateVersion(const QString &revision) const override;
    QStringList annotationPreviousVersions(const QString &revision) const override;
    bool isValidRevision(const QString &revision) const override;
    void addChangeActions(QMenu *menu, const QString &change, int line = 0) override;
    QString revisionSubject(const QTextBlock &inBlock) const override;
    bool supportChangeLinks() const override;
    Utils::FilePath fileNameForLine(int line) const override;
    void jumpToDiffTarget(const Utils::FilePath &filePath,
                          int lineNumber,
                          const QTextBlock &contextBlock) override;
    Utils::FilePath sourceWorkingDirectory() const;

    GitLogFilterWidget *m_logFilterWidget = nullptr;
    QVector<int> m_originalLines;
};

} // Git::Internal
