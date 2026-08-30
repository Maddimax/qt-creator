// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "androidconfigurations.h"
#include "androidsdkmanager.h"
#include "androidsdkmanagerdialog.h"
#include "androidtr.h"
#include "androidutils.h"

#include <coreplugin/icore.h>
#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/algorithm.h>
#include <utils/environment.h>
#include <utils/fancylineedit.h>
#include <utils/layoutbuilder.h>
#include <utils/qtcprocess.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSortFilterProxyModel>
#include <QTreeView>

using namespace Utils;
using namespace std::placeholders;

namespace Android::Internal {

class AndroidSdkModel : public QAbstractItemModel
{
public:
    enum PackageColumn {
        packageNameColumn = 0,
        apiLevelColumn,
        packageRevisionColumn
    };

    enum ExtraRoles {
        PackageTypeRole = Qt::UserRole + 1,
        PackageStateRole
    };

    AndroidSdkModel()
    {
        connect(&sdkManager(), &AndroidSdkManager::packagesReloaded,
                this, &AndroidSdkModel::refreshData);
        refreshData();
    }

    // QAbstractItemModel overrides.
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &) const override { return 3; }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override
    {
        static QHash<int, QByteArray> roles{{PackageTypeRole, "PackageRole"},
                                            {PackageStateRole, "PackageState"}};
        return roles;
    }
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

    void resetSelection()
    {
        beginResetModel();
        m_changeState.clear();
        endResetModel();
    }
    InstallationChange installationChange() const;

private:
    void refreshData();

    QList<const SdkPlatform *> m_sdkPlatforms;
    QList<const AndroidSdkPackage *> m_tools;
    QSet<const AndroidSdkPackage *> m_changeState;
};

QVariant AndroidSdkModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    Q_UNUSED(orientation)
    QVariant data;
    if (role == Qt::DisplayRole) {
        switch (section) {
        case packageNameColumn:
            data = Tr::tr("Package");
            break;
        case packageRevisionColumn:
            data = Tr::tr("Revision");
            break;
        case apiLevelColumn:
            data = Tr::tr("API");
            break;
        default:
            break;
        }
    }
    return data;
}

QModelIndex AndroidSdkModel::index(int row, int column, const QModelIndex &parent) const
{
    if (parent.isValid()) {
        // Packages under top items.
        if (parent.row() == 0) {
            // Tools packages
            if (row < m_tools.count())
                return createIndex(row, column, const_cast<AndroidSdkPackage *>(m_tools.at(row)));
        } else if (parent.row() < m_sdkPlatforms.count() + 1) {
            // Platform packages
            const SdkPlatform *sdkPlatform = m_sdkPlatforms.at(parent.row() - 1);
            SystemImageList images = sdkPlatform->systemImages(AndroidSdkPackage::AnyValidState);
            if (row < images.count() + 1) {
                if (row == 0)
                    return createIndex(row, column, const_cast<SdkPlatform *>(sdkPlatform));
                else
                    return createIndex(row, column, images.at(row - 1));
            }
        }
    } else if (row < m_sdkPlatforms.count() + 1) {
        return createIndex(row, column); // Top level items (Tools & platform)
    }

    return {};
}

QModelIndex AndroidSdkModel::parent(const QModelIndex &index) const
{
    void *ip = index.internalPointer();
    if (!ip)
        return {};

    auto package = static_cast<const AndroidSdkPackage *>(ip);
    if (package->type() == AndroidSdkPackage::SystemImagePackage) {
        auto image = static_cast<const SystemImage *>(package);
        int row = m_sdkPlatforms.indexOf(const_cast<SdkPlatform *>(image->platform()));
        if (row > -1)
            return createIndex(row + 1, 0);
    } else if (package->type() == AndroidSdkPackage::SdkPlatformPackage) {
        int row = m_sdkPlatforms.indexOf(static_cast<const SdkPlatform *>(package));
        if (row > -1)
            return createIndex(row + 1, 0);
    } else {
        return createIndex(0, 0); // Tools
    }

    return {};
}

int AndroidSdkModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return m_sdkPlatforms.count() + 1;

    if (!parent.internalPointer()) {
        if (parent.row() == 0) // Tools
            return m_tools.count();

        if (parent.row() <= m_sdkPlatforms.count()) {
            const SdkPlatform * platform = m_sdkPlatforms.at(parent.row() - 1);
            return platform->systemImages(AndroidSdkPackage::AnyValidState).count() + 1;
        }
    }

    return 0;
}

QVariant AndroidSdkModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    if (!index.parent().isValid()) {
        // Top level tools
        if (index.row() == 0) {
            return role == Qt::DisplayRole && index.column() == packageNameColumn ?
                       QVariant(Tr::tr("Tools")) : QVariant();
        }
        // Top level platforms
        const SdkPlatform *platform = m_sdkPlatforms.at(index.row() - 1);
        if (role == Qt::DisplayRole) {
            if (index.column() == packageNameColumn) {
                const QString androidName = androidNameForApiLevel(platform->apiLevel())
                + platform->extension();
                if (androidName.startsWith("Android"))
                    return androidName;
                else
                    return platform->displayText();
            } else if (index.column() == apiLevelColumn) {
                return platform->apiLevel();
            }
        }
        return {};
    }

    auto p = static_cast<const AndroidSdkPackage *>(index.internalPointer());
    QString apiLevelStr;
    if (p->type() == AndroidSdkPackage::SdkPlatformPackage)
        apiLevelStr = QString::number(static_cast<const SdkPlatform *>(p)->apiLevel());

    if (p->type() == AndroidSdkPackage::SystemImagePackage)
        apiLevelStr = QString::number(static_cast<const SystemImage *>(p)->platform()->apiLevel());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case packageNameColumn:
            return p->type() == AndroidSdkPackage::SdkPlatformPackage ?
                       Tr::tr("SDK Platform") : p->displayText();
        case packageRevisionColumn:
            return p->revision().toString();
        case apiLevelColumn:
            return apiLevelStr;
        default:
            break;
        }
    }

    if (index.column() == packageNameColumn) {
        if (role == Qt::CheckStateRole) {
            if (p->state() == AndroidSdkPackage::Installed)
                return m_changeState.contains(p) ? Qt::Unchecked : Qt::Checked;
            else
                return m_changeState.contains(p) ? Qt::Checked : Qt::Unchecked;
        }

        if (role == Qt::FontRole) {
            QFont font;
            if (m_changeState.contains(p))
                font.setBold(true);
            return font;
        }
    }

    if (role == Qt::TextAlignmentRole && index.column() == packageRevisionColumn)
        return Qt::AlignRight;

    if (role == Qt::ToolTipRole)
        return QString("%1 - (%2)").arg(p->descriptionText()).arg(p->sdkStylePath());

    if (role == PackageTypeRole)
        return p->type();

    if (role == PackageStateRole)
        return p->state();

    return {};
}

Qt::ItemFlags AndroidSdkModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractItemModel::flags(index);
    if (index.column() == packageNameColumn)
        f |= Qt::ItemIsUserCheckable;

    void *ip = index.internalPointer();
    if (ip && index.column() == packageNameColumn) {
        auto package = static_cast<const AndroidSdkPackage *>(ip);
        if (package->state() == AndroidSdkPackage::Installed
            && package->type() == AndroidSdkPackage::SdkToolsPackage) {
            f &= ~Qt::ItemIsEnabled;
        }
    }
    return f;
}

bool AndroidSdkModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    void *ip = index.internalPointer();
    if (ip && role == Qt::CheckStateRole) {
        auto package = static_cast<const AndroidSdkPackage *>(ip);
        if (value.toInt() == Qt::Checked && package->state() != AndroidSdkPackage::Installed) {
            m_changeState << package;
            emit dataChanged(index, index, {Qt::CheckStateRole});
        } else if (m_changeState.remove(package)) {
            emit dataChanged(index, index, {Qt::CheckStateRole});
        } else if (value.toInt() == Qt::Unchecked) {
            m_changeState.insert(package);
            emit dataChanged(index, index, {Qt::CheckStateRole});
        }
        return true;
    }
    return false;
}

