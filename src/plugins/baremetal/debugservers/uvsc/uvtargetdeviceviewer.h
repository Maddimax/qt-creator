// Copyright (C) 2020 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "uvtargetdeviceselection.h"

#include <utils/detailswidget.h>
#include <utils/filepath.h>
#include <utils/widgets.h>

#include <QDialog>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QPlainTextEdit;
QT_END_NAMESPACE

namespace Utils { class PathChooser; }

namespace BareMetal::Internal::Uv {

class DeviceSelectionModel;
class DeviceSelectionView;
class DeviceSelectionMemoryView;
class DeviceSelectionAlgorithmView;




// DeviceSelectionDialog

class DeviceSelectionDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit DeviceSelectionDialog(const Utils::FilePath &toolsIniFile, QWidget *parent = nullptr);
    DeviceSelection selection() const;

private:
    void setSelection(const DeviceSelection &selection);

    DeviceSelection m_selection;
    DeviceSelectionModel *m_model = nullptr;
    DeviceSelectionView *m_view = nullptr;
};

} // BareMetal::Internal::Uv
