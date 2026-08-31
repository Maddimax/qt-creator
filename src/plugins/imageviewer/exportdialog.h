// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QDialog>

#include <memory>

namespace Utils { class FilePath; }

namespace ImageViewer::Internal {

struct ExportData;
class ExportSettings;

#ifdef WITH_TESTS
QObject *createExportDialogTest();
#endif

class ExportDialog : public QDialog
{
public:
    explicit ExportDialog(QWidget *parent = nullptr);
    ~ExportDialog() override;

    QSize exportSize() const;
    void setExportSize(const QSize &);

    Utils::FilePath exportFileName() const;
    void setExportFileName(const Utils::FilePath &);

    ExportData exportData() const;

    void accept() override;

    static QString imageNameFilterString();

private:
    const std::unique_ptr<ExportSettings> m_settings;
};

} // ImageViewer::Internal
