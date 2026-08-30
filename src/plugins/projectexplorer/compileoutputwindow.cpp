// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "compileoutputwindow.h"

#include "buildmanager.h"
#include "projectexplorerconstants.h"
#include "projectexplorericons.h"
#include "projectexplorertr.h"
#include "showoutputtaskhandler.h"
#include "taskhub.h"

#include <coreplugin/outputpaneview.h>
#include <coreplugin/outputtasksink.h>
#include <coreplugin/outputwindow.h>
#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>
#include <coreplugin/coreconstants.h>

#include <extensionsystem/pluginmanager.h>

#include <texteditor/fontsettings.h>
#include <texteditor/behaviorsettings.h>

#include <utils/outputformatter.h>
#include <utils/proxyaction.h>
#include <utils/stylehelper.h>
#include <utils/stylehelperpainting.h>
#include <utils/theme/theme.h>
#include <utils/utilsicons.h>

#include <QCheckBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

#include <climits>

namespace ProjectExplorer::Internal {

const char SETTINGS_KEY[] = "ProjectExplorer/CompileOutput/Zoom";
const char C_COMPILE_OUTPUT[] = "ProjectExplorer.CompileOutput";
const char OPTIONS_PAGE_ID[] = "C.ProjectExplorer.CompileOutputOptions";

CompileOutputWindow::CompileOutputWindow(QAction *cancelBuildAction) :
    m_cancelBuildButton(new QToolButton),
    m_settingsButton(new QToolButton)
{
    setId("CompileOutput");
    setDisplayName(QCoreApplication::translate("QtC::ProjectExplorer", "Compile Output"));
    setPriorityInStatusBar(40);

    Core::Context context(C_COMPILE_OUTPUT);
    m_outputWindow = new Core::OutputPaneView(SETTINGS_KEY);
    m_outputWindow->setWindowTitle(displayName());
    m_outputWindow->setWindowIcon(Icons::WINDOW.icon());

    Utils::ProxyAction *cancelBuildProxyButton =
            Utils::ProxyAction::proxyActionWithIcon(cancelBuildAction,
                                                    Utils::Icons::STOP_SMALL_TOOLBAR.icon());
    m_cancelBuildButton->setDefaultAction(cancelBuildProxyButton);
    m_settingsButton->setToolTip(Core::ICore::msgShowSettings());
    m_settingsButton->setIcon(Utils::Icons::SETTINGS_TOOLBAR.icon());

    auto updateFontSettings = [this] {
        m_outputWindow->setBaseFont(TextEditor::globalFontSettings().data().font());
    };

    auto updateZoomEnabled = [this] {
        m_outputWindow->setWheelZoomEnabled(
                    TextEditor::globalBehaviorSettings().scrollWheelZooming());
    };

    updateFontSettings();
    updateZoomEnabled();
    setupFilterUi("CompileOutputPane.Filter", "ProjectExplorer::Internal::CompileOutputPane");
    setFilteringEnabled(true);

    connect(this, &IOutputPane::zoomInRequested, m_outputWindow, &Core::OutputPaneView::zoomIn);
    connect(this, &IOutputPane::zoomOutRequested, m_outputWindow, &Core::OutputPaneView::zoomOut);
    connect(this, &IOutputPane::resetZoomRequested,
            m_outputWindow, &Core::OutputPaneView::resetZoom);
    connect(&TextEditor::globalFontSettings(), &TextEditor::FontSettings::changed,
            this, updateFontSettings);
    connect(&TextEditor::globalBehaviorSettings(), &Utils::AspectContainer::changed,
            this, updateZoomEnabled);

    connect(m_settingsButton, &QToolButton::clicked, this, [] {
        Core::ICore::showSettings(OPTIONS_PAGE_ID);
    });

    qRegisterMetaType<QTextCharFormat>("QTextCharFormat");

    m_handler = new ShowOutputTaskHandler(this,
        Tr::tr("Show Compile &Output"),
        Tr::tr("Show the output that generated this issue in Compile Output."),
        Tr::tr("O"));
    ExtensionSystem::PluginManager::addObject(m_handler);
    setupContext(C_COMPILE_OUTPUT, m_outputWindow);
    updateFromSettings();

    CompileOutputSettings &s = compileOutputSettings();
    m_outputWindow->setWordWrapEnabled(s.wrapOutput());
    m_outputWindow->setDiscardExcessiveOutput(s.discardOutput());
    m_outputWindow->setMaxCharCount(s.maxCharCount());

    connect(&s.wrapOutput, &Utils::BaseAspect::changed, m_outputWindow, [this] {
        m_outputWindow->setWordWrapEnabled(compileOutputSettings().wrapOutput());
    });
    connect(&s.discardOutput, &Utils::BaseAspect::changed, m_outputWindow, [this] {
        m_outputWindow->setDiscardExcessiveOutput(compileOutputSettings().discardOutput());
    });
    connect(&s.maxCharCount, &Utils::BaseAspect::changed, m_outputWindow, [this] {
        m_outputWindow->setMaxCharCount(compileOutputSettings().maxCharCount());
    });
    connect(m_outputWindow, &Core::OutputPaneView::outputDiscarded, this, [] {
        TaskHub::addTask(
            Task::Warning,
            Tr::tr("Discarded excessive compile output."),
            Constants::TASK_CATEGORY_COMPILE);
    });
    connect(&s.overwriteColor, &Utils::BaseAspect::changed,
            this, &CompileOutputWindow::updateFromSettings);
    connect(&s.backgroundColor, &Utils::BaseAspect::changed,
            this, &CompileOutputWindow::updateFromSettings);
}

CompileOutputWindow::~CompileOutputWindow()
{
    ExtensionSystem::PluginManager::removeObject(m_handler);
    delete m_handler;
    delete m_cancelBuildButton;
    delete m_settingsButton;
}

void CompileOutputWindow::updateFromSettings()
{
    QColor background;
    if (compileOutputSettings().overwriteColor())
            background = compileOutputSettings().backgroundColor();
    if (!background.isValid())
            background = Utils::creatorColor(Utils::Theme::PaletteBase);

    m_outputWindow->setBackgroundColor(background);
}

bool CompileOutputWindow::hasFocus() const
{
    return m_outputWindow->window()->focusWidget()
           && m_outputWindow->isAncestorOf(m_outputWindow->window()->focusWidget());
}

bool CompileOutputWindow::canFocus() const
{
    return true;
}

void CompileOutputWindow::setFocus()
{
    m_outputWindow->setFocus();
}

QWidget *CompileOutputWindow::outputWidget(QWidget *)
{
    return m_outputWindow;
}

QList<QWidget *> CompileOutputWindow::toolBarWidgets() const
{
    return QList<QWidget *>{m_cancelBuildButton, m_settingsButton} + IOutputPane::toolBarWidgets();
}

void CompileOutputWindow::appendText(const QString &text, BuildStep::OutputFormat format)
{
    Utils::OutputFormat fmt = Utils::NormalMessageFormat;
    switch (format) {
    case BuildStep::OutputFormat::Stdout:
        fmt = Utils::StdOutFormat;
        break;
    case BuildStep::OutputFormat::Stderr:
        fmt = Utils::StdErrFormat;
        break;
    case BuildStep::OutputFormat::NormalMessage:
        fmt = Utils::NormalMessageFormat;
        break;
    case BuildStep::OutputFormat::ErrorMessage:
        fmt = Utils::ErrorMessageFormat;
        break;

    }

    m_outputWindow->appendMessage(text, fmt);
}

QStringList CompileOutputWindow::outputTexts() const
{
    return {m_outputWindow->toPlainText()};
}

bool CompileOutputWindow::canShowPositionOf(unsigned taskId) const
{
    return m_outputWindow->knowsPositionOf(taskId);
}

void CompileOutputWindow::showPositionOf(unsigned taskId)
{
    m_outputWindow->showPositionOf(taskId);
}

void CompileOutputWindow::clearContents()
{
    m_outputWindow->clear();
}

bool CompileOutputWindow::canNext() const
{
    return false;
}

bool CompileOutputWindow::canPrevious() const
{
    return false;
}

void CompileOutputWindow::goToNext()
{ }

void CompileOutputWindow::goToPrev()
{ }

bool CompileOutputWindow::canNavigate() const
{
    return false;
}

bool CompileOutputWindow::hasFilterContext() const
{
    return true;
}

void CompileOutputWindow::registerPositionOf(const Task &task, int linkedOutputLines, int skipLines,
                                             int offset)
{
    m_outputWindow->registerPositionOf(
        task.id(), linkedOutputLines, skipLines, offset, Core::OutputTaskSink::TaskSource::Direct);
}

void CompileOutputWindow::flush()
{
    m_outputWindow->flush();
}

void CompileOutputWindow::reset()
{
    m_outputWindow->reset();
}

Utils::OutputFormatter *CompileOutputWindow::outputFormatter() const
{
    return m_outputWindow->formatter();
}

void CompileOutputWindow::updateFilter()
{
    m_outputWindow->setFilter(filterText(), filterCaseSensitivity(), filterUsesRegexp(),
                              filterIsInverted(), beforeContext(), afterContext());
}

// CompileOutputSettings

CompileOutputSettings &compileOutputSettings()
{
    static CompileOutputSettings theSettings;
    return theSettings;
}

CompileOutputSettings::CompileOutputSettings()
{
    setAutoApply(false);

    wrapOutput.setSettingsKey("ProjectExplorer/Settings/WrapBuildOutput");
    wrapOutput.setDefaultValue(true);
    wrapOutput.setLabelText(Tr::tr("Word-wrap output"));

    popUp.setSettingsKey("ProjectExplorer/Settings/ShowCompilerOutput");
    popUp.setLabelText(Tr::tr("Open Compile Output when building"));

    discardOutput.setSettingsKey("ProjectExplorer/Settings/DiscardCompilerOutput");
    discardOutput.setLabelText(Tr::tr("Discard excessive output"));
    discardOutput.setToolTip(
        Tr::tr(
            "Discards compile output that continuously comes in faster than "
            "it can be handled."));

    maxCharCount.setSettingsKey("ProjectExplorer/Settings/MaxBuildOutputLines");
    // The words around the number, which the layout used to hold: it split
    // "Limit output to %1 characters" and put the spin box between the halves.
    {
        const QStringList parts = Tr::tr("Limit output to %1 characters").split("%1")
                                  << QString() << QString();
        maxCharCount.setPrefix(parts.at(0).trimmed());
        maxCharCount.setSuffix(parts.at(1).trimmed());
    }
    maxCharCount.setRange(1, INT_MAX);
    maxCharCount.setDefaultValue(Core::Constants::DEFAULT_MAX_CHAR_COUNT);

    overwriteColor.setSettingsKey("ProjectExplorer/CompileOutput/OverwriteBackground");
    overwriteColor.setLabelText(Tr::tr("Overwrite background color"));
    overwriteColor.setToolTip("Customize background color of the compile output.\n"
                              "Note: existing output will not get recolored.");

    backgroundColor.setSettingsKey("ProjectExplorer/CompileOutput/BackgroundColor");
    backgroundColor.setDefaultValue(QColor{});
    backgroundColor.setEnabler(&overwriteColor);

    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ProjectExplorer/CompileOutputSettingsPage.qml"));

    readSettings();
}

// CompileOutputSettingsPage

class CompileOutputSettingsPage final : public Core::IOptionsPage
{
public:
    CompileOutputSettingsPage()
    {
        setId(OPTIONS_PAGE_ID);
        setDisplayName(Tr::tr("Compile Output"));
        setCategory(Constants::BUILD_AND_RUN_SETTINGS_CATEGORY);
        setSettingsProvider([] { return &compileOutputSettings(); });
    }
};

const CompileOutputSettingsPage settingsPage;

} // ProjectExplorer::Internal
