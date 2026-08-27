// Copyright (C) 2020 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "uvtargetdeviceviewer.h"

#include "uvproject.h" // for buildPackageId()
#include "uvtargetdevicemodel.h"

#include <baremetal/baremetaltr.h>

#include <utils/pathchooser.h>

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace BareMetal::Internal::Uv {

// DeviceSelectionDialog

DeviceSelectionDialog::DeviceSelectionDialog(const Utils::FilePath &toolsIniFile, QWidget *parent)
    : QDialog(parent), m_model(new DeviceSelectionModel(this)), m_view(new DeviceSelectionView(this))
{
    setWindowTitle(Tr::tr("Available Target Devices"));

    const auto layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);
    const auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttonBox);
    setLayout(layout);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &DeviceSelectionDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &DeviceSelectionDialog::reject);

    connect(m_view, &DeviceSelectionView::deviceSelected, this,
            [this](const DeviceSelection &selection) {
        m_selection = selection;
    });

    m_model->fillAllPacks(toolsIniFile);
    m_view->setModel(m_model);
}

void DeviceSelectionDialog::setSelection(const DeviceSelection &selection)
{
    m_selection = selection;
}

DeviceSelection DeviceSelectionDialog::selection() const
{
    return m_selection;
}

} // BareMetal::Internal::Uv
