// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/result.h>

#include <QDialog>

#include <memory>

QT_BEGIN_NAMESPACE
class QDialogButtonBox;
class QObject;
class QPushButton;
class QStandardItemModel;
QT_END_NAMESPACE

namespace Utils { class FilePath; }

namespace VcsBase::Internal {

class NickNameSettings;
#ifdef WITH_TESTS
class NickNameDialogTest;
QObject *createNickNameDialogTest();
#endif

class NickNameDialog : public QDialog
{
public:
    explicit NickNameDialog(QStandardItemModel *model, QWidget *parent = nullptr);
    ~NickNameDialog() override;

    QString nickName() const;

    // Utilities to initialize/populate the model
    static QStandardItemModel *createModel(QObject *parent);
    static Utils::Result<> populateModelFromMailCapFile(const Utils::FilePath &file,
                                                        QStandardItemModel *model);

    // Return a list for a completer on the field line edits
    static QStringList nickNameList(const QStandardItemModel *model);

private:
    QPushButton *okButton() const;

    QStandardItemModel *m_model;
    const std::unique_ptr<NickNameSettings> m_settings;
    QDialogButtonBox *m_buttonBox;

#ifdef WITH_TESTS
    friend class NickNameDialogTest;
#endif
};

} // VcsBase::Internal
