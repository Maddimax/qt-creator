// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cdbsymbolpathlisteditor.h"

#include "../debuggertr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>
#include "symbolpathsdialog.h"

#include <coreplugin/icore.h>

#include <utils/pathchooser.h>
#include <utils/temporarydirectory.h>
#include <utils/widgets.h>

#ifdef WITH_TESTS
#include <QTemporaryDir>
#include <QTest>
#endif

#include <QAction>
#include <QCheckBox>
#include <QDebug>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>

using namespace Utils;

namespace Debugger::Internal {

// Internal helper dialog prompting for a cache directory using a PathChooser.
//
// Note that QFileDialog does not offer a way of suggesting
// a non-existent folder, which is in turn automatically
// created. This is done here (suggest $TEMP\symbolcache
// regardless of its existence).

// What a chosen cache folder has to be before the dialog will close on it:
// empty is allowed - the debugger picks its own then - an existing folder is
// taken as it is, and one that is not there yet is made. Anything else is
// refused, with the reason.
//
// Kept out of accept() because it is a question about a path, and there the
// only way to ask it was to open the dialog and press OK.
Result<> prepareCacheDirectory(const FilePath &cache)
{
    if (cache.isEmpty() || cache.isDir())
        return ResultOk;

    if (cache.exists()) {
        return ResultError(
            Tr::tr("A file named \"%1\" already exists.").arg(cache.toUserOutput()));
    }

    if (!cache.ensureWritableDir()) {
        return ResultError(
            Tr::tr("The folder \"%1\" could not be created.").arg(cache.toUserOutput()));
    }

    return ResultOk;
}

class CacheDirectorySettings final : public AspectContainer
{
public:
    CacheDirectorySettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Debugger/CacheDirectoryDialog.qml"));

        path.setQmlName("Path");
        path.setLabelText(Tr::tr("Path:"));
        path.setExpectedKind(PathChooserKind::ExistingDirectory);
        path.setHistoryCompleter("Debugger.CdbCacheDir.History");
    }

    FilePathAspect path{this};
};

class CacheDirectoryDialog : public QDialog
{
public:
    explicit CacheDirectoryDialog(QWidget *parent);

    void setPath(const FilePath &p) { m_settings.path.setValue(p); }
    FilePath path() const { return m_settings.path(); }

    void accept() override;

private:
    CacheDirectorySettings m_settings;
};

CacheDirectoryDialog::CacheDirectoryDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(Tr::tr("Select Local Cache Folder"));
    setModal(true);

    const auto buttonBox
        = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &CacheDirectoryDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &CacheDirectoryDialog::reject);

    const auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(&m_settings));
    layout->addWidget(buttonBox);

    resize(500, 100);
}

void CacheDirectoryDialog::accept()
{
    // One caption for both refusals, where the widget dialog had "Already
    // Exists" and "Cannot Create". The messages themselves still say which.
    if (const Result<> ready = prepareCacheDirectory(path()); !ready) {
        Utils::AsynchronousMessageBox::warning(Tr::tr("Cannot Use Folder"), ready.error());
        return;
    }
    QDialog::accept();
}

// ---------------- CdbSymbolPathListEditor

// Pre- and Postfix used to build a symbol server path specification
const char symbolServerPrefixC[] = "srv*";
const char symbolServerPostfixC[] = "http://msdl.microsoft.com/download/symbols";
const char symbolCachePrefixC[] = "cache*";

bool CdbSymbolPathListEditor::promptCacheDirectory(QWidget *parent, FilePath *cacheDirectory)
{
    CacheDirectoryDialog dialog(parent);
    dialog.setPath(TemporaryDirectory::masterDirectoryFilePath() / "symbolcache");
    if (dialog.exec() != QDialog::Accepted)
        return false;
    *cacheDirectory = dialog.path();
    return true;
}

FilePath symbolCacheDirectory(const QStringList &paths)
{
    // The directory out of the path, not the path itself: a symbol server
    // entry reads "srv*C:\\cache*http://...", and offering that as a
    // directory to write a cache into is nonsense.
    QString cacheDir;
    if (CdbSymbolPathListEditor::indexOfSymbolPath(
            paths, CdbSymbolPathListEditor::SymbolServerPath, &cacheDir)
            != -1
        && !cacheDir.isEmpty()) {
        return FilePath::fromUserInput(cacheDir);
    }
    cacheDir.clear();
    if (CdbSymbolPathListEditor::indexOfSymbolPath(
            paths, CdbSymbolPathListEditor::SymbolCachePath, &cacheDir)
            != -1
        && !cacheDir.isEmpty()) {
        return FilePath::fromUserInput(cacheDir);
    }
    return TemporaryDirectory::masterDirectoryFilePath() / "symbolcache";
}