InstallationChange AndroidSdkModel::installationChange() const
{
    if (m_changeState.isEmpty())
        return {};

    InstallationChange change;
    for (const AndroidSdkPackage *package : m_changeState) {
        if (package->state() == AndroidSdkPackage::Installed)
            change.toUninstall << package->sdkStylePath();
        else
            change.toInstall << package->sdkStylePath();
    }
    return change;
}

void AndroidSdkModel::refreshData()
{
    m_sdkPlatforms.clear();
    m_tools.clear();
    m_changeState.clear();
    beginResetModel();
    for (AndroidSdkPackage *p : sdkManager().allSdkPackages()) {
        if (p->type() == AndroidSdkPackage::SdkPlatformPackage)
            m_sdkPlatforms << static_cast<SdkPlatform *>(p);
        else
            m_tools << p;
    }
    Utils::sort(m_sdkPlatforms, [](const SdkPlatform *p1, const SdkPlatform *p2) {
        return p1->apiLevel() > p2->apiLevel();
    });

    Utils::sort(m_tools, [](const AndroidSdkPackage *p1, const AndroidSdkPackage *p2) {
        if (p1->state() == p2->state())
            return p1->type() == p2->type() ? p1->revision() > p2->revision() : p1->type() > p2->type();
        else
            return p1->state() < p2->state();
    });
    endResetModel();
}

// The part of "sdkmanager --help" worth showing: everything after the
// "Common Arguments:" heading. The rest of the output is about the command
// that was run, not about what may be passed to it.
//
// Kept out of the dialog because it is a question about that output, and there
// the only way to ask it was to have an Android SDK installed and open the
// dialog.
QString sdkManagerArgumentHelp(const QString &helpOutput)
{
    const int tagIndex = helpOutput.indexOf("Common Arguments:");
    if (tagIndex < 0)
        return {};

    const int detailsIndex = helpOutput.indexOf('\n', tagIndex);
    if (detailsIndex < 0)
        return {};

    return helpOutput.mid(detailsIndex + 1);
}

// What was typed, as arguments. Nothing typed is no arguments rather than one
// empty one, which sdkmanager would refuse.
QStringList sdkManagerArgumentsFrom(const QString &text)
{
    const QString simplified = text.simplified();
    return simplified.isEmpty() ? QStringList() : simplified.split(' ');
}

class SdkManagerOptions final : public AspectContainer
{
public:
    SdkManagerOptions()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Android/SdkManagerOptionsDialog.qml"));

        arguments.setQmlName("Arguments");
        arguments.setLabelText(Tr::tr("SDK manager arguments:"));
        arguments.setDisplayStyle(StringAspect::LineEditDisplay);
        arguments.setValue(AndroidConfig::sdkManagerToolArgs().join(" "));

        // Read from, never typed into: it is what sdkmanager says about
        // itself.
        help.setQmlName("Help");
        help.setLabelText(Tr::tr("Available arguments:"));
        help.setDisplayStyle(StringAspect::TextEditDisplay);
        help.setReadOnly(true);
    }

    QStringList sdkManagerArguments() const { return sdkManagerArgumentsFrom(arguments()); }

    StringAspect arguments{this};
    StringAspect help{this};
};

class OptionsDialog : public QDialog
{
public:
    OptionsDialog(QWidget *parent)
        : QDialog(parent)
    {
        resize(800, 480);
        setWindowTitle(Tr::tr("SDK Manager Arguments"));

        const auto dialogButtons
            = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
        connect(dialogButtons, &QDialogButtonBox::accepted, this, &OptionsDialog::accept);
        connect(dialogButtons, &QDialogButtonBox::rejected, this, &OptionsDialog::reject);

        const auto layout = new QVBoxLayout(this);
        layout->addWidget(Core::createAspectForm(&m_settings));
        layout->addWidget(dialogButtons);

        m_process.setEnvironment(AndroidConfig::toolsEnvironment());
        m_process.setCommand(
            {AndroidConfig::sdkManagerToolPath(),
             {"--help", "--sdk_root=" + AndroidConfig::sdkLocation().path()}});
        connect(&m_process, &Process::done, this, [this] {
            const QString shown = sdkManagerArgumentHelp(m_process.allOutput());
            m_settings.help.setValue(
                shown.isEmpty()
                    ? Tr::tr("Cannot load available arguments for \"sdkmanager\" command.")
                    : shown);
        });
        m_process.start();
    }

