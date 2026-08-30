// Copyright (C) 2016 Orgad Shaneh <orgads@gmail.com>.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "branchcombobox.h"
#include "gerritpushdialog.h"
#include "../gitclient.h"

using namespace Git::Internal;
using namespace Gerrit::Internal;
using namespace Utils;

BranchComboBox::BranchComboBox(QWidget *parent) : QComboBox(parent)
{ }

void BranchComboBox::init(const FilePath &repository)
{
    m_repository = repository;
    QString currentBranch = gitClient().synchronousCurrentLocalBranch(repository);
    if (currentBranch.isEmpty()) {
        m_detached = true;
        currentBranch = "HEAD";
    }
    QString output;
    if (!gitClient().synchronousForEachRefCmd(
                m_repository, {"--format=%(refname)", "refs/heads/"}, &output)) {
        return;
    }
    for (const QString &branch : localBranchChoices(output, m_detached ? QString() : currentBranch))
        addItem(branch);
    if (currentBranch.isEmpty())
        return;
    int index = findText(currentBranch);
    if (index != -1)
        setCurrentIndex(index);
}
