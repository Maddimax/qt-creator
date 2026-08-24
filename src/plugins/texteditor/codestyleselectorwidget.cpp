// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "codestyleselectorwidget.h"

#include "codestyleeditor.h"
#include "icodestylepreferences.h"
#include "icodestylepreferencesfactory.h"
#include "codestylepool.h"
#include "texteditortr.h"

#include <utils/infolabel.h>
#include <utils/layoutbuilder.h>

#include <QComboBox>
#include <QPushButton>

using namespace Utils;

namespace TextEditor {

CodeStyleSelectorWidget::CodeStyleSelectorWidget(const FilePath &projectFile, QWidget *parent)
    : QWidget(parent), m_projectFile(projectFile)
{
    m_delegateComboBox = new QComboBox(this);
    m_delegateComboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto copyButton = new QPushButton(Tr::tr("Copy..."));

    m_removeButton = new QPushButton(Tr::tr("Remove"));

    m_exportButton = new QPushButton(Tr::tr("Export..."));
    m_exportButton->setEnabled(false);

    m_importButton = new QPushButton(Tr::tr("Import..."));
    m_importButton->setEnabled(false);

    m_readonlyLabel = new InfoLabel(
        Tr::tr("The selected configuration is read-only. Copy the configuration for editing."),
        Utils::InfoLabelType::Warning);
    m_readonlyLabel->setVisible(false);

    using namespace Layouting;

    Column {
        Grid {
            Tr::tr("Custom settings:"),
            m_delegateComboBox,
            copyButton,
            m_removeButton,
            m_exportButton,
            m_importButton
        },
        m_readonlyLabel,
        noMargin,
    }.attachTo(this);

    connect(m_delegateComboBox, &QComboBox::activated,
            this, &CodeStyleSelectorWidget::slotComboBoxActivated);
    connect(copyButton, &QAbstractButton::clicked,
            this, &CodeStyleSelectorWidget::slotCopyClicked);
    connect(m_removeButton, &QAbstractButton::clicked,
            this, &CodeStyleSelectorWidget::slotRemoveClicked);
    connect(m_importButton, &QAbstractButton::clicked,
            this, &CodeStyleSelectorWidget::slotImportClicked);
    connect(m_exportButton, &QAbstractButton::clicked,
            this, &CodeStyleSelectorWidget::slotExportClicked);
}

CodeStyleSelectorWidget::~CodeStyleSelectorWidget() = default;

void CodeStyleSelectorWidget::setCodeStyle(ICodeStylePreferences *codeStyle)
{
    if (m_codeStyle == codeStyle)
        return; // nothing changes

    // cleanup old
    if (m_codeStyle) {
        CodeStylePool *codeStylePool = m_codeStyle->delegatingPool();
        if (codeStylePool) {
            disconnect(codeStylePool, &CodeStylePool::codeStyleAdded,
                    this, &CodeStyleSelectorWidget::slotCodeStyleAdded);
            disconnect(codeStylePool, &CodeStylePool::codeStyleRemoved,
                    this, &CodeStyleSelectorWidget::slotCodeStyleRemoved);
        }
        disconnect(m_codeStyle, &ICodeStylePreferences::currentDelegateChanged,
                this, &CodeStyleSelectorWidget::slotCurrentDelegateChanged);

        m_exportButton->setEnabled(false);
        m_importButton->setEnabled(false);
        m_delegateComboBox->clear();
    }
    m_codeStyle = codeStyle;
    // fillup new
    if (m_codeStyle) {
        QList<ICodeStylePreferences *> delegates;
        CodeStylePool *codeStylePool = m_codeStyle->delegatingPool();
        if (codeStylePool) {
            delegates = codeStylePool->codeStyles();

            connect(codeStylePool, &CodeStylePool::codeStyleAdded,
                    this, &CodeStyleSelectorWidget::slotCodeStyleAdded);
            connect(codeStylePool, &CodeStylePool::codeStyleRemoved,
                    this, &CodeStyleSelectorWidget::slotCodeStyleRemoved);
            m_exportButton->setEnabled(true);
            m_importButton->setEnabled(true);
        }

        for (int i = 0; i < delegates.count(); i++)
            slotCodeStyleAdded(delegates.at(i));

        slotCurrentDelegateChanged(m_codeStyle->currentDelegate());

        connect(m_codeStyle, &ICodeStylePreferences::currentDelegateChanged,
                this, &CodeStyleSelectorWidget::slotCurrentDelegateChanged);
    }
}

void CodeStyleSelectorWidget::slotComboBoxActivated(int index)
{
    if (m_ignoreChanges.isLocked())
        return;

    if (index < 0 || index >= m_delegateComboBox->count())
        return;
    auto delegate = m_delegateComboBox->itemData(index).value<ICodeStylePreferences *>();

    QSignalBlocker blocker(this);
    m_codeStyle->setCurrentDelegate(delegate);
}

void CodeStyleSelectorWidget::slotCurrentDelegateChanged(ICodeStylePreferences *delegate)
{
    {
        const GuardLocker locker(m_ignoreChanges);
        m_delegateComboBox->setCurrentIndex(m_delegateComboBox->findData(QVariant::fromValue(delegate)));
        m_delegateComboBox->setToolTip(m_delegateComboBox->currentText());
    }

    const bool removeEnabled = delegate && !delegate->isReadOnly() && !delegate->currentDelegate();
    m_removeButton->setEnabled(removeEnabled);
    m_readonlyLabel->setVisible(delegate && delegate->isReadOnly());
}

void CodeStyleSelectorWidget::slotCopyClicked()
{
    if (m_codeStyle)
        CodeStyleActions::copy(m_codeStyle, this);
}

void CodeStyleSelectorWidget::slotRemoveClicked()
{
    if (m_codeStyle)
        CodeStyleActions::remove(m_codeStyle, this);
}

void CodeStyleSelectorWidget::slotImportClicked()
{
    if (m_codeStyle)
        CodeStyleActions::importFrom(m_codeStyle, this);
}

void CodeStyleSelectorWidget::slotExportClicked()
{
    if (m_codeStyle)
        CodeStyleActions::exportTo(m_codeStyle, this);
}

void CodeStyleSelectorWidget::slotCodeStyleAdded(ICodeStylePreferences *codeStylePreferences)
{
    if (codeStylePreferences == m_codeStyle
            || codeStylePreferences->id() == m_codeStyle->id())
        return;

    if (!codeStylePreferences->project().isEmpty()
        && codeStylePreferences->project() != m_projectFile) {
        return;
    }

    const QVariant data = QVariant::fromValue(codeStylePreferences);
    const QString name = codeStyleDisplayName(codeStylePreferences);
    m_delegateComboBox->addItem(name, data);
    m_delegateComboBox->setItemData(m_delegateComboBox->count() - 1, name, Qt::ToolTipRole);
    connect(codeStylePreferences, &ICodeStylePreferences::displayNameChanged,
            this, [this, codeStylePreferences] { slotUpdateName(codeStylePreferences); });
    if (codeStylePreferences->delegatingPool()) {
        connect(codeStylePreferences, &ICodeStylePreferences::currentPreferencesChanged,
                this, [this, codeStylePreferences] { slotUpdateName(codeStylePreferences); });
    }
}

void CodeStyleSelectorWidget::slotCodeStyleRemoved(ICodeStylePreferences *codeStylePreferences)
{
    const GuardLocker locker(m_ignoreChanges);
    m_delegateComboBox->removeItem(m_delegateComboBox->findData(
                                           QVariant::fromValue(codeStylePreferences)));
    disconnect(codeStylePreferences, &ICodeStylePreferences::displayNameChanged, this, nullptr);
    if (codeStylePreferences->delegatingPool()) {
        disconnect(codeStylePreferences, &ICodeStylePreferences::currentPreferencesChanged,
                   this, nullptr);
    }
}

void CodeStyleSelectorWidget::slotUpdateName(ICodeStylePreferences *codeStylePreferences)
{
    updateName(codeStylePreferences);

    QList<ICodeStylePreferences *> codeStyles = m_codeStyle->delegatingPool()->codeStyles();
    for (int i = 0; i < codeStyles.count(); i++) {
        ICodeStylePreferences *codeStyle = codeStyles.at(i);
        if (codeStyle->currentDelegate() == codeStylePreferences)
            updateName(codeStyle);
    }

    m_delegateComboBox->setToolTip(m_delegateComboBox->currentText());
}

void CodeStyleSelectorWidget::updateName(ICodeStylePreferences *codeStyle)
{
    const int idx = m_delegateComboBox->findData(QVariant::fromValue(codeStyle));
    if (idx < 0)
        return;

    const QString name = codeStyleDisplayName(codeStyle);
    m_delegateComboBox->setItemText(idx, name);
    m_delegateComboBox->setItemData(idx, name, Qt::ToolTipRole);
}

} // TextEditor
