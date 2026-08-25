// Copyright (C) 2020 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "stlinkuvscserverprovider.h"

#include "uvscserverprovider.h"
#include "uvproject.h"
#include "uvprojectwriter.h"

#include <baremetal/baremetalconstants.h>
#include <baremetal/baremetaltr.h>
#include <baremetal/debugserverprovidermanager.h>

#include <QComboBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>

#include <fstream> // for std::ofstream

using namespace Debugger;
using namespace ProjectExplorer;
using namespace Utils;

namespace BareMetal::Internal {

using namespace Uv;

constexpr char adapterOptionsKeyC[] = "AdapterOptions";
constexpr char adapterPortKeyC[] = "AdapterPort";
constexpr char adapterSpeedKeyC[] = "AdapterSpeed";

// StLinkUvscAdapterOptions

class StLinkUvscAdapterOptions final
{
public:
    enum Port { JTAG, SWD };
    enum Speed {
        // SWD speeds.
        Speed_4MHz = 0, Speed_1_8MHz, Speed_950kHz, Speed_480kHz,
        Speed_240kHz, Speed_125kHz, Speed_100kHz, Speed_50kHz,
        Speed_25kHz, Speed_15kHz, Speed_5kHz,
        // JTAG speeds.
        Speed_9MHz = 256, Speed_4_5MHz, Speed_2_25MHz, Speed_1_12MHz,
        Speed_560kHz, Speed_280kHz, Speed_140kHz,
    };
    Port port = Port::SWD;
    Speed speed = Speed::Speed_4MHz;

    QVariantMap toMap() const;
    bool fromMap(const Store &data);
    bool operator==(const StLinkUvscAdapterOptions &other) const;
};

static QString buildAdapterOptions(const StLinkUvscAdapterOptions &opts)
{
    QString s;
    if (opts.port == StLinkUvscAdapterOptions::JTAG)
        s += "-0142";
    else if (opts.port == StLinkUvscAdapterOptions::SWD)
        s += "-0206";

    s += " -S" + QString::number(opts.speed);
    return s;
}

static QString buildDllRegistryName(const DeviceSelection &device,
                                    const StLinkUvscAdapterOptions &opts)
{
    if (device.algorithmIndex < 0 || device.algorithmIndex >= int(device.algorithms.size()))
        return {};
    const DeviceSelection::Algorithm algorithm = device.algorithms.at(device.algorithmIndex);
    const QFileInfo path(algorithm.path);
    const QString flashStart = UvscServerProvider::adjustFlashAlgorithmProperty(algorithm.flashStart);
    const QString flashSize = UvscServerProvider::adjustFlashAlgorithmProperty(algorithm.flashSize);
    const QString adaptOpts = buildAdapterOptions(opts);
    return QStringLiteral(" %6 -FN1 -FF0%1 -FS0%2 -FL0%3 -FP0($$Device:%4$%5)")
            .arg(path.fileName(), flashStart, flashSize, device.name, path.filePath(), adaptOpts);
}

// StLinkUvscServerProvider

class StLinkUvscServerProvider final : public UvscServerProvider
{
public:
    void toMap(Store &data) const final;
    void fromMap(const Store &data) final;

    bool operator==(const IDebugServerProvider &other) const final;
    Utils::FilePath optionsFilePath(ProjectExplorer::RunControl *runControl,
                                    QString &errorMessage) const final;
private:
    explicit StLinkUvscServerProvider();

    void addSettingsRows(Utils::AspectContainer &rows) final;
    void refreshSpeeds();

    // Which wire the probe uses and how fast, as one row. The speeds on
    // offer are the port's: JTAG and SWD do not share any of them.
    Utils::AspectContainer adapterRow{this};
    Utils::TypedSelectionAspect<StLinkUvscAdapterOptions::Port> adapterPort{&adapterRow};
    Utils::TypedSelectionAspect<StLinkUvscAdapterOptions::Speed> adapterSpeed{&adapterRow};
    StLinkUvscAdapterOptions m_adapterOpts;

    friend class StLinkUvscServerProviderFactory;
    friend class StLinkUvProjectOptions;
};

// StLinkUvProjectOptions

class StLinkUvProjectOptions final : public Uv::ProjectOptions
{
public:
    explicit StLinkUvProjectOptions(const StLinkUvscServerProvider *provider)
        : Uv::ProjectOptions(provider)
    {
        const DriverSelection driver = provider->driverSelection();
        const DeviceSelection device = provider->deviceSelection();
        m_debugOpt->appendProperty("nTsel", driver.index);
        m_debugOpt->appendProperty("pMon", driver.dll);

        // Fill 'TargetDriverDllRegistry' (required for dedugging).
        const auto dllRegistry = m_targetOption->appendPropertyGroup("TargetDriverDllRegistry");
        const auto setRegEntry = dllRegistry->appendPropertyGroup("SetRegEntry");
        setRegEntry->appendProperty("Number", 0);
        const QString key = UvscServerProvider::buildDllRegistryKey(driver);
        setRegEntry->appendProperty("Key", key);
        const QString name = buildDllRegistryName(device, provider->m_adapterOpts);
        setRegEntry->appendProperty("Name", name);
    }
};

QVariantMap StLinkUvscAdapterOptions::toMap() const
{
    QVariantMap map;
    map.insert(adapterPortKeyC, port);
    map.insert(adapterSpeedKeyC, speed);
    return map;
}

bool StLinkUvscAdapterOptions::fromMap(const Store &data)
{
    port = static_cast<Port>(data.value(adapterPortKeyC, SWD).toInt());
    speed = static_cast<Speed>(data.value(adapterSpeedKeyC, Speed_4MHz).toInt());
    return true;
}

bool StLinkUvscAdapterOptions::operator==(const StLinkUvscAdapterOptions &other) const
{
    return port == other.port && speed == other.speed;
}

StLinkUvscServerProvider::StLinkUvscServerProvider()
    : UvscServerProvider(Constants::UVSC_STLINK_PROVIDER_ID)
{
    setTypeDisplayName(Tr::tr("uVision St-Link"));
    setSupportedDrivers({"STLink\\ST-LINKIII-KEIL_SWO.dll"});

    adapterRow.setInlineRow(true);
    adapterRow.setLabelText(Tr::tr("Adapter options:"));
    adapterPort.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    adapterPort.addOption({Tr::tr("JTAG"), {}, StLinkUvscAdapterOptions::JTAG});
    adapterPort.addOption({Tr::tr("SWD"), {}, StLinkUvscAdapterOptions::SWD});
    adapterSpeed.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);

