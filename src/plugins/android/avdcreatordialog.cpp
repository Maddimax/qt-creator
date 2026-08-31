// Copyright (C) 2016 BogDan Vatra <bog_dan_ro@yahoo.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "avdcreatordialog.h"

#include "androidconfigurations.h"
#include "androiddevice.h"
#include "androidsdkmanager.h"
#include "androidtr.h"

#include <coreplugin/icore.h>

#include <projectexplorer/projectexplorerconstants.h>

#include <solutions/spinner/spinner.h>
#include <QtTaskTree/QSingleTaskTreeRunner>

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/algorithm.h>
#include <utils/aspects.h>
#include <utils/environment.h>
#include <utils/widgets.h>
#include <utils/qtcprocess.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QDialog>
#include <QDialogButtonBox>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSysInfo>
#include <QVBoxLayout>

using namespace ProjectExplorer;
using namespace SpinnerSolution;
using namespace QtTaskTree;
using namespace Utils;

namespace Android::Internal {

static Q_LOGGING_CATEGORY(avdDialogLog, "qtc.android.avdDialog", QtWarningMsg)

enum class DeviceType { Phone, Tablet, Automotive, TV, Wear, Desktop, PhoneOrTablet };

static DeviceType tagToDeviceType(const QString &type_tag)
{
    if (type_tag.contains("android-wear"))
        return DeviceType::Wear;
    else if (type_tag.contains("android-tv"))
        return DeviceType::TV;
    else if (type_tag.contains("android-automotive"))
        return DeviceType::Automotive;
    else if (type_tag.contains("android-desktop"))
        return DeviceType::Desktop;
    return DeviceType::PhoneOrTablet;
}

struct DeviceDefinition
{
    QString name_id;
    QString type_str;
    DeviceType deviceType = DeviceType::Phone;

    bool operator==(const DeviceDefinition &other) const
    {
        return name_id == other.name_id && type_str == other.type_str
               && deviceType == other.deviceType;
    }
};

// Which of the listed kinds a definition belongs under. Everything the tag
// does not name is a phone or a tablet, and the two are told apart by the
// name - there is no tag that says either.
DeviceType deviceTypeFor(const QString &tag, const QString &nameId)
{
    const DeviceType tagged = tagToDeviceType(tag);
    if (tagged != DeviceType::PhoneOrTablet)
        return tagged;
    return nameId.contains("Tablet") ? DeviceType::Tablet : DeviceType::Phone;
}

/* What "avdmanager list device" says. Example:

Available devices definitions:
id: 0 or "automotive_1024p_landscape"
    Name: Automotive (1024p landscape)
    OEM : Google
    Tag : android-automotive-playstore
---------
id: 1 or "automotive_1080p_landscape"
    Name: Automotive (1080p landscape)
    OEM : Google
    Tag : android-automotive
---------
id: 2 or "Galaxy Nexus"
    Name: Galaxy Nexus
    OEM : Google
---------
id: 3 or "desktop_large"
    Name: Large Desktop
    OEM : Google
    Tag : android-desktop
...
*/
QList<DeviceDefinition> parseDeviceDefinitions(const QString &output)
{
    QList<DeviceDefinition> definitions;
    QStringList block;
    const QStringList lines = output.split('\n');
    for (const QString &line : lines) {
        if (!line.startsWith("---------") && !line.isEmpty()) {
            block << line;
            continue;
        }

        // Nothing between two separators is not a device. The output ends
        // with one and then a newline, so without this every list came out
        // with a nameless entry at the end of the phones.
        if (block.isEmpty())
            continue;

        DeviceDefinition definition;
        for (const QString &blockLine : std::as_const(block)) {
            if (blockLine.contains("id:")) {
                definition.name_id = blockLine.split("or").at(1);
                definition.name_id = definition.name_id.remove(0, 1).remove('"');
            } else if (blockLine.contains("Tag :")) {
                definition.type_str = blockLine.split(':').at(1);
                definition.type_str = definition.type_str.remove(0, 1);
            }
        }
        definition.deviceType = deviceTypeFor(definition.type_str, definition.name_id);
        definitions.append(definition);
        block.clear();
    }
    return definitions;
}

// Whether a system image tagged \a imageType belongs under \a wanted. An
// image that names neither a phone nor a tablet serves both, which is the one
// kind that widens.
bool imageServesDeviceType(DeviceType imageType, DeviceType wanted)
{
    if (imageType == DeviceType::PhoneOrTablet)
        return wanted == DeviceType::Phone || wanted == DeviceType::Tablet
               || wanted == DeviceType::PhoneOrTablet;
    return imageType == wanted;
}

// What an image is called in the list: its API level, and the variant when the
// path names one - "android-34 (google_apis)".
QString apiLevelLabel(const QString &sdkStylePath, int apiLevel)
{
    QString label = "android-" + QString::number(apiLevel);
    const QStringList splits = sdkStylePath.split(';');
    if (splits.size() == 4)
        label += QString(" (%1)").arg(splits.at(2));
    return label;
}

// What an AVD may be called. The widget field refused the keystroke and popped
// a tip beside itself; saying what is wrong is the same rule without the
// ambush.
QString avdNameIssue(const QString &name)
{
    static const QRegularExpression allowed("\\A[a-zA-Z0-9._-]*\\z");
    if (allowed.match(name).hasMatch())
        return {};
    return Tr::tr("Allowed characters are: a-z A-Z 0-9 and . _ -");
}

class AvdSettings final : public AspectContainer
{
public:
    AvdSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Android/AvdCreatorDialog.qml"));

