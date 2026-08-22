// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include "filepath.h"

#include <QFileDialog>
#include <QFuture>

namespace Utils {
namespace FileUtils {

QTCREATOR_UTILS_EXPORT bool hasNativeFileDialog();

QTCREATOR_UTILS_EXPORT FilePath getOpenFilePath(
        const QString &caption,
        const FilePath &dir = {},
        const QString &filter = {},
        QString *selectedFilter = nullptr,
        QFileDialog::Options options = {},
        bool fromDeviceIfShiftIsPressed = false,
        bool forceNonNativeDialog = false);

// Non-blocking variant of getOpenFilePath: shows the dialog (no nested event loop) and
// returns immediately with a QFuture that resolves to the chosen path, or an empty
// FilePath if the user cancels. Required on platforms (e.g. WebAssembly without asyncify)
// where blocking the calling thread in QDialog::exec() is not allowed.
//
// Must be called from the GUI thread, and the future must never be waited on: the result
// only arrives once the calling thread returns to the event loop, so waitForFinished()
// deadlocks by construction. Use .then() instead. The future always carries exactly one
// result: if the dialog is destroyed together with dialogParent() before the user chose
// anything, it resolves with an empty FilePath, just like a cancel.
//
// The dialog is window-modal to dialogParent(), so the action that opened it cannot be
// triggered a second time while it is up - it is only the nested event loop of the blocking
// variant, not its modality, that this variant does away with.
//
// On WebAssembly the browser only hands over the file *content*, so the future resolves
// with a temporary copy in the (in-memory) temporary directory, not with the file the user
// picked. The copy keeps the original file name, so opening the same file twice overwrites
// the previous copy, and it stays around for the lifetime of the process.
QTCREATOR_UTILS_EXPORT QFuture<FilePath> getOpenFilePathAsync(
        const QString &caption,
        const FilePath &dir = {},
        const QString &filter = {},
        QFileDialog::Options options = {});

QTCREATOR_UTILS_EXPORT FilePath getSaveFilePath(
        const QString &caption,
        const FilePath &dir = {},
        const QString &filter = {},
        QString *selectedFilter = nullptr,
        QFileDialog::Options options = {},
        bool forceNonNativeDialog = false);

QTCREATOR_UTILS_EXPORT FilePath getExistingDirectory(
        const QString &caption,
        const FilePath &dir = {},
        QFileDialog::Options options = QFileDialog::ShowDirsOnly,
        bool fromDeviceIfShiftIsPressed = false,
        bool forceNonNativeDialog = false);

QTCREATOR_UTILS_EXPORT FilePaths getOpenFilePaths(
        const QString &caption,
        const FilePath &dir = {},
        const QString &filter = {},
        QString *selectedFilter = nullptr,
        QFileDialog::Options options = {});

QTCREATOR_UTILS_EXPORT void showError(const QString &errorMessage);

} // namespace FileUtils
} // namespace Utils
