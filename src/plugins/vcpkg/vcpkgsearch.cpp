// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "vcpkgsearch.h"

#include "qpushbutton.h"
#include "vcpkgsettings.h"
#include "vcpkgtr.h"

#include <projectexplorer/projecttree.h>

#include <solutions/spinner/spinner.h>
#include <QtTaskTree/QTaskTree>
#include <QtTaskTree/QSingleTaskTreeRunner>

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/algorithm.h>
#include <utils/async.h>
#include <utils/fancylineedit.h>
#include <utils/infolabel.h>
#include <utils/layoutbuilder.h>

#include <coreplugin/icore.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QTextBrowser>

using namespace ProjectExplorer;
using namespace QtTaskTree;
using namespace Utils;

namespace Vcpkg::Internal::Search {

static void vcpkgManifests(QPromise<VcpkgManifest> &promise, const FilePath &vcpkgRoot)
{
    const FilePath portsDir = vcpkgRoot / "ports";
    const FilePaths manifestFiles =
        portsDir.dirEntries({{"vcpkg.json"}, DirFilterFlag::Files, DirIteratorFlag::Subdirectories});
    for (const FilePath &manifestFile : manifestFiles) {
        if (promise.isCanceled())
            return;
        if (const Result<QByteArray> res = manifestFile.fileContents())
            promise.addResult(parseVcpkgManifest(*res));
    }
}

QStringList packageNamesMatching(const QList<VcpkgManifest> &packages, const QString &filter)
{
    const QList<VcpkgManifest> matching
        = filtered(packages, [&filter](const VcpkgManifest &package) {
              return filter.isEmpty()
                     || package.name.contains(filter, Qt::CaseInsensitive)
                     || package.shortDescription.contains(filter, Qt::CaseInsensitive)
                     || Utils::anyOf(package.description, [&filter](const QString &paragraph) {
                            return paragraph.contains(filter, Qt::CaseInsensitive);
                        });
          });
    QStringList names = transform(matching, [](const VcpkgManifest &package) {
        return package.name;
    });
    names.sort();
    return names;
}

QString packageDescriptionHtml(const VcpkgManifest &manifest)
{
    QString description = manifest.shortDescription;
    if (!manifest.description.isEmpty())
        description.append("<p>" + manifest.description.join("</p><p>") + "</p>");
    return description;
}

bool canAddPackage(const QString &package, bool isProjectDependency)
{
    return !package.isEmpty() && !isProjectDependency;
}

// The package names, in order. A package is found by what it says about itself
// as well as by its name, so the row answers FilterTextRole with all of it -
// which is what the table's own filter field then searches.
class PackageModel : public QAbstractListModel
{
public:
    void setPackages(const QList<VcpkgManifest> &packages)
    {
        beginResetModel();
        m_packages = packages;
        Utils::sort(m_packages, [](const VcpkgManifest &a, const VcpkgManifest &b) {
            return a.name < b.name;
        });
        endResetModel();
    }

    VcpkgManifest at(int row) const
    {
        return row >= 0 && row < m_packages.size() ? m_packages.at(row) : VcpkgManifest();
    }

    int rowCount(const QModelIndex &parent) const override
    {
        return parent.isValid() ? 0 : m_packages.size();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_packages.size())
            return {};
        const VcpkgManifest &package = m_packages.at(index.row());
        switch (role) {
        case Qt::DisplayRole:
            return package.name;
        case AspectTable::FilterTextRole:
            return QString(QStringList{package.name, package.shortDescription}.join(' ')
                           + ' ' + package.description.join(' '));
        case AspectTable::EditableRole:
            return false;
        default:
            return {};
        }
    }

    // One unnamed column: a table draws a header saying "1" for a model that
    // answers the column number.
    QVariant headerData(int, Qt::Orientation, int) const override { return {}; }

    QHash<int, QByteArray> roleNames() const override
    {
        return AspectTable::withRoleNames(QAbstractListModel::roleNames());
    }

private:
    QList<VcpkgManifest> m_packages;
};

class VcpkgSearchSettings final : public AspectContainer
{
public:
    VcpkgSearchSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Vcpkg/VcpkgPackageSearchDialog.qml"));

        packages.setQmlName("Packages");
        packages.setLabelText(Tr::tr("Packages:"));
        packages.setModel(&model);
        packages.setFilterPlaceholderText(Tr::tr("Filter"));

        name.setQmlName("Name");
        name.setLabelText(Tr::tr("Name:"));
        version.setQmlName("Version");
        version.setLabelText(Tr::tr("Version:"));
        license.setQmlName("License");
        license.setLabelText(Tr::tr("License:"));

        description.setQmlName("Description");
        description.setLabelText(Tr::tr("Description:"));
        description.setTextFormat(AspectControls::TextFormat::RichText);

        homepage.setQmlName("Homepage");
        homepage.setLabelText(Tr::tr("Homepage:"));
        homepage.setTextFormat(AspectControls::TextFormat::RichText);

        alreadyADependency.setQmlName("AlreadyADependency");
        alreadyADependency.setText(Tr::tr("This package is already a project dependency."));
        alreadyADependency.setIconType(Utils::InfoType::Information);
        alreadyADependency.setVisible(false);

        // The manifests are read off the disk, which takes a while.
        loading.setQmlName("Loading");
        loading.setValue(false);
    }

    PackageModel model;
    TableAspect packages{this};
    TextDisplay name{this};
    TextDisplay version{this};
    TextDisplay license{this};
    TextDisplay description{this};
    TextDisplay homepage{this};
    TextDisplay alreadyADependency{this};
    BoolAspect loading{this};
};

