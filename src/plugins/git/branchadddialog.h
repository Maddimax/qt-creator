// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QDialog>

#include <memory>
#include <QItemDelegate>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QDialogButtonBox;
class QLineEdit;
QT_END_NAMESPACE

namespace Git::Internal {

// A reference name with everything git refuses replaced by an underscore. The
// widget validator did this to the text as it was typed, so the reader saw a
// space turn into "_" - that is kept, and is why this is separate from the
// judgement below.
QString sanitisedReferenceName(const QString &name);

// What is wrong with \a name as a new reference, given the \a existing ones,
// or nothing when there is nothing to say. Empty gets no complaint: nothing
// has been typed yet.
QString referenceNameIssue(const QString &name, const QStringList &existing);

// Whether it can be used. Not the same as having no complaint - an empty name
// has none and is still not a name.
bool isAcceptableReferenceName(const QString &name, const QStringList &existing);

class BranchAddSettings;
class BranchModel;

class BranchValidationDelegate : public QItemDelegate
{
public:
    BranchValidationDelegate(QWidget *parent, BranchModel *model);
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;

private:
    BranchModel *m_model = nullptr;
};

class BranchAddDialog : public QDialog
{
public:
    enum Type {
        AddBranch,
        RenameBranch,
        AddTag,
        RenameTag
    };

    BranchAddDialog(const QStringList &localBranches, Type type, QWidget *parent);
    ~BranchAddDialog() override;

    void setBranchName(const QString &);
    QString branchName() const;

    QString annotation() const;

    void setTrackedBranchName(const QString &name, bool remote);

    bool track() const;

    void setCheckoutVisible(bool visible);
    bool checkout() const;

private:
    const std::unique_ptr<BranchAddSettings> m_settings;
    QDialogButtonBox * const m_buttonBox;
};

#ifdef WITH_TESTS
QObject *createBranchAddDialogTest();
#endif

} // Git::Internal