QStringList symbolPathsToAdd(bool useSymbolCache, bool useSymbolServer, const FilePath &cacheDir)
{
    if (useSymbolCache) {
        QStringList paths{
            CdbSymbolPathListEditor::symbolPath(cacheDir, CdbSymbolPathListEditor::SymbolCachePath)};
        // The cache entry already names the directory, so the server entry
        // does not repeat it.
        if (useSymbolServer) {
            paths.append(
                CdbSymbolPathListEditor::symbolPath({}, CdbSymbolPathListEditor::SymbolServerPath));
        }
        return paths;
    }
    if (useSymbolServer) {
        return {CdbSymbolPathListEditor::symbolPath(cacheDir,
                                                    CdbSymbolPathListEditor::SymbolServerPath)};
    }
    return {};
}

QString CdbSymbolPathListEditor::symbolPath(const FilePath &cacheDir,
                                            CdbSymbolPathListEditor::SymbolPathMode mode)
{
    if (mode == SymbolCachePath)
        return symbolCachePrefixC + cacheDir.toUserOutput();
    QString s = QLatin1String(symbolServerPrefixC);
    if (!cacheDir.isEmpty())
        s += cacheDir.toUserOutput() + '*';
    s += QLatin1String(symbolServerPostfixC);
    return s;
}

bool CdbSymbolPathListEditor::isSymbolServerPath(const QString &path, QString *cacheDir /*  = nullptr */)
{
    if (!path.startsWith(QLatin1String(symbolServerPrefixC)) || !path.endsWith(QLatin1String(symbolServerPostfixC)))
        return false;
    if (cacheDir) {
        static const unsigned prefixLength = unsigned(qstrlen(symbolServerPrefixC));
        static const unsigned postfixLength = unsigned(qstrlen(symbolServerPostfixC));
        if (unsigned(path.size()) == prefixLength + postfixLength)
            return true;
        // Split apart symbol server post/prefixes. The path is
        // "srv*<cacheDir>*<url>", so what is left after taking off both ends is
        // the directory and the separator before the url.
        *cacheDir = path.mid(prefixLength,
                             path.size() - prefixLength - int(postfixLength) - 1);
    }
    return true;
}

bool CdbSymbolPathListEditor::isSymbolCachePath(const QString &path, QString *cacheDir)
{
    if (!path.startsWith(QLatin1String(symbolCachePrefixC)))
        return false;
    if (cacheDir) {
        static const unsigned prefixLength = unsigned(qstrlen(symbolCachePrefixC));
        // Split apart symbol cach prefixes
        *cacheDir = path.mid(prefixLength);
    }
    return true;
}

int CdbSymbolPathListEditor::indexOfSymbolPath(const QStringList &paths,
                                               CdbSymbolPathListEditor::SymbolPathMode mode,
                                               QString *cacheDir /*  = nullptr */)
{
    const int count = paths.size();
    for (int i = 0; i < count; i++) {
        if (mode == SymbolServerPath
                ? CdbSymbolPathListEditor::isSymbolServerPath(paths.at(i), cacheDir)
                : CdbSymbolPathListEditor::isSymbolCachePath(paths.at(i), cacheDir)) {
                return i;
        }
    }
    return -1;
}

#ifdef WITH_TESTS

class CacheDirectoryTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        CacheDirectorySettings settings;
        const Result<> rendered
            = Core::aspectFormRenders(&settings, "CacheDirectoryDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhatMakesAFolderUsableAsACache()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const FilePath root = FilePath::fromString(temp.path());

        // Nothing chosen is allowed: the debugger picks its own then.
        QVERIFY(prepareCacheDirectory({}));

        // A folder that is already there is taken as it is.
        QVERIFY(prepareCacheDirectory(root));

        // One that is not there is made, which is the whole reason this runs
        // on OK rather than on browsing.
        const FilePath fresh = root / "made-on-accept";
        QVERIFY(!fresh.exists());
        QVERIFY(prepareCacheDirectory(fresh));
        QVERIFY2(fresh.isDir(), "the folder was accepted without being created");

        // A file of that name is refused, and the message says which file -
        // "cannot create" with no name is not something a reader can act on.
        const FilePath file = root / "in-the-way";
        QVERIFY(file.writeFileContents("not a folder"));
        const Result<> refused = prepareCacheDirectory(file);
        QVERIFY2(!refused, "a file was accepted as a cache folder");
        QVERIFY(refused.error().contains(file.toUserOutput()));

        // And so is a folder that cannot be made.
        const Result<> impossible = prepareCacheDirectory(file / "under-a-file");
        QVERIFY2(!impossible, "a folder under a file was accepted");
        QVERIFY(impossible.error().contains("under-a-file"));

        // The two reasons are different, and stay that way: both refusals
        // reach the reader as a message, and "could not be created" for a name
        // that is taken by a file sends them looking for the wrong problem.
        // Checked by the word that separates them, because falling from the
        // first case into the second still refuses, and still names the path.
        QVERIFY2(refused.error().contains("exists"),
                 qPrintable("a file in the way was refused as: " + refused.error()));
        QVERIFY2(impossible.error().contains("created"),
                 qPrintable("an uncreatable folder was refused as: " + impossible.error()));
    }
};

QObject *createCacheDirectoryTest()
{
    return new CacheDirectoryTest;
}

#endif // WITH_TESTS

} // Debugger::Internal

#ifdef WITH_TESTS
#include "cdbsymbolpathlisteditor.moc"
#endif