    QStringList sdkManagerArguments() const { return m_settings.sdkManagerArguments(); }

private:
    SdkManagerOptions m_settings;
    Process m_process;
};

class PackageFilterModel final : public QSortFilterProxyModel
{
public:
    explicit PackageFilterModel(QAbstractItemModel *sdkModel)
        : QSortFilterProxyModel(sdkModel)
    {
        setSourceModel(sdkModel);
    }

    void setAcceptedPackageState(AndroidSdkPackage::PackageState state)
    {
        m_packageState = state;
        invalidateFilter();
    }

    void setAcceptedSearchPackage(const QString &name)
    {
        m_searchText = name;
        invalidateFilter();
    }

    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const final
    {
        QModelIndex srcIndex = sourceModel()->index(sourceRow, 0, sourceParent);
        if (!srcIndex.isValid())
            return false;

        auto packageState = [](const QModelIndex& i) {
            return (AndroidSdkPackage::PackageState)i.data(AndroidSdkModel::PackageStateRole).toInt();
        };

        auto packageFound = [this](const QModelIndex& i) {
            return i.data(AndroidSdkModel::packageNameColumn).toString()
            .contains(m_searchText, Qt::CaseInsensitive);
        };

        bool showTopLevel = false;
        if (!sourceParent.isValid()) {
            // Top Level items
            for (int row = 0; row < sourceModel()->rowCount(srcIndex); ++row) {
                QModelIndex childIndex = sourceModel()->index(row, 0, srcIndex);
                if ((m_packageState & packageState(childIndex) && packageFound(childIndex))) {
                    showTopLevel = true;
                    break;
                }
            }
        }

        return showTopLevel || ((packageState(srcIndex) & m_packageState) && packageFound(srcIndex));
    }

private:
    AndroidSdkPackage::PackageState m_packageState = AndroidSdkPackage::AnyValidState;
    QString m_searchText;
};

class AndroidSdkManagerDialog final : public QDialog
{
public:
    AndroidSdkManagerDialog();

private:
    AndroidSdkModel m_sdkModel;
};

