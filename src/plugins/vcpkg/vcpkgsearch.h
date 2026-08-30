// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QStringList>
#include <QUrl>

QT_FORWARD_DECLARE_CLASS(QWidget)

namespace Vcpkg::Internal::Search {

struct VcpkgManifest
{
    QString name;
    QString version;
    QString license;
    QStringList dependencies;
    QString shortDescription;
    QStringList description;
    QUrl homepage;
};

VcpkgManifest parseVcpkgManifest(const QByteArray &vcpkgManifestJsonData, bool *ok = nullptr);

// The packages \a filter matches, by name; a package is found by its
// descriptions as well as its name, which is why the list carries them.
QStringList packageNamesMatching(const QList<VcpkgManifest> &packages, const QString &filter);

// What the details pane shows for \a manifest: the short description, then
// each paragraph of the long one.
QString packageDescriptionHtml(const VcpkgManifest &manifest);

// Whether \a package may be added: something has to be chosen, and a package
// the project already depends on is not worth adding twice.
bool canAddPackage(const QString &package, bool isProjectDependency);
VcpkgManifest showVcpkgPackageSearchDialog(const VcpkgManifest &projectManifest,
                                           QWidget *parent = nullptr);

} // namespace Vcpkg::Internal::Search
