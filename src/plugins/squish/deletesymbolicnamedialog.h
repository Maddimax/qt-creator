// Copyright (C) 2022 The Qt Company Ltd
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QDialog>

#include <memory>

QT_BEGIN_NAMESPACE
class QDialogButtonBox;
QT_END_NAMESPACE

namespace Squish::Internal {

class DeleteSymbolicNameSettings;

#ifdef WITH_TESTS
QObject *createDeleteSymbolicNameDialogTest();
#endif

class DeleteSymbolicNameDialog : public QDialog
{
    Q_OBJECT
public:
    enum Result { ResetReference, InvalidateNames, RemoveNames };

    explicit DeleteSymbolicNameDialog(const QString &symbolicName,
                                      const QStringList &names,
                                      QWidget *parent = nullptr);
    ~DeleteSymbolicNameDialog() override;

    QString selectedSymbolicName() const;
    Result result() const;

private:
    const std::unique_ptr<DeleteSymbolicNameSettings> d;
    QDialogButtonBox *m_buttonBox;
};

} // namespace Squish::Internal