AndroidSdkManagerDialog::AndroidSdkManagerDialog()
    : QDialog(Core::ICore::dialogParent())
{
    setWindowTitle(Tr::tr("Android SDK Manager"));
    resize(664, 396);
    setModal(true);

    auto packagesView = new QTreeView;
    packagesView->setIndentation(20);
    packagesView->header()->setCascadingSectionResizes(false);

    auto updateInstalledButton = new QPushButton(Tr::tr("Update Installed"));

    auto channelCheckbox = new QComboBox;
    channelCheckbox->addItem(Tr::tr("Default"));
    channelCheckbox->addItem(Tr::tr("Stable"));
    channelCheckbox->addItem(Tr::tr("Beta"));
    channelCheckbox->addItem(Tr::tr("Dev"));
    channelCheckbox->addItem(Tr::tr("Canary"));

    auto obsoleteCheckBox = new QCheckBox(Tr::tr("Include obsolete"));

    auto showAvailableRadio = new QRadioButton(Tr::tr("Available"));
    auto showInstalledRadio = new QRadioButton(Tr::tr("Installed"));
    auto showAllRadio = new QRadioButton(Tr::tr("All", "Show all packages"));
    showAllRadio->setChecked(true);

    auto optionsButton = new QPushButton(Tr::tr("Advanced Options..."));

    auto searchField = new FancyLineEdit;
    searchField->setPlaceholderText("Filter");

    auto expandCheck = new QCheckBox(Tr::tr("Expand All"));

    auto buttonBox = new QDialogButtonBox;
    buttonBox->setStandardButtons(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
    buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);

    auto proxyModel = new PackageFilterModel(&m_sdkModel);
    packagesView->setModel(proxyModel);
    packagesView->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    packagesView->header()->setSectionResizeMode(AndroidSdkModel::packageNameColumn,
                                                       QHeaderView::Stretch);
    packagesView->header()->setStretchLastSection(false);

    using namespace Layouting;
    Column {
        Grid {
            Row {searchField, expandCheck}, br,
            packagesView,
            Column {
                updateInstalledButton,
                st,
                Group {
                    title(Tr::tr("Show Packages")),
                    Column {
                        Row { Tr::tr("Channel:"), channelCheckbox },
                        obsoleteCheckBox,
                        hr,
                        showAvailableRadio,
                        showInstalledRadio,
                        showAllRadio,
                    }
                },
                optionsButton,
            }, br,
        },
        buttonBox,
    }.attachTo(this);

    const auto updateApplyButton = [this, buttonBox] {
        buttonBox->button(QDialogButtonBox::Apply)->setEnabled(m_sdkModel.installationChange().count());
    };
    connect(&m_sdkModel, &AndroidSdkModel::modelReset, this, updateApplyButton);
    connect(&m_sdkModel, &AndroidSdkModel::dataChanged, this, updateApplyButton);

    connect(expandCheck, &QCheckBox::stateChanged, this, [packagesView](int state) {
        if (state == Qt::Checked)
            packagesView->expandAll();
        else
            packagesView->collapseAll();
    });
    connect(updateInstalledButton, &QPushButton::clicked,
            &sdkManager(), &AndroidSdkManager::runUpdate);
    connect(showAllRadio, &QRadioButton::toggled, this, [this, proxyModel](bool checked) {
        if (checked) {
            proxyModel->setAcceptedPackageState(AndroidSdkPackage::AnyValidState);
            m_sdkModel.resetSelection();
        }
    });
    connect(showInstalledRadio, &QRadioButton::toggled, this, [this, proxyModel](bool checked) {
        if (checked) {
            proxyModel->setAcceptedPackageState(AndroidSdkPackage::Installed);
            m_sdkModel.resetSelection();
        }
    });
    connect(showAvailableRadio, &QRadioButton::toggled, this, [this, proxyModel](bool checked) {
        if (checked) {
            proxyModel->setAcceptedPackageState(AndroidSdkPackage::Available);
            m_sdkModel.resetSelection();
        }
    });

    connect(searchField, &QLineEdit::textChanged,
            this, [this, proxyModel, expandCheck](const QString &text) {
        proxyModel->setAcceptedSearchPackage(text);
        m_sdkModel.resetSelection();
        // It is more convenient to expand the view with the results
        expandCheck->setChecked(!text.isEmpty());
    });

    connect(buttonBox->button(QDialogButtonBox::Apply), &QAbstractButton::clicked, this, [this] {
        sdkManager().runInstallationChange(m_sdkModel.installationChange());
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &AndroidSdkManagerDialog::reject);

    connect(optionsButton, &QPushButton::clicked, this, [this] {
        OptionsDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            QStringList arguments = dlg.sdkManagerArguments();
            if (arguments != AndroidConfig::sdkManagerToolArgs()) {
                AndroidConfig::setSdkManagerToolArgs(arguments);
                sdkManager().reloadPackages();
            }
        }
    });

    connect(obsoleteCheckBox, &QCheckBox::stateChanged, this, [](int state) {
        const QString obsoleteArg = "--include_obsolete";
        QStringList args = AndroidConfig::sdkManagerToolArgs();
        if (state == Qt::Checked && !args.contains(obsoleteArg)) {
            args.append(obsoleteArg);
            AndroidConfig::setSdkManagerToolArgs(args);
        } else if (state == Qt::Unchecked && args.contains(obsoleteArg)) {
            args.removeAll(obsoleteArg);
            AndroidConfig::setSdkManagerToolArgs(args);
        }
        sdkManager().reloadPackages();
    });

    connect(channelCheckbox, &QComboBox::currentIndexChanged, this, [](int index) {
        QStringList args = AndroidConfig::sdkManagerToolArgs();
        QString existingArg;
        for (int i = 0; i < 4; ++i) {
            const QString arg = "--channel=" + QString::number(i);
            if (args.contains(arg)) {
                existingArg = arg;
                break;
            }
        }

        if (index == 0 && !existingArg.isEmpty()) {
            args.removeAll(existingArg);
            AndroidConfig::setSdkManagerToolArgs(args);
        } else if (index > 0) {
            // Add 1 to account for Stable (second item) being channel 0
            const QString channelArg = "--channel=" + QString::number(index - 1);
            if (existingArg != channelArg) {
                if (!existingArg.isEmpty()) {
                    args.removeAll(existingArg);
                    AndroidConfig::setSdkManagerToolArgs(args);
                }
                args.append(channelArg);
                AndroidConfig::setSdkManagerToolArgs(args);
            }
        }
        sdkManager().reloadPackages();
    });
}