    connect(&adapterPort, &BaseAspect::volatileValueChanged, this, [this] {
        refreshSpeeds();
        m_adapterOpts = {adapterPort.volatileValue(), adapterSpeed.volatileValue()};
    });
    connect(&adapterSpeed, &BaseAspect::volatileValueChanged, this, [this] {
        m_adapterOpts = {adapterPort.volatileValue(), adapterSpeed.volatileValue()};
    });

    // Reading a selection that has no options asserts, and the speeds are the
    // port's - so the list exists from the start rather than from first draw.
    refreshSpeeds();
}

void StLinkUvscServerProvider::refreshSpeeds()
{
    using Opts = StLinkUvscAdapterOptions;
    const QList<std::pair<QString, Opts::Speed>> jtag = {
        {Tr::tr("9MHz"), Opts::Speed_9MHz}, {Tr::tr("4.5MHz"), Opts::Speed_4_5MHz},
        {Tr::tr("2.25MHz"), Opts::Speed_2_25MHz}, {Tr::tr("1.12MHz"), Opts::Speed_1_12MHz},
        {Tr::tr("560kHz"), Opts::Speed_560kHz}, {Tr::tr("280kHz"), Opts::Speed_280kHz},
        {Tr::tr("140kHz"), Opts::Speed_140kHz}};
    const QList<std::pair<QString, Opts::Speed>> swd = {
        {Tr::tr("4MHz"), Opts::Speed_4MHz}, {Tr::tr("1.8MHz"), Opts::Speed_1_8MHz},
        {Tr::tr("950kHz"), Opts::Speed_950kHz}, {Tr::tr("480kHz"), Opts::Speed_480kHz},
        {Tr::tr("240kHz"), Opts::Speed_240kHz}, {Tr::tr("125kHz"), Opts::Speed_125kHz},
        {Tr::tr("100kHz"), Opts::Speed_100kHz}, {Tr::tr("50kHz"), Opts::Speed_50kHz},
        {Tr::tr("25kHz"), Opts::Speed_25kHz}, {Tr::tr("15kHz"), Opts::Speed_15kHz},
        {Tr::tr("5kHz"), Opts::Speed_5kHz}};

    adapterSpeed.clearOptions();
    for (const auto &[text, speed] :
         adapterPort.volatileValue() == Opts::JTAG ? jtag : swd) {
        adapterSpeed.addOption({text, {}, speed});
    }
}

void StLinkUvscServerProvider::addSettingsRows(AspectContainer &rows)
{
    UvscServerProvider::addSettingsRows(rows);
    adapterPort.setValue(m_adapterOpts.port);
    refreshSpeeds();
    // The speeds on offer are the port's, so a stored speed from the other
    // port is not one of them.
    if (adapterSpeed.indexForItemValue(m_adapterOpts.speed) >= 0)
        adapterSpeed.setValue(m_adapterOpts.speed);
    rows.registerAspect(&adapterRow);
}

void StLinkUvscServerProvider::toMap(Store &data) const
{
    UvscServerProvider::toMap(data);
    data.insert(adapterOptionsKeyC, m_adapterOpts.toMap());
}

void StLinkUvscServerProvider::fromMap(const Store &data)
{
    UvscServerProvider::fromMap(data);
    m_adapterOpts.fromMap(storeFromVariant(data.value(adapterOptionsKeyC)));
}

bool StLinkUvscServerProvider::operator==(const IDebugServerProvider &other) const
{
    if (!UvscServerProvider::operator==(other))
        return false;
    const auto p = static_cast<const StLinkUvscServerProvider *>(&other);
    return m_adapterOpts == p->m_adapterOpts;
    return true;
}

FilePath StLinkUvscServerProvider::optionsFilePath(RunControl *runControl,
                                                   QString &errorMessage) const
{
    const FilePath optionsPath = buildOptionsFilePath(runControl);
    std::ofstream ofs(optionsPath.path().toStdString(), std::ofstream::out);
    Uv::ProjectOptionsWriter writer(&ofs);
    const StLinkUvProjectOptions projectOptions(this);
    if (!writer.write(&projectOptions)) {
        errorMessage = Tr::tr("Unable to create a uVision project options template.");
        return {};
    }
    return optionsPath;
}

// StLinkUvscServerProviderFactory

class StLinkUvscServerProviderFactory final : public IDebugServerProviderFactory
{
public:
    StLinkUvscServerProviderFactory()
    {
        setId(Constants::UVSC_STLINK_PROVIDER_ID);
        setDisplayName(Tr::tr("uVision St-Link"));
        setCreator([] { return new StLinkUvscServerProvider; });
    }
};

void setupStLinkUvscServerProvider()
{
    static StLinkUvscServerProviderFactory theStLinkUvscServerProviderFactory;
}

} // BareMetal::Internal

#include "stlinkuvscserverprovider.moc"
