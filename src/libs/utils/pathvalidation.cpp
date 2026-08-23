// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "pathvalidation.h"

#include "environment.h"
#include "filepath.h"
#include "hostosinfo.h"
#include "macroexpander.h"
#include "utilstr.h"

QString findMacOSAppByBundleId(const QString &appName);

namespace Utils {

FilePath appBundleExpandedPath(const FilePath &path)
{
    if (path.osType() == OsTypeMac && path.endsWith(".app")) {
        // possibly expand to Foo.app/Contents/MacOS/Foo
        if (path.isDir()) {
            const FilePath exePath = path / "Contents/MacOS" / path.completeBaseName();
            if (exePath.exists())
                return exePath;
        }
    }
    return path;
}

FilePath expandPath(
    const FilePath &input,
    const MacroExpander *macroExpander,
    const FilePath &baseDirectory,
    const Environment &environment,
    PathChooserKind expectedKind)
{
    if (input.isEmpty())
        return {};

    FilePath path = input;

    const Environment env = environment.appliedToEnvironment(path.deviceEnvironment());
    path = env.expandVariables(path);

    if (macroExpander)
        path = macroExpander->expand(path);

    if (path.isEmpty())
        return path;

    if (path.isAbsolutePath())
        return path;

    switch (expectedKind) {
    case PathChooserKind::Command:
    case PathChooserKind::ExistingCommand: {
        const FilePath expanded = path.searchInDirectories(env.mappedPath(path) << baseDirectory);

        if constexpr (HostOsInfo::isMacHost()) {
            if (expanded.isEmpty() && path.isLocal()) {
                const QString appPath = findMacOSAppByBundleId(path.path());
                if (!appPath.isEmpty())
                    return appBundleExpandedPath(FilePath::fromString(appPath));
            }
        }
        return expanded.isEmpty() ? path : expanded;
    }
    case PathChooserKind::Any:
        break;
    case PathChooserKind::Directory:
    case PathChooserKind::ExistingDirectory:
    case PathChooserKind::File:
    case PathChooserKind::SaveFile:
        if (!baseDirectory.isEmpty()) {
            FilePath fp = baseDirectory.resolvePath(path.path()).absoluteFilePath();
            // FIXME bad hotfix for manually editing PathChooser (invalid paths, jumping cursor)
            // examples: have an absolute path and try to change the device letter by typing the new
            // letter and removing the original afterwards ends up in
            // D:\\dev\\project\\cD:\\dev\\build-project (before trying to remove the original)
            // as 'cD:\\dev\\build-project' is considered is handled as being relative
            // input = "cD:\\dev\build-project"; // prepended 'c' to change the device letter
            // m_baseDirectory = "D:\\dev\\project"
            if (fp.isLocal() && HostOsInfo::isWindowsHost() && fp.toUrlishString().count(':') > 1)
                return path;
            return fp;
        }
        break;
    }
    return path;
}

AsyncValidationResult validatePath(const FilePath &filePath, PathChooserKind kind)
{
    if (filePath.isEmpty())
        return ResultError(Tr::tr("The path must not be empty."));

    // Check if existing
    switch (kind) {
    case PathChooserKind::ExistingDirectory:
        if (!filePath.exists()) {
            return ResultError(
                Tr::tr("The path \"%1\" does not exist.").arg(filePath.toUserOutput()));
        }
        if (!filePath.isDir()) {
            return ResultError(
                Tr::tr("The path \"%1\" is not a directory.").arg(filePath.toUserOutput()));
        }
        break;
    case PathChooserKind::File:
        if (!filePath.exists()) {
            return ResultError(
                Tr::tr("The path \"%1\" does not exist.").arg(filePath.toUserOutput()));
        }
        if (!filePath.isFile()) {
            return ResultError(
                Tr::tr("The path \"%1\" is not a file.").arg(filePath.toUserOutput()));
        }
        break;
    case PathChooserKind::SaveFile:
        if (!filePath.parentDir().exists()) {
            return ResultError(
                Tr::tr("The directory \"%1\" does not exist.").arg(filePath.toUserOutput()));
        }
        if (filePath.exists() && filePath.isDir()) {
            return ResultError(
                Tr::tr("The path \"%1\" is not a file.").arg(filePath.toUserOutput()));
        }
        break;
    case PathChooserKind::ExistingCommand:
        if (!filePath.exists()) {
            return ResultError(
                Tr::tr("The path \"%1\" does not exist.").arg(filePath.toUserOutput()));
        }
        if (!filePath.isExecutableFile()) {
            return ResultError(
                Tr::tr("The path \"%1\" is not an executable file.").arg(filePath.toUserOutput()));
        }
        break;
    case PathChooserKind::Directory:
        if (filePath.exists() && !filePath.isDir()) {
            return ResultError(
                Tr::tr("The path \"%1\" is not a directory.").arg(filePath.toUserOutput()));
        }
        if (filePath.osType() == OsTypeWindows && !filePath.startsWithDriveLetter()
            && filePath.scheme() != u"unc"
            && !filePath.path().startsWith("\\\\") && !filePath.path().startsWith("//")) {
            return ResultError(Tr::tr("Invalid path \"%1\".").arg(filePath.toUserOutput()));
        }
        break;
    case PathChooserKind::Command:
        if (filePath.exists() && !filePath.isExecutableFile()) {
            return ResultError(Tr::tr("Cannot execute \"%1\".").arg(filePath.toUserOutput()));
        }
        break;

    default:
        ;
    }

    return filePath.toUserOutput();
}

} // namespace Utils
