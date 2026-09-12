// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "console.h"

#include "consoleitemmodel.h"
#include "consoleproxymodel.h"
#include "consoleview.h"
#include "../debuggertr.h"

#include <coreplugin/findplaceholder.h>
#include <coreplugin/icore.h>

#include <utils/stylehelper.h>
#include <utils/stylehelperpainting.h>
#include <utils/utilsicons.h>

#include <QAction>
#include <QToolButton>
#include <QLabel>
#include <QVBoxLayout>

const char CONSOLE[] = "Console";
const char SHOW_LOG[] = "showLog";
const char SHOW_WARNING[] = "showWarning";
const char SHOW_ERROR[] = "showError";

using namespace Utils;

namespace Debugger::Internal {

/////////////////////////////////////////////////////////////////////
//
// Console
//
/////////////////////////////////////////////////////////////////////

Console::Console()
{
    setId("QMLDebuggerConsole");
    setDisplayName(Tr::tr("QML Debugger Console"));
    setPriorityInStatusBar(-40);

    m_consoleItemModel = new ConsoleItemModel(this);

    m_consoleWidget = new QWidget;
    m_consoleWidget->setWindowTitle(displayName());
    m_consoleWidget->setEnabled(true);

    auto vbox = new QVBoxLayout(m_consoleWidget);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);

    auto proxyModel = new ConsoleProxyModel(this);
    proxyModel->setSourceModel(m_consoleItemModel);
    connect(m_consoleItemModel,
            &ConsoleItemModel::selectEditableRow,
            proxyModel,
            &ConsoleProxyModel::selectEditableRow);

    //Scroll to bottom when rows matching current filter settings are inserted
    //Not connecting rowsRemoved as the only way to remove rows is to clear the
    //model which will automatically reset the view.
    connect(m_consoleItemModel, &QAbstractItemModel::rowsInserted,
            proxyModel, &ConsoleProxyModel::onRowsInserted);

    m_consoleView = new ConsoleView(m_consoleItemModel, proxyModel, m_consoleWidget);

    connect(proxyModel, &ConsoleProxyModel::setCurrentIndex,
            m_consoleView, &ConsoleView::setCurrentIndex);
    connect(proxyModel, &ConsoleProxyModel::scrollToBottom,
            m_consoleView, &ConsoleView::onScrollToBottom);

    vbox->addWidget(m_consoleView);
    vbox->addWidget(new Core::FindToolBarPlaceHolder(m_consoleWidget));

    m_showDebug.setDefaultValue(true);
    m_showDebug.setSettingsKey(CONSOLE, SHOW_LOG);
    m_showDebug.setLabelText(Tr::tr("Show debug, log, and info messages."));
    m_showDebug.setToolTip(Tr::tr("Show debug, log, and info messages."));
    m_showDebug.setValue(true);
    m_showDebug.setIcon(Icons::INFO_TOOLBAR.icon());
    connect(&m_showDebug, &BoolAspect::changed,
            proxyModel, [this, proxyModel] { proxyModel->setShowLogs(m_showDebug()); });

    m_showWarning.setDefaultValue(true);
    m_showWarning.setSettingsKey(CONSOLE, SHOW_WARNING);
    m_showWarning.setLabelText(Tr::tr("Show warning messages."));
    m_showWarning.setToolTip(Tr::tr("Show warning messages."));
    m_showWarning.setValue(true);
    m_showWarning.setIcon(Icons::WARNING_TOOLBAR.icon());
    connect(&m_showWarning, &BoolAspect::changed,
            proxyModel, [this, proxyModel] { proxyModel->setShowWarnings(m_showWarning()); });

    m_showError.setDefaultValue(true);
    m_showError.setSettingsKey(CONSOLE, SHOW_ERROR);
    m_showError.setLabelText(Tr::tr("Show error messages."));
    m_showError.setToolTip(Tr::tr("Show error messages."));
    m_showError.setValue(true);
    m_showError.setIcon(Icons::CRITICAL_TOOLBAR.icon());
    connect(&m_showError, &BoolAspect::changed,
            proxyModel, [this, proxyModel] { proxyModel->setShowErrors(m_showError()); });