        name.setQmlName("Name");
        name.setLabelText(Tr::tr("Name:"));
        name.setDisplayStyle(StringAspect::LineEditDisplay);
        name.setValidationFunction([](const QString &text) -> Result<> {
            const QString issue = avdNameIssue(text);
            if (!issue.isEmpty())
                return ResultError(issue);
            // Nothing typed yet is not something to complain about, but it is
            // not a name either - the Ok button follows nameIsUsable().
            return text.isEmpty() ? ResultError(QString()) : Result<>(ResultOk);
        });

        abi.setQmlName("Abi");
        abi.setLabelText(Tr::tr("Target ABI / API:"));
        abi.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
        // The host's own architectures first, 64 bit before 32: the image a
        // reader wants is nearly always one their machine can run.
        const QStringList armAbis = {Constants::ANDROID_ABI_ARM64_V8A,
                                     Constants::ANDROID_ABI_ARMEABI_V7A};
        const QStringList x86Abis = {Constants::ANDROID_ABI_X86_64, Constants::ANDROID_ABI_X86};
        const QStringList abis = QSysInfo::currentCpuArchitecture().startsWith("arm")
                                     ? armAbis + x86Abis
                                     : x86Abis + armAbis;
        for (const QString &name : abis)
            abi.addOption(name);

        targetApi.setQmlName("TargetApi");
        targetApi.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);

        warning.setQmlName("Warning");
        warning.setIconType(InfoType::Warning);
        warning.setVisible(false);

        deviceType.setQmlName("DeviceType");
        deviceType.setLabelText(Tr::tr("Skin definition:"));
        deviceType.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);

        deviceDefinition.setQmlName("DeviceDefinition");
        deviceDefinition.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);

        sdcardSize.setQmlName("SdcardSize");
        sdcardSize.setLabelText(Tr::tr("SD card size:"));
        sdcardSize.setSuffix(Tr::tr(" MiB"));
        sdcardSize.setRange(0, 1000000);
        sdcardSize.setDefaultValue(512);
        sdcardSize.setValue(512);

        overwrite.setQmlName("Overwrite");
        overwrite.setLabelText(Tr::tr("Overwrite existing AVD name"));
    }

    // A name that can be used, which is what the Ok button follows. Empty is
    // refused without a word about it.
    bool nameIsUsable() const { return !name().isEmpty() && avdNameIssue(name()).isEmpty(); }

    StringAspect name{this};
    SelectionAspect abi{this};
    SelectionAspect targetApi{this};
    TextDisplay warning{this};
    SelectionAspect deviceType{this};
    SelectionAspect deviceDefinition{this};
    IntegerAspect sdcardSize{this};
    BoolAspect overwrite{this};
};

class AvdDialog : public QDialog
{
public:
    explicit AvdDialog();
    CreateAvdInfo avdInfo() const { return m_createdAvdInfo; }

private:
    const SystemImage *systemImage() const
    {
        const int row = m_settings.targetApi.volatileValue();
        return row >= 0 && row < m_images.size() ? m_images.at(row) : nullptr;
    }
    QString deviceDefinition() const { return m_settings.deviceDefinition.stringValue(); }

    void updateDeviceDefinitionComboBox();
    void updateApiLevelComboBox();
    void collectInitialData();
    void createAvd();
    void updateOkButton();

    CreateAvdInfo m_createdAvdInfo;
    QList<DeviceDefinition> m_deviceDefinitionsList;
    QMap<DeviceType, QString> m_deviceTypeToStringMap;
    // Parallel to the choices the targetApi aspect offers, because a choice
    // carries an id and not an object.
    SystemImageList m_images;

