// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "scxmldocument.h"
#include "scxmleditortr.h"
#include "statistics.h"
#include "statisticsdialog.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <QVBoxLayout>

#include <QDialogButtonBox>

using namespace ScxmlEditor::Common;

StatisticsDialog::StatisticsDialog(QWidget *parent)
    : QDialog(parent)
{
    resize(400, 300);
    setWindowTitle(Tr::tr("Document Statistics"));

    m_statistics.reset(new Statistics);
    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_statistics.get()));
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &StatisticsDialog::accept);
}

StatisticsDialog::~StatisticsDialog() = default;

void StatisticsDialog::setDocument(ScxmlEditor::PluginInterface::ScxmlDocument *doc)
{
    m_statistics->setDocument(doc);
}
