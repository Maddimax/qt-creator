// Copyright (C) 2020 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "uvtargetdriverselection.h"

#include <utils/detailswidget.h>
#include <utils/filepath.h>
#include <utils/widgets.h>

#include <QDialog>

QT_BEGIN_NAMESPACE
class QLineEdit;
QT_END_NAMESPACE

namespace BareMetal::Internal::Uv {

class DriverSelectionModel;
class DriverSelectionView;
class DriverSelectionCpuDllView;




// DriverSelectionDialog

class DriverSelectionDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit DriverSelectionDialog(const Utils::FilePath &toolsIniFile,
                                   const QStringList &supportedDrivers,
                                   QWidget *parent = nullptr);
    DriverSelection selection() const;

private:
    void setSelection(const DriverSelection &selection);

    DriverSelection m_selection;
    DriverSelectionModel *m_model = nullptr;
    DriverSelectionView *m_view = nullptr;
};

} // BareMetal::Internal::Uv