class VcpkgPackageSearchDialog : public QDialog
{
public:
    explicit VcpkgPackageSearchDialog(const VcpkgManifest &preexistingPackages, QWidget *parent);
    ~VcpkgPackageSearchDialog() override;

    VcpkgManifest selectedPackage() const { return m_selectedPackage; }

private:
    void showPackageDetails();
    void updateStatus();
    void updatePackages();

    QList<VcpkgManifest> m_allPackages;
    VcpkgManifest m_selectedPackage;
    const VcpkgManifest m_projectManifest;

    const std::unique_ptr<VcpkgSearchSettings> m_settings;
    QDialogButtonBox *m_buttonBox;
    QSingleTaskTreeRunner m_taskTreeRunner;

#ifdef WITH_TESTS
    friend class VcpkgSearchDialogTest;
#endif
};

VcpkgPackageSearchDialog::VcpkgPackageSearchDialog(const VcpkgManifest &preexistingPackages,
                                                   QWidget *parent)
    : QDialog(parent)
    , m_projectManifest(preexistingPackages)
    , m_settings(new VcpkgSearchSettings)
    , m_buttonBox(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
{
    resize(920, 400);
    setWindowTitle(Tr::tr("Add vcpkg Package"));

    auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(m_buttonBox);

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(&m_settings->packages, &TableAspect::chosenChanged,
            this, &VcpkgPackageSearchDialog::showPackageDetails);

    updateStatus();
    updatePackages();
}

VcpkgPackageSearchDialog::~VcpkgPackageSearchDialog() = default;

void VcpkgPackageSearchDialog::showPackageDetails()
{
    const VcpkgManifest manifest = m_settings->model.at(m_settings->packages.currentRow());

    m_settings->name.setText(manifest.name);
    m_settings->version.setText(manifest.version);
    m_settings->license.setText(manifest.license);
    m_settings->description.setText(packageDescriptionHtml(manifest));
    m_settings->homepage.setText(QString::fromLatin1("<a href=\"%1\">%1</a>")
                                     .arg(manifest.homepage.toDisplayString()));

    m_selectedPackage = manifest;
    updateStatus();
}

void VcpkgPackageSearchDialog::updateStatus()
{
    const QString package = selectedPackage().name;
    const bool isProjectDependency = m_projectManifest.dependencies.contains(package);
    m_settings->alreadyADependency.setVisible(isProjectDependency);
    m_buttonBox->button(QDialogButtonBox::Ok)
        ->setEnabled(canAddPackage(package, isProjectDependency));
}

void VcpkgPackageSearchDialog::updatePackages()
{
    using namespace QtTaskTree;

    const Group recipe {
        onGroupSetup([this] { m_settings->loading.setValue(true); }),
        AsyncTask<VcpkgManifest>{
            [](Async<VcpkgManifest> &task) {
                FilePath vcpkgRoot =
                    vcpkgSettingsForProject(ProjectTree::currentProject())->vcpkgRoot.expandedValue();
                task.setConcurrentCallData(vcpkgManifests, vcpkgRoot);
            },
            [this](const Async<VcpkgManifest> &task) { m_allPackages = task.results(); }
        },
        onGroupDone([this] {
            m_settings->loading.setValue(false);
            m_settings->model.setPackages(m_allPackages);
            updateStatus();
        }),
    };
    m_taskTreeRunner.start(recipe);
}

VcpkgManifest parseVcpkgManifest(const QByteArray &vcpkgManifestJsonData, bool *ok)
{
    // https://learn.microsoft.com/en-us/vcpkg/reference/vcpkg-json
    VcpkgManifest result;
    const QJsonObject jsonObject = QJsonDocument::fromJson(vcpkgManifestJsonData).object();
    if (const QJsonValue name = jsonObject.value("name"); !name.isUndefined())
        result.name = name.toString();
    for (const char *key : {"version", "version-semver", "version-date", "version-string"} ) {
        if (const QJsonValue ver = jsonObject.value(QLatin1String(key)); !ver.isUndefined()) {
            result.version = ver.toString();
            break;
        }
    }
    if (const QJsonValue license = jsonObject.value("license"); !license.isUndefined())
        result.license = license.toString();
    if (const QJsonValue deps = jsonObject.value("dependencies"); !deps.isUndefined()) {
        const QJsonArray dependencies = deps.toArray();
        for (const QJsonValue &dependency : dependencies) {
            if (dependency.isString())
                result.dependencies.append(dependency.toString());
            else if (const QJsonValue name = dependency.toObject().value("name"); name.isString())
                result.dependencies.append(name.toString());
        }
    }
    if (const QJsonValue description = jsonObject.value("description"); !description.isUndefined()) {
        if (description.isArray()) {
            const QJsonArray descriptionLines = description.toArray();
            for (const QJsonValue &val : descriptionLines) {
                const QString line = val.toString();
                if (result.shortDescription.isEmpty()) {
                    result.shortDescription = line;
                    continue;
                }
                result.description.append(line);
            }
        } else {
            result.shortDescription = description.toString();
        }
    }
    if (const QJsonValue homepage = jsonObject.value("homepage"); !homepage.isUndefined())
        result.homepage = QUrl::fromUserInput(homepage.toString());

    if (ok)
        *ok = !(result.name.isEmpty() || result.version.isEmpty());

    return result;
}

VcpkgManifest showVcpkgPackageSearchDialog(const VcpkgManifest &projectManifest, QWidget *parent)
{
    QWidget *dlgParent = parent ? parent : Core::ICore::dialogParent();
    VcpkgPackageSearchDialog dlg(projectManifest, dlgParent);
    const VcpkgManifest result = (dlg.exec() == QDialog::Accepted) ? dlg.selectedPackage()
                                                                   : VcpkgManifest();
    return result;
}

} // namespace Vcpkg::Internal::Search