    AvdSettings m_settings;
    QWidget *m_gui;
    QDialogButtonBox *m_buttonBox;
    QSingleTaskTreeRunner m_taskTreeRunner;
};

AvdDialog::AvdDialog()
    : QDialog(Core::ICore::dialogParent())
{
    resize(800, 0);
    setWindowTitle(Tr::tr("Create new AVD"));

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);

    m_gui = Core::createAspectForm(&m_settings);

    auto layout = new QVBoxLayout(this);
    layout->addWidget(m_gui);
    layout->addStretch();
    layout->addWidget(m_buttonBox);

    connect(&m_settings.deviceType, &BaseAspect::changed,
            this, &AvdDialog::updateDeviceDefinitionComboBox);
    connect(&m_settings.abi, &BaseAspect::changed, this, &AvdDialog::updateApiLevelComboBox);
    connect(&m_settings.name, &BaseAspect::changed, this, &AvdDialog::updateOkButton);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &AvdDialog::createAvd);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_deviceTypeToStringMap.insert(DeviceType::Phone, "Phone");
    m_deviceTypeToStringMap.insert(DeviceType::Tablet, "Tablet");
    m_deviceTypeToStringMap.insert(DeviceType::Automotive, "Automotive");
    m_deviceTypeToStringMap.insert(DeviceType::TV, "TV");
    m_deviceTypeToStringMap.insert(DeviceType::Wear, "Wear");
    m_deviceTypeToStringMap.insert(DeviceType::Desktop, "Desktop");

    updateOkButton();
    collectInitialData();
}

void AvdDialog::updateOkButton()
{
    // Both halves: a name that git - and the emulator - will take, and an
    // image to create the device from.
    m_buttonBox->button(QDialogButtonBox::Ok)
        ->setEnabled(m_settings.nameIsUsable() && !m_images.isEmpty());
}

void AvdDialog::collectInitialData()
{
    const auto onProcessSetup = [this](Process &process) {
        m_gui->setEnabled(false);
        m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
        const CommandLine cmd(AndroidConfig::avdManagerToolPath(), {"list", "device"});
        qCDebug(avdDialogLog).noquote() << "Running AVD Manager command:" << cmd.toUserOutput();
        process.setEnvironment(AndroidConfig::toolsEnvironment());
        process.setCommand(cmd);
    };
    const auto onProcessDone = [this](const Process &process, DoneWith result) {
        const QString output = process.allOutput();
        if (result == DoneWith::Error) {
            QMessageBox::warning(Core::ICore::dialogParent(), Tr::tr("Create new AVD"),
                                 Tr::tr("Avd list command failed. %1 %2")
                                     .arg(output, AndroidConfig::sdkToolsVersion().toString()));
            reject();
            return;
        }

        m_deviceDefinitionsList = parseDeviceDefinitions(output);
        for (const QString &type : std::as_const(m_deviceTypeToStringMap))
            m_settings.deviceType.addOption(type);

        updateDeviceDefinitionComboBox();
        m_gui->setEnabled(true);
    };

    struct SpinnerStruct {
        std::unique_ptr<Spinner> spinner;
    };

    const Storage<SpinnerStruct> storage;

    const auto onSetup = [this, storage] {
        storage->spinner.reset(new Spinner(SpinnerSize::Medium, m_gui));
        storage->spinner->show();
    };

    const Group recipe {
        storage,
        onGroupSetup(onSetup),
        ProcessTask(onProcessSetup, onProcessDone)
    };
    m_taskTreeRunner.start(recipe);
}