    m_spacer = new QWidget(m_consoleWidget);
    m_spacer->setMinimumWidth(30);

    m_statusLabel = new QLabel(m_consoleWidget);
    StyleHelper::setPanelWidget(m_statusLabel);

    readSettings();
    connect(Core::ICore::instance(), &Core::ICore::saveSettingsRequested,
            this, &Console::writeSettings);
}

Console::~Console()
{
    writeSettings();
    delete m_consoleWidget;
}

QWidget *Console::outputWidget(QWidget *)
{
    return m_consoleWidget;
}

QList<Core::IOutputPane::ToolBarItem> Console::toolBarItems() const
{
    // The order this pane had before its toggles stopped being QToolButtons:
    // the three of them, then the gap, then what it has to report.
    QList<ToolBarItem> items{ToolBarItem::forAspect(const_cast<Utils::BoolAspect *>(&m_showDebug)),
                             ToolBarItem::forAspect(const_cast<Utils::BoolAspect *>(&m_showWarning)),
                             ToolBarItem::forAspect(const_cast<Utils::BoolAspect *>(&m_showError)),
                             ToolBarItem::forWidget(m_spacer),
                             ToolBarItem::forWidget(m_statusLabel)};
    for (const ToolBarItem &item : baseToolBarItems())
        items << item;
    return items;
}

void Console::clearContents()
{
    m_consoleItemModel->clear();
}

bool Console::canFocus() const
{
    return true;
}

bool Console::hasFocus() const
{
    for (QWidget *widget = m_consoleWidget->window()->focusWidget(); widget != nullptr;
         widget = widget->parentWidget()) {
        if (widget == m_consoleWidget)
            return true;
    }
    return false;
}

void Console::setFocus()
{
    m_consoleView->focusPrompt();
}

bool Console::canNext() const
{
    return false;
}

bool Console::canPrevious() const
{
    return false;
}

void Console::goToNext()
{
}

void Console::goToPrev()
{
}

bool Console::canNavigate() const
{
    return false;
}

void Console::readSettings()
{
    m_showDebug.readSettings();
    m_showWarning.readSettings();
    m_showError.readSettings();
}

void Console::setContext(const QString &context)
{
    m_statusLabel->setText(context);
}

void Console::writeSettings() const
{
    m_showDebug.writeSettings();
    m_showWarning.writeSettings();
    m_showError.writeSettings();
}

void Console::setScriptEvaluator(const ScriptEvaluator &evaluator)
{
    m_scriptEvaluator = evaluator;
    m_consoleItemModel->setCanFetchMore(bool(m_scriptEvaluator));
    if (!m_scriptEvaluator)
        setContext(QString());
}

void Console::populateFileFinder()
{
    m_consoleView->populateFileFinder();
}

void Console::printItem(ConsoleItem::ItemType itemType, const QString &text)
{
    printItem(new ConsoleItem(itemType, text));
}

void Console::printItem(ConsoleItem *item)
{
    m_consoleItemModel->appendItem(item);
    // Only draw attention to a message type the user has not filtered out:
    // popping the pane up for errors that are hidden anyway is just clutter.
    if (item->itemType() == ConsoleItem::ErrorType) {
        if (m_showError())
            popup(Core::IOutputPane::ModeSwitch);
    } else if (item->itemType() == ConsoleItem::WarningType) {
        if (m_showWarning())
            flash();
    }
}

void Console::evaluate(const QString &expression)
{
    if (m_scriptEvaluator) {
        m_consoleItemModel->shiftEditableRow();
        m_scriptEvaluator(expression);
    } else {
        auto item = new ConsoleItem(
            ConsoleItem::ErrorType, Tr::tr("Can only evaluate during a debug session."));
        m_consoleItemModel->shiftEditableRow();
        printItem(item);
    }
}

} // Debugger::Internal
