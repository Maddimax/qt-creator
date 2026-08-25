// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "updateinfosettings.h"

#include "updateinfoplugin.h"
#include "updateinfotr.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <utils/aspects.h>
#include <utils/guiutils.h>
#include <utils/shutdownguard.h>
#include <utils/qtcassert.h>

#include <QDate>
#include <QLocale>

using namespace Utils;

namespace UpdateInfo::Internal {

// The message beside Check Now, and the button itself: one line that says
// what the last check found, or that one is running.
class CheckNowAspect final : public BaseAspect
{
    Q_OBJECT

public:
    using BaseAspect::BaseAspect;

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::TextWithAction;
        p.actionText = Tr::tr("Check Now");
        return p;
    }

    QString displayText() const override { return m_message; }
    void triggerAction() override { if (m_onTrigger) m_onTrigger(); }

    void setMessage(const QString &message)
    {
        if (m_message == message)
            return;
        m_message = message;
        emit displayTextChanged();
    }

    void setOnTrigger(const std::function<void()> &onTrigger) { m_onTrigger = onTrigger; }

private:
    QString m_message;
    std::function<void()> m_onTrigger;
};

class UpdateInfoSettingsPageWidget final : public AspectContainer
{
public:
    UpdateInfoSettingsPageWidget(UpdateInfoPlugin *plugin);

    void apply() final;

private:
    void updateLastCheckDate();
    void updateNextCheckDate();
    UpdateInfoPlugin::CheckUpdateInterval currentCheckInterval() const;

    UpdateInfoPlugin *m_plugin;

    AspectContainer m_updatesGroup{this};
    BoolAspect m_automaticCheck{this};
    TextDisplay m_info{&m_updatesGroup};
    TypedSelectionAspect<UpdateInfoPlugin::CheckUpdateInterval> m_checkInterval{&m_updatesGroup};
    TextDisplay m_nextCheckDate{&m_updatesGroup};
    BoolAspect m_checkForNewQtVersions{&m_updatesGroup};

    TextDisplay m_lastCheckDate{this};
    CheckNowAspect m_checkNow{this};
};

static QString localizedDate(const QDate &date)
{
    static QLocale locale(Core::ICore::userInterfaceLanguage());
    return locale.toString(date, locale.dateFormat());
}

UpdateInfoSettingsPageWidget::UpdateInfoSettingsPageWidget(UpdateInfoPlugin *plugin)
    : m_plugin(plugin)
{
    setAutoApply(false);
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/UpdateInfo/UpdateSettingsPage.qml"));

    m_automaticCheck.setQmlName("AutomaticCheck");
    m_automaticCheck.setLabelText(Tr::tr("Automatic Check for Updates"));
    m_automaticCheck.setValue(m_plugin->isAutomaticCheck());

    m_updatesGroup.setQmlName("Updates");
    m_updatesGroup.setLabelText(m_automaticCheck.labelText());

    m_info.setQmlName("Info");
    m_info.setText(Tr::tr("Automatically runs a scheduled check for updates on "
                          "a time interval basis. The automatic check for updates "
                          "will be performed at the scheduled date, or the next "
                          "startup following it."));
    m_info.setWordWrap(true);

    m_checkInterval.setQmlName("Interval");
    m_checkInterval.setLabelText(Tr::tr("Check interval basis:"));
    m_checkInterval.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    m_checkInterval.addOption({Tr::tr("Daily"), {}, UpdateInfoPlugin::DailyCheck});
    m_checkInterval.addOption({Tr::tr("Weekly"), {}, UpdateInfoPlugin::WeeklyCheck});
    m_checkInterval.addOption({Tr::tr("Monthly"), {}, UpdateInfoPlugin::MonthlyCheck});
    m_checkInterval.setValue(m_plugin->checkUpdateInterval());

    m_nextCheckDate.setQmlName("NextCheckDate");
    m_nextCheckDate.setLabelText(Tr::tr("Next check date:"));

    m_checkForNewQtVersions.setQmlName("CheckForNewQtVersions");
    m_checkForNewQtVersions.setLabel(Tr::tr("Check for new Qt versions"));
    m_checkForNewQtVersions.setLabelPlacement(BoolAspect::LabelPlacement::Compact);
    m_checkForNewQtVersions.setValue(m_plugin->isCheckingForQtVersions());

    m_lastCheckDate.setQmlName("LastCheckDate");
    m_lastCheckDate.setLabelText(Tr::tr("Last check date:"));

    m_checkNow.setQmlName("CheckNow");
    m_checkNow.setOnTrigger([this] { m_plugin->startCheckForUpdates(); });

    // Behaviour, not layout.
    const auto checkRunningChanged = [this](bool running) {
        // The spinner the page used to overlay itself with said the same
        // thing as the message beside the button, and only while the button
        // was disabled anyway.
        m_checkNow.setEnabled(!running);
        m_checkNow.setMessage(running ? Tr::tr("Checking for updates...") : QString());
    };
    connect(&m_checkInterval, &BaseAspect::volatileValueChanged,
            this, &UpdateInfoSettingsPageWidget::updateNextCheckDate);
    connect(m_plugin, &UpdateInfoPlugin::lastCheckDateChanged,
            this, &UpdateInfoSettingsPageWidget::updateLastCheckDate);
    connect(m_plugin, &UpdateInfoPlugin::checkForUpdatesRunningChanged, this, checkRunningChanged);
    connect(m_plugin, &UpdateInfoPlugin::newUpdatesAvailable, this, [this](bool available) {
        m_checkNow.setMessage(available ? Tr::tr("New updates are available.")
                                        : Tr::tr("No new updates are available."));
    });

    updateLastCheckDate();
    checkRunningChanged(m_plugin->isCheckForUpdatesRunning());
}

UpdateInfoPlugin::CheckUpdateInterval UpdateInfoSettingsPageWidget::currentCheckInterval() const
{
    return m_checkInterval.volatileValue();
}

void UpdateInfoSettingsPageWidget::updateLastCheckDate()
{
    const QDate date = m_plugin->lastCheckDate();
    m_lastCheckDate.setText(date.isValid() ? localizedDate(date) : Tr::tr("Not checked yet"));
    updateNextCheckDate();
}

void UpdateInfoSettingsPageWidget::updateNextCheckDate()
{
    QDate date = m_plugin->nextCheckDate(currentCheckInterval());
    if (!date.isValid() || date < QDate::currentDate())
        date = QDate::currentDate();

    m_nextCheckDate.setText(localizedDate(date));
}

void UpdateInfoSettingsPageWidget::apply()
{
    m_plugin->setCheckUpdateInterval(currentCheckInterval());
    m_plugin->setAutomaticCheck(m_automaticCheck.volatileValue());
    m_plugin->setCheckingForQtVersions(m_checkForNewQtVersions.volatileValue());
    AspectContainer::apply();
}

// SettingsPage

class SettingsPage : public Core::IOptionsPage
{
public:
    explicit SettingsPage(UpdateInfoPlugin *plugin)
    {
        setId(FILTER_OPTIONS_PAGE_ID);
        setCategory(Core::Constants::SETTINGS_CATEGORY_CORE);
        setDisplayName(Tr::tr("Update"));
        setSettingsProvider([plugin] {
            static GuardedObject<UpdateInfoSettingsPageWidget> theAspects(plugin);
            return theAspects.get();
        });
    }
};

void setupSettings(UpdateInfoPlugin *plugin)
{
    static SettingsPage theSettingsPage(plugin);
}

} // UpdateInfoPlugin::Internal

#include "updateinfosettings.moc"