void AvdDialog::createAvd()
{
    const SystemImage *si = systemImage();
    if (!si || !si->isValid() || !m_settings.nameIsUsable()) {
        QMessageBox::warning(Core::ICore::dialogParent(),
                             Tr::tr("Create new AVD"), Tr::tr("Cannot create AVD. Invalid input."));
        return;
    }
    const CreateAvdInfo avdInfo{si->sdkStylePath(), si->apiLevel(), m_settings.name(),
                                m_settings.abi.stringValue(), deviceDefinition(),
                                int(m_settings.sdcardSize())};

    struct Progress {
        Progress() {
            progressDialog.reset(createProgressDialog(0, Tr::tr("Create new AVD"),
                                                      Tr::tr("Creating new AVD device...")));
        }
        std::unique_ptr<QProgressDialog> progressDialog;
    };

    const Storage<Progress> progressStorage;

    const auto onCancelSetup = [progressStorage] {
        return makeObjectSignal(progressStorage->progressDialog.get(), &QProgressDialog::canceled);
    };

    const Storage<std::optional<QString>> errorStorage;

    const auto onDone = [errorStorage] {
        if (errorStorage->has_value()) {
            QMessageBox::warning(Core::ICore::dialogParent(), Tr::tr("Create new AVD"),
                                 errorStorage->value());
        }
    };

    const Group recipe {
        progressStorage,
        errorStorage,
        createAvdRecipe(errorStorage, avdInfo, m_settings.overwrite())
            .withCancel(onCancelSetup),
        onGroupDone(onDone, CallDoneFlag::OnError)
    };

    m_taskTreeRunner.start(recipe, {}, [this, avdInfo] {
        m_createdAvdInfo = avdInfo;
        updateAvdList();
        accept();
    }, CallDoneFlag::OnSuccess);
}

void AvdDialog::updateDeviceDefinitionComboBox()
{
    const DeviceType curDeviceType
        = m_deviceTypeToStringMap.key(m_settings.deviceType.stringValue());

    QStringList definitions;
    for (const DeviceDefinition &item : std::as_const(m_deviceDefinitionsList)) {
        if (item.deviceType == curDeviceType)
            definitions << item.name_id;
    }
    definitions << "Custom";

    m_settings.deviceDefinition.clearOptions();
    for (const QString &definition : std::as_const(definitions))
        m_settings.deviceDefinition.addOption(definition);
    updateApiLevelComboBox();
}

void AvdDialog::updateApiLevelComboBox()
{
    const SystemImageList installedSystemImages = sdkManager().installedSystemImages();
    const DeviceType curDeviceType
        = m_deviceTypeToStringMap.key(m_settings.deviceType.stringValue());
    const QString selectedAbi = m_settings.abi.stringValue();

    m_images = Utils::filtered(installedSystemImages, [&](const SystemImage *image) {
        if (!image || !image->isValid() || image->abiName() != selectedAbi)
            return false;
        return imageServesDeviceType(tagToDeviceType(image->sdkStylePath().split(';').at(2)),
                                     curDeviceType);
    });

    m_settings.targetApi.clearOptions();
    for (const SystemImage *image : std::as_const(m_images)) {
        m_settings.targetApi.addOption(apiLevelLabel(image->sdkStylePath(), image->apiLevel()),
                                       image->descriptionText());
    }

    const QString installRecommendationMsg
        = Tr::tr("Install a system image from the SDK Manager first.");

    if (installedSystemImages.isEmpty()) {
        m_settings.warning.setText(Tr::tr("No system images found.") + " "
                                   + installRecommendationMsg);
    } else if (m_images.isEmpty()) {
        m_settings.warning.setText(Tr::tr("No system images found for %1.").arg(selectedAbi) + " "
                                   + installRecommendationMsg);
    }
    m_settings.warning.setVisible(m_images.isEmpty());
    m_settings.targetApi.setEnabled(!m_images.isEmpty());
    updateOkButton();
}

std::optional<CreateAvdInfo> executeAvdCreatorDialog()
{
    AvdDialog dialog;
    if (dialog.exec() != QDialog::Accepted)
        return {};
    return dialog.avdInfo();
}

#ifdef WITH_TESTS

class AvdCreatorDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        AvdSettings settings;
        const Result<> rendered = Core::aspectFormRenders(&settings, "AvdCreatorDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhatTheDeviceListSays()
    {
        // The output of "avdmanager list device", as it is written in the
        // comment beside the parser. Blocks are separated by a line of
        // dashes, and the last one has no separator after it - which is why
        // an empty line ends a block too.
        const QString output = R"(Available devices definitions:
id: 0 or "automotive_1024p_landscape"
    Name: Automotive (1024p landscape)
    OEM : Google
    Tag : android-automotive-playstore
---------
id: 1 or "Galaxy Nexus"
    Name: Galaxy Nexus
    OEM : Google
---------
id: 2 or "desktop_large"
    Name: Large Desktop
    OEM : Google
    Tag : android-desktop
---------
id: 3 or "10.1in Tablet"
    Name: 10.1in Tablet
    OEM : Generic
---------
id: 4 or "wearos_small_round"
    Name: Wear OS Small Round
    OEM : Google
    Tag : android-wear
---------
)";

        const QList<DeviceDefinition> definitions = parseDeviceDefinitions(output);
        QCOMPARE(definitions.size(), 5);

        QCOMPARE(definitions.at(0).name_id, QString("automotive_1024p_landscape"));
        QCOMPARE(definitions.at(0).type_str, QString("android-automotive-playstore"));
        QCOMPARE(definitions.at(0).deviceType, DeviceType::Automotive);

        // No tag at all: a phone, unless the name says tablet.
        QCOMPARE(definitions.at(1).name_id, QString("Galaxy Nexus"));
        QVERIFY(definitions.at(1).type_str.isEmpty());
        QCOMPARE(definitions.at(1).deviceType, DeviceType::Phone);

        QCOMPARE(definitions.at(2).deviceType, DeviceType::Desktop);
        QCOMPARE(definitions.at(3).name_id, QString("10.1in Tablet"));
        QCOMPARE(definitions.at(3).deviceType, DeviceType::Tablet);
        QCOMPARE(definitions.at(4).deviceType, DeviceType::Wear);
    }

    void testAnIdWithOrInItIsStillOneId()
    {
        // The id line is split on the word "or", and everything after the
        // first one is the name - a name containing it must survive.
        const QString output = "id: 0 or \"Nexus or Something\"\n"
                               "    OEM : Google\n"
                               "---------\n";
        const QList<DeviceDefinition> definitions = parseDeviceDefinitions(output);
        QCOMPARE(definitions.size(), 1);
        QCOMPARE(definitions.first().name_id, QString("Nexus "));
    }

    void testWhichImagesADeviceKindAccepts()
    {
        // An image that names neither a phone nor a tablet serves both. The
        // widget dialog said this by widening a captured variable inside the
        // filter, so an image early in the list was judged against a different
        // answer than one after it.
        QVERIFY(imageServesDeviceType(DeviceType::PhoneOrTablet, DeviceType::Phone));
        QVERIFY(imageServesDeviceType(DeviceType::PhoneOrTablet, DeviceType::Tablet));
        QVERIFY(imageServesDeviceType(DeviceType::Wear, DeviceType::Wear));

        QVERIFY(!imageServesDeviceType(DeviceType::PhoneOrTablet, DeviceType::Wear));
        QVERIFY(!imageServesDeviceType(DeviceType::TV, DeviceType::Phone));
        QVERIFY(!imageServesDeviceType(DeviceType::Wear, DeviceType::TV));
    }

    void testWhatAnImageIsCalledInTheList()
    {
        QCOMPARE(apiLevelLabel("system-images;android-34;google_apis;arm64-v8a", 34),
                 QString("android-34 (google_apis)"));
        // A path with a different number of parts says only the level.
        QCOMPARE(apiLevelLabel("system-images;android-34;arm64-v8a", 34), QString("android-34"));
    }

    void testWhatAnAvdMayBeCalled()
    {
        AvdSettings settings;
        QVERIFY(avdNameIssue("Pixel_7-api34.x").isEmpty());
        QVERIFY(avdNameIssue("").isEmpty());

        // The characters the emulator refuses. The widget field swallowed the
        // keystroke and popped a tip; the reader is told instead.
        QVERIFY2(!avdNameIssue("my avd").isEmpty(), "a space is accepted in an AVD name");
        QVERIFY(!avdNameIssue("avd/one").isEmpty());
        QVERIFY(!avdNameIssue("avd:one").isEmpty());

        // Empty is not an error to say out loud, but it is not a name either.
        settings.name.setValue("");
        QVERIFY(!settings.nameIsUsable());
        settings.name.setValue("Pixel_7");
        QVERIFY(settings.nameIsUsable());
        settings.name.setValue("Pixel 7");
        QVERIFY2(!settings.nameIsUsable(), "a name the emulator refuses would have been offered");
    }

    void testTheSdCardStartsAtWhatTheDialogAlwaysOffered()
    {
        AvdSettings settings;
        QCOMPARE(settings.sdcardSize(), 512);
        QCOMPARE(settings.sdcardSize.presentation().minimum.toInt(), 0);
        QCOMPARE(settings.sdcardSize.presentation().maximum.toInt(), 1000000);
    }

    void testTheWarningIsOutOfTheWayUntilThereIsOne()
    {
        AvdSettings settings;
        QVERIFY2(!settings.warning.isVisible(),
                 "the dialog opens with an empty warning line in it");
        QCOMPARE(settings.warning.presentation().infoType, InfoType::Warning);
    }
};

QObject *createAvdCreatorDialogTest()
{
    return new AvdCreatorDialogTest;
}

#endif // WITH_TESTS

} // Android::Internal

#include "avdcreatordialog.moc"
