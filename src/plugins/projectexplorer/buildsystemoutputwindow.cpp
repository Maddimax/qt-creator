// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "buildsystemoutputwindow.h"

#include "projectexplorerconstants.h"
#include "projectexplorertr.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/find/optionspopup.h>
#include <coreplugin/icontext.h>
#include <coreplugin/outputpaneview.h>

#include <texteditor/fontsettings.h>

#include <utils/fancylineedit.h>
#include <utils/qtcassert.h>
#include <utils/stylehelper.h>
#include <utils/widgets.h>
#include <utils/utilsicons.h>

#include <coreplugin/find/ifindsupport.h>
#include <utils/aggregate.h>
#include <QQuickItem>
#include <QQuickWidget>
#include <QTest>
#include <QHBoxLayout>
#include <QVBoxLayout>

using namespace Core;
using namespace Utils;

namespace ProjectExplorer::Internal {

const char kBuildSystemOutputContext[] = "ProjectsMode.BuildSystemOutput";
const char kRegExpActionId[] = "OutputFilter.RegularExpressions.BuildSystemOutput";
const char kCaseSensitiveActionId[] = "OutputFilter.CaseSensitive.BuildSystemOutput";
const char kInvertActionId[] = "OutputFilter.Invert.BuildSystemOutput";

BuildSystemOutputWindow::BuildSystemOutputWindow()
    : m_view(new OutputPaneView(Context(kBuildSystemOutputContext),
                               "ProjectsMode.BuildSystemOutput.Zoom"))
{
    auto * const layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    IContext::attach(this, Context(kBuildSystemOutputContext));

    Command *clearCommand = ActionManager::command(Core::Constants::OUTPUTPANE_CLEAR);
    m_clear.setIcon(Utils::Icons::CLEAN_TOOLBAR.icon());
    m_clear.setText(clearCommand->action()->text());
    ActionManager::registerAction(&m_clear,
                                  Core::Constants::OUTPUTPANE_CLEAR,
                                  Context(kBuildSystemOutputContext));
    connect(&m_clear, &QAction::triggered, m_view, &OutputPaneView::clear);

    m_filterActionRegexp.setCheckable(true);
    m_filterActionRegexp.setText(Tr::tr("Use Regular Expressions"));
    connect(&m_filterActionRegexp, &QAction::toggled, this,
            &BuildSystemOutputWindow::updateFilter);
    ActionManager::registerAction(&m_filterActionRegexp,
                                  kRegExpActionId,
                                  Context(Constants::C_PROJECTEXPLORER));

    m_filterActionCaseSensitive.setCheckable(true);
    m_filterActionCaseSensitive.setText(Tr::tr("Case Sensitive"));
    connect(&m_filterActionCaseSensitive, &QAction::toggled, this,
            &BuildSystemOutputWindow::updateFilter);
    ActionManager::registerAction(&m_filterActionCaseSensitive,
                                  kCaseSensitiveActionId,
                                  Context(Constants::C_PROJECTEXPLORER));

    m_invertFilterAction.setCheckable(true);
    m_invertFilterAction.setText(Tr::tr("Show Non-matching Lines"));
    connect(&m_invertFilterAction, &QAction::toggled, this,
            &BuildSystemOutputWindow::updateFilter);
    ActionManager::registerAction(&m_invertFilterAction,
                                  kInvertActionId,
                                  Context(Constants::C_PROJECTEXPLORER));

    connect(&TextEditor::globalFontSettings(), &TextEditor::FontSettings::changed,
            this,
            [this] { m_view->setBaseFont(TextEditor::globalFontSettings().data().font()); });
    m_view->setBaseFont(TextEditor::globalFontSettings().data().font());

    m_zoomIn.setIcon(Utils::Icons::PLUS_TOOLBAR.icon());
    m_zoomIn.setText(ActionManager::command(Core::Constants::ZOOM_IN)->action()->text());
    connect(&m_zoomIn, &QAction::triggered, m_view, &OutputPaneView::zoomIn);
    ActionManager::registerAction(&m_zoomIn,
                                  Core::Constants::ZOOM_IN,
                                  Context(kBuildSystemOutputContext));

    m_zoomOut.setIcon(Utils::Icons::MINUS_TOOLBAR.icon());
    m_zoomOut.setText(ActionManager::command(Core::Constants::ZOOM_OUT)->action()->text());
    connect(&m_zoomOut, &QAction::triggered, m_view, &OutputPaneView::zoomOut);
    ActionManager::registerAction(&m_zoomOut,
                                  Core::Constants::ZOOM_OUT,
                                  Context(kBuildSystemOutputContext));
}

void BuildSystemOutputWindow::appendMessage(const QString &text, OutputFormat format)
{
    m_view->appendMessage(text, format);
}

void BuildSystemOutputWindow::grayOutOldContent()
{
    m_view->grayOutOldContent();
}

void BuildSystemOutputWindow::clearLinesPrefixedWith(const QString &prefix,
                                                     bool deleteTrailingLineBreak)
{
    m_view->clearLinesPrefixedWith(prefix, deleteTrailingLineBreak);
}

QWidget *BuildSystemOutputWindow::toolBar()
{
    if (!m_toolBar) {
        m_toolBar = new StyledBar(this);
        auto clearButton
            = Command::toolButtonWithAppendedShortcut(&m_clear, Core::Constants::OUTPUTPANE_CLEAR);

        m_filterOutputLineEdit = new FancyLineEdit;
        m_filterOutputLineEdit->setButtonVisible(FancyLineEdit::Left, true);
        m_filterOutputLineEdit->setButtonIcon(FancyLineEdit::Left, Utils::Icons::MAGNIFIER.icon());
        m_filterOutputLineEdit->setFiltering(true);
        m_filterOutputLineEdit->setHistoryCompleter("ProjectsMode.BuildSystemOutput.Filter");
        m_filterOutputLineEdit->setAttribute(Qt::WA_MacShowFocusRect, false);
        connect(m_filterOutputLineEdit, &FancyLineEdit::textChanged, this,
                &BuildSystemOutputWindow::updateFilter);
        connect(m_filterOutputLineEdit, &FancyLineEdit::returnPressed, this,
                &BuildSystemOutputWindow::updateFilter);
        connect(m_filterOutputLineEdit, &FancyLineEdit::leftButtonClicked, this, [this] {
            auto popup = new OptionsPopup(m_filterOutputLineEdit,
                                          {kRegExpActionId,
                                           kCaseSensitiveActionId,
                                           kInvertActionId});
            popup->show();
        });

        auto zoomInButton = Command::toolButtonWithAppendedShortcut(&m_zoomIn,
                                                                   Core::Constants::ZOOM_IN);
        auto zoomOutButton = Command::toolButtonWithAppendedShortcut(&m_zoomOut,
                                                                    Core::Constants::ZOOM_OUT);

        auto layout = new QHBoxLayout;
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        m_toolBar->setLayout(layout);
        layout->addWidget(clearButton);
        layout->addWidget(m_filterOutputLineEdit);
        layout->addWidget(zoomInButton);
        layout->addWidget(zoomOutButton);
        layout->addStretch();
    }
    return m_toolBar;
}

void BuildSystemOutputWindow::updateFilter()
{
    if (!m_filterOutputLineEdit)
        return;

    using Flag = OutputWindow::FilterModeFlag;
    OutputWindow::FilterModeFlags mode;
    if (m_filterActionRegexp.isChecked())
        mode |= Flag::RegExp;
    if (m_filterActionCaseSensitive.isChecked())
        mode |= Flag::CaseSensitive;
    if (m_invertFilterAction.isChecked())
        mode |= Flag::Inverted;

    m_view->setFilter(m_filterOutputLineEdit->text(), mode);
}

} // namespace ProjectExplorer::Internal
