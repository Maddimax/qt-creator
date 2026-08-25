// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QObject>

namespace Utils {
class AspectContainer;
class FilePath;
class FilePaths;
class Key;
} // namespace Utils

namespace McuSupport::Internal {

class McuPackageVersionDetector;

class McuAbstractPackage : public QObject
{
    Q_OBJECT
public:
    enum class Status {
        EmptyPath,
        InvalidPath,
        ValidPathInvalidPackage,
        ValidPackageMismatchedVersion,
        ValidPackageVersionNotDetected,
        ValidPackage
    };

    virtual ~McuAbstractPackage() = default;

    virtual QString label() const = 0;
    virtual QString cmakeVariableName() const = 0;
    virtual QString environmentVariableName() const = 0;
    virtual bool isOptional() const = 0;
    virtual bool isAddToSystemPath() const = 0;
    virtual QStringList versions() const = 0;

    virtual Utils::FilePath basePath() const = 0;
    virtual Utils::FilePath path() const = 0;
    virtual void setPath(const Utils::FilePath &) = 0;
    virtual Utils::FilePath defaultPath() const = 0;
    virtual Utils::FilePaths detectionPaths() const = 0;
    virtual QString detectionPathsToString() const { return {}; };
    virtual Utils::Key settingsKey() const = 0;

    virtual void updateStatus() = 0;
    virtual Status status() const = 0;
    virtual QString statusText() const = 0;
    virtual bool isValidStatus() const = 0;

    virtual bool writeToSettings() const = 0;
    virtual void readFromSettings() = 0;

    // The rows a page shows for this package - where its path goes, and what
    // is wrong with it - appended to whatever the page is listing. This is
    // what widget() was: which controls, in what order.
    virtual void addSettingsRows(Utils::AspectContainer &rows) = 0;
    // What Reset goes back to. The page knows it with the target's macros
    // expanded; the package only has the unexpanded one.
    virtual void setExpandedDefaultPath(const Utils::FilePath &path) = 0;
    virtual const McuPackageVersionDetector *getVersionDetector() const = 0;

signals:
    void changed();
    void statusChanged();
};

} // namespace McuSupport::Internal
