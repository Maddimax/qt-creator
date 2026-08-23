// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include "validationfunction.h"

#include <QObject>

namespace Utils {
Q_NAMESPACE_EXPORT(QTCREATOR_UTILS_EXPORT)

class Environment;
class FilePath;
class MacroExpander;

enum class PathChooserKind {
    ExistingDirectory,
    Directory, // A directory, doesn't need to exist
    File, // An existing file
    SaveFile, // A file that does not need to exist
    ExistingCommand, // A command that must exist at the time of selection
    Command, // A command that may or may not exist at the time of selection (e.g. result of a build)
    Any
};
Q_ENUM_NS(PathChooserKind)

QTCREATOR_UTILS_EXPORT FilePath appBundleExpandedPath(const FilePath &path);

QTCREATOR_UTILS_EXPORT FilePath expandPath(
    const FilePath &input,
    const MacroExpander *macroExpander,
    const FilePath &baseDirectory,
    const Environment &environment,
    PathChooserKind expectedKind = PathChooserKind::Any);

QTCREATOR_UTILS_EXPORT AsyncValidationResult validatePath(
    const FilePath &filePath, PathChooserKind kind);

} // namespace Utils
