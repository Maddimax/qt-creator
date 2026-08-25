// Copyright (C) 2020 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "jlinkuvscserverprovider.h"

#include "uvproject.h"
#include "uvprojectwriter.h"
#include "uvscserverprovider.h"

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

class JLinkUvscAdapterOptions final
{
public:
    enum Port { JTAG, SWD };
    enum Speed {
        Speed_50MHz = 50000, Speed_33MHz = 33000, Speed_25MHz = 25000,
        Speed_20MHz = 20000, Speed_10MHz = 10000, Speed_5MHz = 5000,
        Speed_3MHz = 3000, Speed_2MHz = 2000, Speed_1MHz = 1000,
        Speed_500kHz = 500, Speed_200kHz = 200, Speed_100kHz = 100,
    };
    Port port = Port::SWD;
    Speed speed = Speed::Speed_1MHz;

    Store toMap() const;
    bool fromMap(const Store &data);
    bool operator==(const JLinkUvscAdapterOptions &other) const;
};

static int decodeSpeedCode(JLinkUvscAdapterOptions::Speed speed)
{
    switch (speed) {
    case JLinkUvscAdapterOptions::Speed_50MHz:
        return 8;
    case JLinkUvscAdapterOptions::Speed_33MHz:
        return 9;
    case JLinkUvscAdapterOptions::Speed_25MHz:
        return 10;
    case JLinkUvscAdapterOptions::Speed_20MHz:
        return 0;
    case JLinkUvscAdapterOptions::Speed_10MHz:
        return 1;
    case JLinkUvscAdapterOptions::Speed_5MHz:
        return 2;
    case JLinkUvscAdapterOptions::Speed_3MHz:
        return 3;
    case JLinkUvscAdapterOptions::Speed_2MHz:
        return 4;
    case JLinkUvscAdapterOptions::Speed_1MHz:
        return 5;
    case JLinkUvscAdapterOptions::Speed_500kHz:
        return 6;
    case JLinkUvscAdapterOptions::Speed_200kHz:
        return 7;
    default:
        return 8;
    }
}

static QString buildAdapterOptions(const JLinkUvscAdapterOptions &opts)
{
    QString s;
    if (opts.port == JLinkUvscAdapterOptions::JTAG)
        s += "-O14";
    else if (opts.port == JLinkUvscAdapterOptions::SWD)
        s += "-O78";

    const int code = decodeSpeedCode(opts.speed);
    s += " -S" + QString::number(code) + " -ZTIFSpeedSel" + QString::number(opts.speed);
    return s;
}

static QString buildDllRegistryName(const DeviceSelection &device,
                                    const JLinkUvscAdapterOptions &opts)
{
    if (device.algorithmIndex < 0 || device.algorithmIndex >= int(device.algorithms.size()))
        return {};

    const DeviceSelection::Algorithm algorithm = device.algorithms.at(device.algorithmIndex);
    const QFileInfo path(algorithm.path);
    const QString flashStart = UvscServerProvider::adjustFlashAlgorithmProperty(algorithm.flashStart);
    const QString flashSize = UvscServerProvider::adjustFlashAlgorithmProperty(algorithm.flashSize);
    const QString adaptOpts = buildAdapterOptions(opts);

    QString content = QStringLiteral(" %6 -FN1 -FF0%1 -FS0%2 -FL0%3 -FP0($$Device:%4$%5)")
            .arg(path.fileName(), flashStart, flashSize, device.name, path.filePath(), adaptOpts);

    if (!algorithm.ramStart.isEmpty()) {
        const QString ramStart = UvscServerProvider::adjustFlashAlgorithmProperty(algorithm.ramStart);
        content += QStringLiteral(" -FD%1").arg(ramStart);
    }
    if (!algorithm.ramSize.isEmpty()) {
        const QString ramSize = UvscServerProvider::adjustFlashAlgorithmProperty(algorithm.ramSize);
        content += QStringLiteral(" -FC%1").arg(ramSize);
    }

    return content;
}

// JLinkUvscAdapterOptions

Store JLinkUvscAdapterOptions::toMap() const
{
    Store map;
    map.insert(adapterPortKeyC, port);
    map.insert(adapterSpeedKeyC, speed);
    return map;
}

bool JLinkUvscAdapterOptions::fromMap(const Store &data)
{
    port = static_cast<Port>(data.value(adapterPortKeyC, SWD).toInt());
    speed = static_cast<Speed>(data.value(adapterSpeedKeyC, Speed_1MHz).toInt());
    return true;
}

bool JLinkUvscAdapterOptions::operator==(const JLinkUvscAdapterOptions &other) const
{
    return port == other.port && speed == other.speed;
}

// JLinkUvscServerProvider

class JLinkUvscServerProvider final : public UvscServerProvider
{
public:
    void toMap(Store &data) const final;
    void fromMap(const Store &data) final;

    bool operator==(const IDebugServerProvider &other) const final;
    Utils::FilePath optionsFilePath(ProjectExplorer::RunControl *runControl,
                                    QString &errorMessage) const final;
private:
    explicit JLinkUvscServerProvider();

    void addSettingsRows(Utils::AspectContainer &rows) final;