void executeAndroidSdkManagerDialog()
{
    AndroidSdkManagerDialog().exec();
}

#ifdef WITH_TESTS

class SdkManagerOptionsTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        // This is the first read-only multi-line report drawn as an aspect
        // rather than as a QPlainTextEdit, so that it renders at all is the
        // thing being checked.
        SdkManagerOptions settings;
        const Utils::Result<> rendered
            = Core::aspectFormRenders(&settings, "SdkManagerOptionsDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));

        const Utils::AspectPresentation presentation = settings.help.presentation();
        QCOMPARE(presentation.control, Utils::AspectControls::TextEdit);

        // And that the *drawn* area cannot be typed into. Asserting
        // help.isReadOnly() would only be reading back the setter two lines
        // above; what matters is whether it reached the text area.
        const std::unique_ptr<QWidget> form(Core::createAspectForm(&settings));
        QVERIFY(form);
        QObject * const root = Core::aspectFormRoot(form.get());
        QVERIFY2(root, "no front end said what the form was drawn from");

        QObject *area = nullptr;
        QTRY_VERIFY(area = root->findChild<QObject *>("textArea"));
        QVERIFY2(area->property("readOnly").toBool(), "the report could be typed into");
    }

    void testWhichPartOfTheHelpIsWorthShowing()
    {
        const QString output = "Usage: sdkmanager [options]\n"
                               "Blah about the command itself\n"
                               "Common Arguments:\n"
                               "  --sdk_root=PATH\n"
                               "  --channel=N\n";

        // Everything after the heading, and not the heading itself - the lines
        // above it are about the command that was run.
        const QString shown = sdkManagerArgumentHelp(output);
        QVERIFY(shown.startsWith("  --sdk_root=PATH"));
        QVERIFY(shown.contains("--channel=N"));
        QVERIFY2(!shown.contains("Usage:"), "the part about the command itself was shown");
        QVERIFY2(!shown.contains("Common Arguments:"), "the heading was shown as an argument");

        // Output that does not have the heading answers nothing, which is what
        // lets the dialog say it could not load them.
        QVERIFY(sdkManagerArgumentHelp("something else entirely").isEmpty());
        QVERIFY(sdkManagerArgumentHelp({}).isEmpty());

        // The heading with nothing under it is also nothing.
        QVERIFY(sdkManagerArgumentHelp("Common Arguments:").isEmpty());
    }

    void testWhatIsTypedBecomesArguments()
    {
        QCOMPARE(sdkManagerArgumentsFrom("--verbose --channel=1"),
                 QStringList({"--verbose", "--channel=1"}));

        // Spacing is the reader's, not sdkmanager's.
        QCOMPARE(sdkManagerArgumentsFrom("  --verbose   --channel=1  "),
                 QStringList({"--verbose", "--channel=1"}));

        // Nothing typed is no arguments, not one empty one - sdkmanager
        // refuses an empty argument.
        QVERIFY(sdkManagerArgumentsFrom({}).isEmpty());
        QVERIFY2(sdkManagerArgumentsFrom("   ").isEmpty(),
                 "whitespace was passed as an argument");
    }
};

QObject *createSdkManagerOptionsTest()
{
    return new SdkManagerOptionsTest;
}

#endif // WITH_TESTS

} // Android::Internal

#ifdef WITH_TESTS
#include "androidsdkmanagerdialog.moc"
#endif
