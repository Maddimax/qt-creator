// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/filepath.h>

#include <QStringList>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace Debugger::Internal {

// How a CDB symbol path is spelled, and what is in one. The formatting and
// the parsing are a pair: "srv*<cache>*<url>" and "cache*<dir>".
class CdbSymbolPathListEditor
{
public:
    enum SymbolPathMode{
        SymbolServerPath,
        SymbolCachePath
    };

    static bool promptCacheDirectory(QWidget *parent, Utils::FilePath *cacheDirectory);

    // Format a symbol path specification
    static QString symbolPath(const Utils::FilePath &cacheDir, SymbolPathMode mode);
    // Check for a symbol server path and extract local cache directory
    static bool isSymbolServerPath(const QString &path, QString *cacheDir = nullptr);
    // Check for a symbol cache path and extract local cache directory
    static bool isSymbolCachePath(const QString &path, QString *cacheDir = nullptr);
    // Check for symbol server in list of paths.
    static int indexOfSymbolPath(const QStringList &paths, SymbolPathMode mode, QString *cacheDir = nullptr);
};

// The cache directory to offer when setting the symbol paths up: the one a
// listed symbol server or cache entry already names, and otherwise a directory
// under the temporary one. A symbol path is not itself a directory - it is
// "srv*<cache>*<url>" - so the directory has to be taken out of it.
Utils::FilePath symbolCacheDirectory(const QStringList &paths);

// The paths to add for the answers the setup dialog came back with. With both
// wanted the cache entry carries the directory and the server entry does not;
// with only the server wanted, the server entry carries it.
QStringList symbolPathsToAdd(bool useSymbolCache, bool useSymbolServer,
                             const Utils::FilePath &cacheDir);

#ifdef WITH_TESTS
QObject *createCacheDirectoryTest();
#endif

} // Debugger::Internal
