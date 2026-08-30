// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/filepath.h>
#include <utils/icon.h>
#include <utils/itemviews.h>

#include <QDialog>
#include <QIcon>

QT_BEGIN_NAMESPACE
class QDialogButtonBox;
class QComboBox;
class QLabel;
class QStandardItemModel;
class QStandardItem;
QT_END_NAMESPACE

namespace Git::Internal {

class LogChangeModel;

// How the rows are marked relative to the one the reader is on. Every dialog
// that shows a log marks them, and each marks them differently - which used to
// be three item delegates painting over the view. The model answers it now, so
// a Qt Quick table shows the same marks without a delegate of its own.
enum class LogRowMarks
{
    None,
    // Reset: the commits above the chosen one are the ones being discarded.
    StrikeOutBeforeCurrent,
    // Interactive rebase: the chosen commit and everything after it is replayed.
    IconUpToCurrent,
    // Format-patch: whatever is picked is what gets exported.
    IconOnSelected
};

// Whether \a row is struck through, given the row the reader is on.
bool logRowIsStruckOut(LogRowMarks marks, int row, int currentRow);
// Whether \a row carries the mark icon.
bool logRowHasIcon(LogRowMarks marks, int row, int currentRow, bool selected);

#ifdef WITH_TESTS
QObject *createLogChangeMarksTest();
#endif

// A widget that lists hash and subject of the changes
// Used for reset and interactive rebase

class LogChangeWidget : public Utils::TreeView
{
    Q_OBJECT

public:
    enum LogFlag
    {
        None = 0x00,
        IncludeRemotes = 0x01,
        Silent = 0x02,
        OmitMerges = 0x04,
    };

    Q_DECLARE_FLAGS(LogFlags, LogFlag)

    explicit LogChangeWidget(QWidget *parent = nullptr);
    bool init(const Utils::FilePath &repository, const QString &commit = {}, LogFlags flags = None);
    QString commit() const;
    int commitIndex() const;
    QStringList commitList() const;
    QStringList patchRange() const;
    bool isRowSelected(int row) const;
    QString earliestCommit() const;
    void setMarks(LogRowMarks marks);
    void setExcludedRemote(const QString &remote) { m_excludedRemote = remote; }

signals:
    void commitActivated(const QString &commit);
    void hasSelectionChanged(bool hasSelection);

private:
    void emitCommitActivated(const QModelIndex &index);

    void selectionChanged(const QItemSelection &selected, const QItemSelection &deselected) override;
    bool populateLog(const Utils::FilePath &repository, const QString &commit, LogFlags flags);
    const QStandardItem *currentItem(int column = 0) const;

    LogChangeModel *m_model;
    LogRowMarks m_marks = LogRowMarks::None;
    QString m_excludedRemote;
};

class LogChangeDialog : public QDialog
{
public:
    enum DialogType {
        Reset,
        Select
    };
    LogChangeDialog(DialogType type, QWidget *parent);

    void setSelectionMode(QAbstractItemView::SelectionMode mode);

    bool runDialog(const Utils::FilePath &repository, const QString &commit = QString(),
                   LogChangeWidget::LogFlags flags = LogChangeWidget::None);

    QString commit() const;
    int commitIndex() const;
    QStringList commitList() const;
    QStringList patchRange() const;
    QString resetFlag() const;
    LogChangeWidget *widget() const;

private:
    LogChangeWidget *m_widget = nullptr;
    QLabel *m_selectionHintLabel = nullptr;
    QDialogButtonBox *m_dialogButtonBox = nullptr;
    QComboBox *m_resetTypeComboBox = nullptr;
};



} // Git::Internal
