// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "filedialogs.h"

#include "algorithm.h"
#include "environment.h"
#include "filedialog.h"
#include "fileutils.h"
#include "guiutils.h"
#include "hostosinfo.h"
#include "qtcassert.h"
#include "temporarydirectory.h"
#include "utilstr.h"

#include <QGuiApplication>
#include <QMessageBox>
#include <QPromise>

namespace Utils {
namespace FileUtils {

static FilePaths getFilePaths(
    const QString &caption,
    const FilePath &dir,
    const QString &filter,
    QString *selectedFilter,
    QFileDialog::Options options,
    const QStringList &supportedSchemes,
    const bool forceNonNativeDialog,
    QFileDialog::FileMode fileMode,
    QFileDialog::AcceptMode acceptMode)
{
#ifdef QT_DEBUG
    const bool shiftPressed = QGuiApplication::queryKeyboardModifiers().testFlag(Qt::ShiftModifier);
#else
    const bool shiftPressed = false;
#endif
    if (!hasNativeFileDialog() || forceNonNativeDialog || shiftPressed || !dir.isLocal()
        || options.testFlag(QFileDialog::DontUseNativeDialog)) {
        FileDialog dialog(dialogParent());
        dialog.setMode(fileMode);
        if (!caption.isEmpty())
            dialog.setWindowTitle(caption);
        dialog.setDirectory(dir.isEmpty() ? FilePath::fromString(QDir::homePath()) : dir);
        if (!filter.isEmpty())
            dialog.setNameFilters({filter});
        dialog.setAcceptMode(acceptMode);
        if (dialog.exec() == QDialog::Accepted) {
            if (selectedFilter)
                *selectedFilter = dialog.selectedNameFilter();
            return dialog.selectedFiles();
        }
        return {};
    }

    QFileDialog dialog(dialogParent(), caption, dir.toFSPathString(), filter);
    dialog.setFileMode(fileMode);
    dialog.setOptions(options);

    dialog.setSupportedSchemes(supportedSchemes);
    dialog.setAcceptMode(acceptMode);

    if (selectedFilter && !selectedFilter->isEmpty())
        dialog.selectNameFilter(*selectedFilter);
    if (dialog.exec() == QDialog::Accepted) {
        if (selectedFilter)
            *selectedFilter = dialog.selectedNameFilter();
        return Utils::transform(dialog.selectedUrls(), &FilePath::fromUrl);
    }
    return {};
}

static FilePath firstOrEmpty(const FilePaths &filePaths)
{
    return filePaths.isEmpty() ? FilePath() : filePaths.first();
}

bool hasNativeFileDialog()
{
    // Checking QFileDialog::itemDelegate() seems to be the only way to determine
    // whether the dialog is native or not.
    static bool hasNative = QFileDialog().itemDelegate() == nullptr;
    return hasNative;
}

FilePath getOpenFilePath(const QString &caption,
                         const FilePath &dir,
                         const QString &filter,
                         QString *selectedFilter,
                         QFileDialog::Options options,
                         bool fromDeviceIfShiftIsPressed,
                         bool forceNonNativeDialog)
{
    forceNonNativeDialog = forceNonNativeDialog || !dir.isLocal();
#ifdef QT_GUI_LIB
    if (fromDeviceIfShiftIsPressed && qApp->queryKeyboardModifiers() & Qt::ShiftModifier) {
        forceNonNativeDialog = true;
    }
#endif

    const QStringList schemes = QStringList(QStringLiteral("file"));
    return firstOrEmpty(getFilePaths(caption,
                                     dir,
                                     filter,
                                     selectedFilter,
                                     options,
                                     schemes,
                                     forceNonNativeDialog,
                                     QFileDialog::ExistingFile,
                                     QFileDialog::AcceptOpen));
}

QFuture<FilePath> getOpenFilePathAsync(const QString &caption,
                                       const FilePath &dir,
                                       const QString &filter,
                                       QFileDialog::Options options)
{
    auto promise = std::make_shared<QPromise<FilePath>>();
    promise->start();
    QFuture<FilePath> future = promise->future();

    // Creates and shows a dialog, so anything but the GUI thread is a programming error.
    // Resolve like a cancel, so the future carries a result on this path as well.
    QTC_ASSERT(QThread::currentThread() == qApp->thread(),
               promise->addResult(FilePath());
               promise->finish();
               return future);

#ifdef Q_OS_WASM
    Q_UNUSED(caption)
    Q_UNUSED(dir)
    Q_UNUSED(options)
    // On WebAssembly the browser hands us the file *content*, not a path that can be read
    // back later (a QFileDialog selection is only a JS File handle, unreadable from worker
    // threads). Materialize the bytes into a real file in the in-memory filesystem and
    // resolve with that path, keeping the original name so suffix-based handling still works.
    QFileDialog::getOpenFileContent(
        filter, [promise](const QString &fileName, const QByteArray &content) {
            if (fileName.isEmpty()) { // user cancelled
                promise->addResult(FilePath());
                promise->finish();
                return;
            }
            FilePath target;
            if (QTemporaryDir *masterDir = TemporaryDirectory::masterTemporaryDirectory())
                target = FilePath::fromString(masterDir->path());
            if (target.isEmpty())
                target = FilePath::fromString(QDir::tempPath());
            target = target / FilePath::fromString(fileName).fileName();
            const Result<qint64> written = target.writeFileContents(content);
            if (!written) {
                qWarning() << "getOpenFilePathAsync: cannot write" << target.toUserOutput() << ":"
                           << written.error();
                promise->addResult(FilePath());
                promise->finish();
                return;
            }
            promise->addResult(target);
            promise->finish();
        });
    return future;
#else
    const auto resolve = [promise](const FilePath &filePath) {
        promise->addResult(filePath);
        promise->finish();
    };

    // The dialog may die with dialogParent() before it was ever finished. Resolving on
    // destroyed() too keeps the promise's "exactly one result" contract; on the ordinary
    // path finish() has already happened by then and the second result is ignored.
    const auto resolveOnDestruction = [resolve](QDialog *dialog) {
        QObject::connect(dialog, &QObject::destroyed, dialog, [resolve] { resolve(FilePath()); });
    };

    if (!hasNativeFileDialog() || !dir.isLocal()
        || options.testFlag(QFileDialog::DontUseNativeDialog)) {
        auto dialog = new FileDialog(dialogParent());
        dialog->setMode(QFileDialog::ExistingFile);
        if (!caption.isEmpty())
            dialog->setWindowTitle(caption);
        dialog->setDirectory(dir.isEmpty() ? FilePath::fromString(QDir::homePath()) : dir);
        if (!filter.isEmpty())
            dialog->setNameFilters({filter});
        dialog->setAcceptMode(QFileDialog::AcceptOpen);
        dialog->setAttribute(Qt::WA_DeleteOnClose);

        QObject::connect(dialog, &QDialog::finished, dialog, [resolve, dialog](int result) {
            const FilePaths filePaths = dialog->selectedFiles();
            resolve(result == QDialog::Accepted ? firstOrEmpty(filePaths) : FilePath());
        });
        resolveOnDestruction(dialog);

        dialog->open();
        return future;
    }

    auto dialog = new QFileDialog(dialogParent(), caption, dir.toFSPathString(), filter);
    dialog->setFileMode(QFileDialog::ExistingFile);
    dialog->setAcceptMode(QFileDialog::AcceptOpen);
    dialog->setOptions(options);
    dialog->setSupportedSchemes({QStringLiteral("file")});
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(dialog, &QFileDialog::finished, dialog, [resolve, dialog](int result) {
        if (result != QDialog::Accepted) {
            resolve(FilePath());
            return;
        }
        const QList<QUrl> urls = dialog->selectedUrls();
        resolve(urls.isEmpty() ? FilePath() : FilePath::fromUrl(urls.constFirst()));
    });
    resolveOnDestruction(dialog);

    dialog->open();
    return future;
#endif
}

FilePath getSaveFilePath(const QString &caption,
                         const FilePath &dir,
                         const QString &filter,
                         QString *selectedFilter,
                         QFileDialog::Options options,
                         bool forceNonNativeDialog)
{
    forceNonNativeDialog = forceNonNativeDialog || !dir.isLocal();

    const QStringList schemes = QStringList(QStringLiteral("file"));
    return firstOrEmpty(getFilePaths(caption,
                                     dir,
                                     filter,
                                     selectedFilter,
                                     options,
                                     schemes,
                                     forceNonNativeDialog,
                                     QFileDialog::AnyFile,
                                     QFileDialog::AcceptSave));
}

FilePath getExistingDirectory(const QString &caption,
                              const FilePath &dir,
                              QFileDialog::Options options,
                              bool fromDeviceIfShiftIsPressed,
                              bool forceNonNativeDialog)
{
    forceNonNativeDialog = forceNonNativeDialog || !dir.isLocal();

#ifdef QT_GUI_LIB
    if (fromDeviceIfShiftIsPressed && qApp->queryKeyboardModifiers() & Qt::ShiftModifier) {
        forceNonNativeDialog = true;
    }
#endif

    const QStringList schemes = QStringList(QStringLiteral("file"));
    return firstOrEmpty(getFilePaths(caption,
                                     dir,
                                     {},
                                     nullptr,
                                     options,
                                     schemes,
                                     forceNonNativeDialog,
                                     QFileDialog::Directory,
                                     QFileDialog::AcceptOpen));
}

FilePaths getOpenFilePaths(const QString &caption,
                           const FilePath &dir,
                           const QString &filter,
                           QString *selectedFilter,
                           QFileDialog::Options options)
{
    bool forceNonNativeDialog = !dir.isLocal();

    const QStringList schemes = QStringList(QStringLiteral("file"));
    return getFilePaths(caption,
                        dir,
                        filter,
                        selectedFilter,
                        options,
                        schemes,
                        forceNonNativeDialog,
                        QFileDialog::ExistingFiles,
                        QFileDialog::AcceptOpen);
}

void showError(const QString &errorMessage)
{
    QMessageBox::critical(dialogParent(), Tr::tr("File Error"), errorMessage);
}

} // namespace FileUtils
} // namespace Utils
