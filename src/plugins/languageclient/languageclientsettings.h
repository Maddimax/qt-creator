// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "languageclient_global.h"
#include "mimetypesaspect.h"

#include <utils/layoutbuilder.h>

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QJsonObject>
#include <QPointer>
#include <QUuid>

QT_BEGIN_NAMESPACE
class QLineEdit;
QT_END_NAMESPACE

namespace Utils {
class CommandLine;
class FancyLineEdit;
class FilePath;
class PathChooser;
class QtcSettings;
} // namespace Utils

namespace Core { class IDocument; }

namespace ProjectExplorer {
class BuildConfiguration;
class Project;
}

namespace LanguageClient {

class Client;
class BaseClientInterface;

class LANGUAGECLIENT_EXPORT LanguageFilter
{
public:
    QStringList mimeTypes;
    QStringList filePattern;
    // mime types that are explicitly not supported, even if they inherit a supported mime type
    QStringList excludeMimeTypes;
    bool isSupported(const Utils::FilePath &filePath, const QString &mimeType) const;
    bool isSupported(const Core::IDocument *document) const;
    bool operator==(const LanguageFilter &other) const;
};

class LANGUAGECLIENT_EXPORT BaseSettings : public Utils::AspectContainer
{
public:
    BaseSettings();

    virtual ~BaseSettings() = default;

    enum StartBehavior {
        AlwaysOn = 0,
        RequiresFile,
        RequiresProject,
        LastSentinel
    };

    Utils::StringAspect name{this};
    MimeTypesAspect mimeTypes{this};
    MimeTypesAspect excludeMimeTypes{this};
    Utils::StringAspect filePattern{this};
    Utils::StringAspect id{this};
    Utils::IdAspect settingsTypeId{this};
    Utils::TypedSelectionAspect<StartBehavior> startBehavior{this};
    Utils::BoolAspect enabled{this};

    Utils::StringAspect initializationOptions{this};
    Utils::StringAspect configuration{this};
    Utils::BoolAspect showInSettings{this};
    // controls whether the resulting client can be used for completions/highlight/outline etc.
    Utils::BoolAspect activatable{this};

    QJsonObject initializationOptionsAsJson() const;
    QJsonValue configurationAsJson() const;

    // The rows a page shows for this client, in the order it wants them. This
    // is what createSettingsWidget() was: which aspects, and in what order.
    virtual void addSettingsRows(Utils::AspectContainer &rows);
    virtual bool applySettings();
    virtual BaseSettings *copy() const;
    virtual BaseSettings *create() const = 0;
    virtual bool isValid() const;
    virtual bool isValidOnBuildConfiguration(ProjectExplorer::BuildConfiguration *bc) const;
    // The settings this client adds to a project's Language Server panel, or
    // nothing where it adds none. A client says what it has rather than
    // drawing it, so that the panel can show it whichever way it is drawn.
    // Give the container a labelText: it becomes the group's title.
    virtual Utils::AspectContainer *projectSpecificSettings(ProjectExplorer::Project *) const
    {
        return nullptr;
    }
    Client *createClient() const;
    Client *createClient(ProjectExplorer::BuildConfiguration *bc) const;
    bool isEnabledOnProject(ProjectExplorer::Project *project) const;
    const LanguageFilter languageFilter() const;

protected:
    virtual BaseClientInterface *createInterface(ProjectExplorer::BuildConfiguration *) const = 0;
    virtual Client *createClient(BaseClientInterface *interface) const;

private:
    static constexpr char filterSeparator = ';';
};

class LANGUAGECLIENT_EXPORT StdIOSettings : public BaseSettings
{
public:
    StdIOSettings();
    ~StdIOSettings() override;

    void addSettingsRows(Utils::AspectContainer &rows) override;
    BaseSettings *create() const override { return new StdIOSettings; }
    bool isValid() const override;

    Utils::CommandLine command() const;

    Utils::StringAspect arguments{this};
    Utils::FilePathAspect executable{this};

protected:
    BaseClientInterface *createInterface(ProjectExplorer::BuildConfiguration *bc) const override;
};

struct ClientType {
    Utils::Id id;
    QString name;
    using SettingsGenerator = std::function<BaseSettings*()>;
    SettingsGenerator generator = nullptr;
    bool userAddable = true;
};

class LANGUAGECLIENT_EXPORT LanguageClientSettings
{
public:
    static void init();
    static bool initialized();

    static QList<BaseSettings *> fromSettings(Utils::QtcSettings *settings);
    static QList<BaseSettings *> pageSettings();
    static QList<BaseSettings *> changedSettings();
    static BaseSettings *settingById(QList<BaseSettings *> settings, const QString &id);

    static QList<Utils::Store> storesBySettingsType(Utils::Id settingsTypeId);

    /**
     * must be called before the delayed initialize phase
     * otherwise the settings are not loaded correctly
     */
    static void registerClientType(const ClientType &type);
    static void addSettings(BaseSettings *settings);
    static void enableSettings(const QString &id, bool enable = true);
    static void toSettings(Utils::QtcSettings *settings, const QList<BaseSettings *> &languageClientSettings);

    static bool outlineComboBoxIsSorted();
    static void setOutlineComboBoxSorted(bool sorted);
};

class ProjectSettings
{
public:
    explicit ProjectSettings(ProjectExplorer::Project *project);

    QJsonValue workspaceConfiguration() const;

    QByteArray json() const;
    void setJson(const QByteArray &json);

    void enableSetting(const QString &id);
    void disableSetting(const QString &id);
    void clearOverride(const QString &id);

    QStringList enabledSettings();
    QStringList disabledSettings();

private:
    ProjectExplorer::Project *m_project = nullptr;
    QByteArray m_json;
    QStringList m_enabledSettings;
    QStringList m_disabledSettings;
};

void setupLanguageClientProjectPanel();

#ifdef WITH_TESTS
QObject *createLanguageClientSettingsPageTest();
#endif

} // namespace LanguageClient
