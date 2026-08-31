// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/filepath.h>

#include <QList>
#include <QDialog>
#include <QPointer>

#include <memory>

QT_BEGIN_NAMESPACE
class QDialogButtonBox;
class QPushButton;
QT_END_NAMESPACE

namespace Core {

class IDocument;

namespace Internal {

class SaveItemsSettings;

#ifdef WITH_TESTS
QObject *createSaveItemsDialogTest();
#endif

class SaveItemsDialog : public QDialog
{
    Q_OBJECT

public:
    SaveItemsDialog(QWidget *parent, const QList<IDocument *> &items);
    ~SaveItemsDialog() override;

    void setMessage(const QString &msg);
    void setAlwaysSaveMessage(const QString &msg);
    bool alwaysSaveChecked();
    QList<IDocument *> itemsToSave() const;
    Utils::FilePaths filesToDiff() const;

private:
    void collectItemsToSave();
    void collectFilesToDiff();
    void discardAll();
    void updateButtons();
    void adjustButtonWidths();

    const std::unique_ptr<SaveItemsSettings> d;
    QDialogButtonBox *m_buttonBox;
    QList<IDocument *> m_itemsToSave;
    Utils::FilePaths m_filesToDiff;
    QPushButton *m_diffButton = nullptr;
};

} // namespace Internal
} // namespace Core
