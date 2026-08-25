// Copyright (C) 2020 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "uvtargetdrivermodel.h"
#include "uvtargetdriverviewer.h"

#include <baremetal/baremetaltr.h>

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace BareMetal::Internal::Uv {

// DriverSelectionDialog

DriverSelectionDialog::DriverSelectionDialog(const Utils::FilePath &toolsIniFile,
                                             const QStringList &supportedDrivers,
                                             QWidget *parent)
    : QDialog(parent), m_model(new DriverSelectionModel(this)),
      m_view(new DriverSelectionView(this))
{
    setWindowTitle(Tr::tr("Available Target Drivers"));

    const auto layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);
    const auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttonBox);
    setLayout(layout);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &DriverSelectionDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &DriverSelectionDialog::reject);

    connect(m_view, &DriverSelectionView::driverSelected, this,
            [this](const DriverSelection &selection) {
        m_selection = selection;
    });

    m_model->fillDrivers(toolsIniFile, supportedDrivers);
    m_view->setModel(m_model);
}

void DriverSelectionDialog::setSelection(const DriverSelection &selection)
{
    m_selection = selection;
}

DriverSelection DriverSelectionDialog::selection() const
{
    return m_selection;
}

} // BareMetal::Internal::Uv