    // Which wire the probe uses and how fast, as one row.
    Utils::AspectContainer adapterRow{this};
    Utils::TypedSelectionAspect<JLinkUvscAdapterOptions::Port> adapterPort{&adapterRow};
    Utils::TypedSelectionAspect<JLinkUvscAdapterOptions::Speed> adapterSpeed{&adapterRow};
    JLinkUvscAdapterOptions m_adapterOpts;

    friend class JLinkUvscServerProviderFactory;
    friend class JLinkUvProjectOptions;
};

// JLinkUvProjectOptions

class JLinkUvProjectOptions final : public Uv::ProjectOptions
{
public:
    explicit JLinkUvProjectOptions(const JLinkUvscServerProvider *provider)
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

JLinkUvscServerProvider::JLinkUvscServerProvider()
    : UvscServerProvider(Constants::UVSC_JLINK_PROVIDER_ID)
{
    setTypeDisplayName(Tr::tr("uVision JLink"));
    setSupportedDrivers({"Segger\\JL2CM3.dll"});

    adapterRow.setInlineRow(true);
    adapterRow.setLabelText(Tr::tr("Adapter options:"));
    adapterPort.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    adapterPort.addOption({Tr::tr("JTAG"), {}, JLinkUvscAdapterOptions::JTAG});
    adapterPort.addOption({Tr::tr("SWD"), {}, JLinkUvscAdapterOptions::SWD});
    adapterSpeed.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    for (const auto &[text, speed] : QList<std::pair<QString, JLinkUvscAdapterOptions::Speed>>{
             {Tr::tr("50MHz"), JLinkUvscAdapterOptions::Speed_50MHz},
             {Tr::tr("33MHz"), JLinkUvscAdapterOptions::Speed_33MHz},
             {Tr::tr("25MHz"), JLinkUvscAdapterOptions::Speed_25MHz},
             {Tr::tr("20MHz"), JLinkUvscAdapterOptions::Speed_20MHz},
             {Tr::tr("10MHz"), JLinkUvscAdapterOptions::Speed_10MHz},
             {Tr::tr("5MHz"), JLinkUvscAdapterOptions::Speed_5MHz},
             {Tr::tr("3MHz"), JLinkUvscAdapterOptions::Speed_3MHz},
             {Tr::tr("2MHz"), JLinkUvscAdapterOptions::Speed_2MHz},
             {Tr::tr("1MHz"), JLinkUvscAdapterOptions::Speed_1MHz},
             {Tr::tr("500kHz"), JLinkUvscAdapterOptions::Speed_500kHz},
             {Tr::tr("200kHz"), JLinkUvscAdapterOptions::Speed_200kHz},
             {Tr::tr("100kHz"), JLinkUvscAdapterOptions::Speed_100kHz}}) {
        adapterSpeed.addOption({text, {}, speed});
    }

    // The two are stored as one value, so they are written back together.
    const auto updateAdapterOptions = [this] {
        m_adapterOpts = {adapterPort.volatileValue(), adapterSpeed.volatileValue()};
    };
    connect(&adapterPort, &BaseAspect::volatileValueChanged, this, updateAdapterOptions);
    connect(&adapterSpeed, &BaseAspect::volatileValueChanged, this, updateAdapterOptions);
}

void JLinkUvscServerProvider::addSettingsRows(AspectContainer &rows)
{
    UvscServerProvider::addSettingsRows(rows);
    adapterPort.setValue(m_adapterOpts.port);
    adapterSpeed.setValue(m_adapterOpts.speed);
    rows.registerAspect(&adapterRow);
}

void JLinkUvscServerProvider::toMap(Store &data) const
{
    UvscServerProvider::toMap(data);
    data.insert(adapterOptionsKeyC, variantFromStore(m_adapterOpts.toMap()));
}

void JLinkUvscServerProvider::fromMap(const Store &data)
{
    UvscServerProvider::fromMap(data);
    m_adapterOpts.fromMap(storeFromVariant(data.value(adapterOptionsKeyC)));
}

bool JLinkUvscServerProvider::operator==(const IDebugServerProvider &other) const
{
    if (!UvscServerProvider::operator==(other))
        return false;
    const auto p = static_cast<const JLinkUvscServerProvider *>(&other);
    return m_adapterOpts == p->m_adapterOpts;
    return true;
}

FilePath JLinkUvscServerProvider::optionsFilePath(RunControl *runControl,
                                                  QString &errorMessage) const
{
    const FilePath optionsPath = buildOptionsFilePath(runControl);
    std::ofstream ofs(optionsPath.path().toStdString(), std::ofstream::out);
    Uv::ProjectOptionsWriter writer(&ofs);
    const JLinkUvProjectOptions projectOptions(this);
    if (!writer.write(&projectOptions)) {
        errorMessage = Tr::tr("Unable to create a uVision project options template.");
        return {};
    }
    return optionsPath;
}

// JLinkUvscServerProviderFactory

class JLinkUvscServerProviderFactory final : public IDebugServerProviderFactory
{
public:
    JLinkUvscServerProviderFactory()
    {
        setId(Constants::UVSC_JLINK_PROVIDER_ID);
        setDisplayName(Tr::tr("uVision JLink"));
        setCreator([] { return new JLinkUvscServerProvider; });
    }
};

void setupJLinkUvscServerProvider()
{
    static JLinkUvscServerProviderFactory theJLinkUvscServerProviderFactory;
}

} // BareMetal::Internal

#include "jlinkuvscserverprovider.moc"
