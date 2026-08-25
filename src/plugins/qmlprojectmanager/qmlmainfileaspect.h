// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qmlprojectmanager_global.h"

#include <utils/aspects.h>

namespace Core {
class IEditor;
}

namespace QmlProjectManager {

// The entries a main-file chooser offers, as the ids it stores: the file in
// the editor first, then every .qml in \a projectFiles, relative to
// \a projectDir and sorted the way a reader would. Free so that it can be
// checked against a list of paths rather than a project.
QMLPROJECTMANAGER_EXPORT QStringList mainFileChoices(const Utils::FilePaths &projectFiles,
                                                     const Utils::FilePath &projectDir);

class QmlProject;
class QmlBuildSystem;

class QMLPROJECTMANAGER_EXPORT QmlMainFileAspect : public Utils::BaseAspect
{
    Q_OBJECT

public:
    explicit QmlMainFileAspect(Utils::AspectContainer *container = nullptr);
    ~QmlMainFileAspect() override;

    enum MainScriptSource {
        FileInEditor,
        FileInProjectFile,
        FileInSettings
    };

    struct Data : BaseAspect::Data
    {
        Utils::FilePath mainScript;
        Utils::FilePath currentFile;
    };

    Utils::AspectPresentation presentation() const final;
    // The chosen entry's id rather than its place in the list: the list is
    // rebuilt whenever the project's files change.
    QVariant volatileVariantValue() const final;
    void setVolatileVariantValue(const QVariant &value, Announcement howToAnnounce = DoEmit) final;
    void toMap(Utils::Store &map) const final;
    void fromMap(const Utils::Store &map) final;

    void updateFileList();
    MainScriptSource mainScriptSource() const;

    void setScriptSource(MainScriptSource source, const QString &settingsPath = QString());

    Utils::FilePath mainScript() const;
    Utils::FilePath currentFile() const;
    void changeCurrentFile(Core::IEditor *editor = nullptr);
    bool isQmlFilePresent();
    QmlBuildSystem *qmlBuildSystem() const;

public:
    // The ids of what is on offer, in order. Held here because both backends
    // read it through presentation(), and because what the aspect stores is
    // one of them.
    QStringList m_fileList;
    QString m_scriptFile;
    // absolute path to current file (if being used)
    Utils::FilePath m_currentFileFilename;
    // absolute path to selected main script (if being used)
    Utils::FilePath m_mainScriptFilename;
};

#ifdef WITH_TESTS
QObject *createQmlMainFileTest();
#endif

} // QmlProjectManager
