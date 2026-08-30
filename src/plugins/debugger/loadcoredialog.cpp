// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "loadcoredialog.h"

#include "debuggerkitaspect.h"
#include "debuggerruncontrol.h"
#include "debuggertr.h"
#include "gdb/gdbengine.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/kitchooser.h>
#include <projectexplorer/projectexplorerconstants.h>

#include <utils/aspects.h>
#include <utils/async.h>
#include <utils/pathchooser.h>
#include <utils/processinterface.h>
#include <utils/progressindicator.h>
#include <utils/qtcassert.h>
#include <utils/temporaryfile.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Core;
using namespace ProjectExplorer;
using namespace QtTaskTree;
using namespace Utils;

namespace Debugger::Internal {

class AttachCoreDialogData : public AspectContainer
{
public:
    AttachCoreDialogData()
    {
        setSettingsGroup("DebugMode");
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Debugger/LoadCoreDialog.qml"));

        kitChooser.setQmlName("Kit");
        kitChooser.kit.setLabelText(Tr::tr("Kit:"));
        kitChooser.setShowIcons(true);

        coreFile.setQmlName("CoreFile");
        symbolFile.setQmlName("SymbolFile");
        overrideStartScript.setQmlName("StartScript");
        sysRoot.setQmlName("SysRoot");

        coreFile.setSettingsKey("LastLocalCoreFile");
        coreFile.setHistoryCompleter("Debugger.CoreFile.History");
        coreFile.setExpectedKind(PathChooserKind::File);
        coreFile.setPromptDialogTitle(Tr::tr("Select Core File"));
        coreFile.setAllowPathFromDevice(true);
        coreFile.setLabelText(Tr::tr("Core file:"));

        symbolFile.setSettingsKey("LastExternalExecutableFile");
        symbolFile.setHistoryCompleter("Executable");
        symbolFile.setExpectedKind(PathChooserKind::File);
        symbolFile.setPromptDialogTitle(Tr::tr("Select Executable or Symbol File"));
        symbolFile.setAllowPathFromDevice(true);
        symbolFile.setLabelText(Tr::tr("&Executable or symbol file:"));
        symbolFile.setToolTip(
            Tr::tr("Select a file containing debug information corresponding to the core file. "
                   "Typically, this is the executable or a *.debug file if the debug "
                   "information is stored separately from the executable."));

        overrideStartScript.setSettingsKey("LastExternalStartScript");
        overrideStartScript.setHistoryCompleter("Debugger.StartupScript.History");
        overrideStartScript.setExpectedKind(PathChooserKind::File);
        overrideStartScript.setPromptDialogTitle(Tr::tr("Select Startup Script"));
        overrideStartScript.setLabelText(Tr::tr("Override &start script:"));

        sysRoot.setSettingsKey("LastSysRoot");
        sysRoot.setHistoryCompleter("Debugger.SysRoot.History");
        sysRoot.setExpectedKind(PathChooserKind::Directory);
        sysRoot.setPromptDialogTitle(Tr::tr("Select SysRoot Directory"));
        sysRoot.setToolTip(Tr::tr("This option can be used to override the kit's SysRoot setting"));
        sysRoot.setLabelText(Tr::tr("Override S&ysRoot:"));
    }

    ProjectExplorer::KitChooserAspect kitChooser{this};
    FilePathAspect coreFile{this};
    FilePathAspect symbolFile{this};
    FilePathAspect overrideStartScript{this};
    FilePathAspect sysRoot{this};
};

class AttachCoreDialog final : public QDialog
{
public:
    AttachCoreDialog();

    int exec() final;

    FilePath symbolFile() const { return m_data.symbolFile(); }
    FilePath coreFile() const { return m_data.coreFile(); }
    FilePath overrideStartScript() const { return m_data.overrideStartScript(); }
    FilePath sysRoot() const { return m_data.sysRoot(); }

    // For persistance.
    ProjectExplorer::Kit *kit() const { return m_data.kitChooser.currentKit(); }

    void setKitId(Id id) { m_data.kitChooser.setCurrentKitId(id); }
    void restoreSettings() { m_data.readSettings(); }
    void saveSettings() const { m_data.writeSettings(); }

    FilePath coreFileCopy() const;
    FilePath symbolFileCopy() const;

private:
    void accepted();
    void changed();
    void coreFileChanged(const FilePath &core);

    AttachCoreDialogData m_data;

    FilePath m_debuggerPath;

    QDialogButtonBox *m_buttonBox;
    ProgressIndicator *m_progressIndicator;
    QLabel *m_progressLabel;

    QTaskTree m_taskTree;
    Result<FilePath> m_coreFileResult;
    Result<FilePath> m_symbolFileResult;

    struct State
    {
        bool isValid() const
        {
            return validKit && validSymbolFilename && validCoreFilename;
        }

        bool validKit;
        bool validSymbolFilename;
        bool validCoreFilename;
    };

    State getDialogState() const
    {
        State st;
        st.validKit = (m_data.kitChooser.currentKit() != nullptr);
        st.validSymbolFilename = m_data.symbolFile.isValid();
        st.validCoreFilename = m_data.coreFile.isValid();
        return st;
    }

#ifdef WITH_TESTS
    friend class AttachCoreDialogTest;
#endif
};

AttachCoreDialog::AttachCoreDialog()
    : QDialog(ICore::dialogParent())
{
    setWindowTitle(Tr::tr("Load Core File"));

    m_buttonBox = new QDialogButtonBox(this);
    m_buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);
    m_buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);

    m_data.kitChooser.populate();

    m_progressIndicator = new ProgressIndicator(ProgressIndicatorSize::Small, this);
    m_progressIndicator->setVisible(false);

    m_progressLabel = new QLabel();
    m_progressLabel->setVisible(false);

    const auto bottom = new QHBoxLayout;
    bottom->addWidget(m_progressIndicator);
    bottom->addWidget(m_progressLabel);
    bottom->addWidget(m_buttonBox);

    const auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(&m_data));
    layout->addStretch();
    layout->addLayout(bottom);
}

int AttachCoreDialog::exec()
{
    connect(&m_data.symbolFile, &FilePathAspect::validChanged, this, &AttachCoreDialog::changed);
    connect(&m_data.coreFile, &FilePathAspect::validChanged, this, [this] {
        coreFileChanged(m_data.coreFile());
    });
    connect(&m_data.kitChooser.kit, &BaseAspect::changed, this, &AttachCoreDialog::changed);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &AttachCoreDialog::accepted);
    changed();

    connect(&m_taskTree, &QTaskTree::done, this, [this] {
        setEnabled(true);
        m_progressIndicator->setVisible(false);
        m_progressLabel->setVisible(false);

        if (!m_coreFileResult) {
            QMessageBox::critical(this,
                                  Tr::tr("Error"),
                                  Tr::tr("Failed to copy core file to device: %1")
                                      .arg(m_coreFileResult.error()));
            return;
        }

        if (!m_symbolFileResult) {
            QMessageBox::critical(this,
                                  Tr::tr("Error"),
                                  Tr::tr("Failed to copy symbol file to device: %1")
                                      .arg(m_symbolFileResult.error()));
            return;
        }

        accept();
    });
    connect(&m_taskTree, &QTaskTree::progressValueChanged, this, [this](int value) {
        const QString text = Tr::tr("Copying files to device... %1/%2")
                                 .arg(value)
                                 .arg(m_taskTree.progressMaximum());
        m_progressLabel->setText(text);
    });

    State st = getDialogState();
    if (!st.validKit) {
        m_data.kitChooser.kit.setFocusToInputField();
    } else if (!st.validCoreFilename) {
        m_data.coreFile.setFocusToInputField();
    } else if (!st.validSymbolFilename) {
        m_data.symbolFile.setFocusToInputField();
    }

    return QDialog::exec();
}

void AttachCoreDialog::accepted()
{
    const DebuggerItem debuggerItem = Debugger::DebuggerKitAspect::debugger(kit());
    if (!debuggerItem)
        return;
    const FilePath debuggerCommand = debuggerItem.command();

    const auto copyFile = [debuggerCommand](const FilePath &srcPath) -> Result<FilePath> {
        if (!srcPath.isSameDevice(debuggerCommand)) {
            const Result<FilePath> tmpPath = debuggerCommand.tmpDir();
            if (!tmpPath)
                return make_unexpected(tmpPath.error());

            const FilePath pattern = (*tmpPath
                                      / (srcPath.fileName() + ".XXXXXXXXXXX"));

            const Result<FilePath> resultPath = pattern.createTempFile();
            if (!resultPath)
                return make_unexpected(resultPath.error());
            const Result<> result = srcPath.copyFile(*resultPath);
            if (!result)
                return make_unexpected(result.error());

            return resultPath;
        }

        return srcPath;
    };

    using ResultType = Result<FilePath>;

    const auto copyFileAsync = [=](QPromise<ResultType> &promise, const FilePath &srcPath) {
        promise.addResult(copyFile(srcPath));
    };

    const Group root = {
        parallel,
        AsyncTask<ResultType>{[this, copyFileAsync](auto &task) {
                                  task.setConcurrentCallData(copyFileAsync, coreFile());
                              },
                              [this](const Async<ResultType> &task) { m_coreFileResult = task.result(); },
                              CallDoneFlag::OnSuccess},
        AsyncTask<ResultType>{[this, copyFileAsync](auto &task) {
                                  task.setConcurrentCallData(copyFileAsync, symbolFile());
                              },
                              [this](const Async<ResultType> &task) { m_symbolFileResult = task.result(); },
                              CallDoneFlag::OnSuccess}
    };

    m_taskTree.setRecipe(root);
    m_taskTree.start();

    m_progressLabel->setText(Tr::tr("Copying files to device..."));

    setEnabled(false);
    m_progressIndicator->setVisible(true);
    m_progressLabel->setVisible(true);
}

void AttachCoreDialog::coreFileChanged(const FilePath &coreFile)
{
    if (coreFile.osType() != OsType::OsTypeWindows && coreFile.exists()) {
        Kit *k = m_data.kitChooser.currentKit();
        QTC_ASSERT(k, return);
        ProcessRunData debugger = DebuggerKitAspect::runnable(k);
        CoreInfo cinfo = CoreInfo::readExecutableNameFromCore(debugger, coreFile);
        if (!cinfo.foundExecutableName.isEmpty())
            m_data.symbolFile.setValue(cinfo.foundExecutableName);
        else if (!m_data.symbolFile.isValid() && !cinfo.rawStringFromCore.isEmpty())
            m_data.symbolFile.setValue(FilePath::fromString(cinfo.rawStringFromCore));
    }
    changed();
}

void AttachCoreDialog::changed()
{
    State st = getDialogState();
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(st.isValid());
}

FilePath AttachCoreDialog::coreFileCopy() const
{
    return m_coreFileResult.value_or(m_data.symbolFile());
}

FilePath AttachCoreDialog::symbolFileCopy() const
{
    return m_symbolFileResult.value_or(m_data.symbolFile());
}

void runAttachToCoreDialog()
{
    AttachCoreDialog dlg;

    QtcSettings *settings = ICore::settings();
    const Key kitKey("DebugMode/LastExternalKit");

    const QString lastExternalKit = settings->value(kitKey).toString();
    if (!lastExternalKit.isEmpty())
        dlg.setKitId(Id::fromString(lastExternalKit));
    dlg.restoreSettings();

    if (dlg.exec() != QDialog::Accepted)
        return;

    dlg.saveSettings();
    settings->setValue(kitKey, dlg.kit()->id().toSetting());

    auto runControl = new RunControl(ProjectExplorer::Constants::DEBUG_RUN_MODE);
    runControl->setKit(dlg.kit());
    runControl->setDisplayName(Tr::tr("Core file \"%1\"").arg(dlg.coreFile().toUserOutput()));

    DebuggerRunParameters rp = DebuggerRunParameters::fromRunControl(runControl);
    rp.setInferiorExecutable(dlg.symbolFileCopy());
    rp.setCoreFilePath(dlg.coreFileCopy());
    rp.setStartMode(AttachToCore);
    rp.setCloseMode(DetachAtClose);
    rp.setOverrideStartScript(dlg.overrideStartScript());
    const FilePath sysRoot = dlg.sysRoot();
    if (!sysRoot.isEmpty())
        rp.setSysRoot(sysRoot);

    runControl->setRunRecipe(debuggerRecipe(runControl, rp));
    runControl->start();
}

#ifdef WITH_TESTS

class AttachCoreDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        AttachCoreDialogData data;
        const Result<> rendered = Core::aspectFormRenders(&data, "LoadCoreDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testTheKeysItRemembers()
    {
        // These four are in every reader's settings file already, under the
        // group the dialog has always written them to.
        AttachCoreDialogData data;
        QCOMPARE(data.settingsGroups(), QStringList{"DebugMode"});
        QCOMPARE(data.coreFile.settingsKey(), Key("LastLocalCoreFile"));
        QCOMPARE(data.symbolFile.settingsKey(), Key("LastExternalExecutableFile"));
        QCOMPARE(data.overrideStartScript.settingsKey(), Key("LastExternalStartScript"));
        QCOMPARE(data.sysRoot.settingsKey(), Key("LastSysRoot"));

        // And the kit is not one of them: it is remembered beside them, as a
        // kit id rather than as the chooser's row.
        QVERIFY2(data.kitChooser.kit.settingsKey().isEmpty(),
                 "the chooser's row was written into the reader's settings");
    }

    void testWhatTheFieldsAccept()
    {
        AttachCoreDialogData data;

        // A core file and a symbol file may both be on the machine being
        // debugged rather than on this one.
        QVERIFY2(data.coreFile.presentation().allowPathFromDevice,
                 "a core file could only be chosen on this machine");
        QVERIFY2(data.symbolFile.presentation().allowPathFromDevice,
                 "a symbol file could only be chosen on this machine");

        QCOMPARE(data.sysRoot.presentation().pathKind, AspectControls::PathKind::Directory);
        QCOMPARE(data.coreFile.presentation().pathKind, AspectControls::PathKind::File);
    }

    void testWhenTheCoreCanBeLoaded()
    {
        // All three are needed: a core file to read, something to read it
        // against, and a kit whose debugger does the reading.
        AttachCoreDialog dlg;
        QPushButton *const ok = dlg.m_buttonBox->button(QDialogButtonBox::Ok);
        QVERIFY(ok);
        QVERIFY2(!ok->isEnabled(), "an empty dialog offered to load a core file");

        AttachCoreDialog::State st = dlg.getDialogState();
        QVERIFY2(!st.isValid(), "a dialog with no files chosen was taken to be complete");

        st.validKit = true;
        st.validCoreFilename = true;
        st.validSymbolFilename = false;
        QVERIFY2(!st.isValid(), "a core file with nothing to read it against was accepted");

        st.validSymbolFilename = true;
        st.validKit = false;
        QVERIFY2(!st.isValid(), "a core file was accepted without a kit to read it");

        st.validKit = true;
        QVERIFY(st.isValid());
    }
};

QObject *createAttachCoreDialogTest()
{
    return new AttachCoreDialogTest;
}

#endif // WITH_TESTS

} // Debugger::Internal

#ifdef WITH_TESTS
#include "loadcoredialog.moc"
#endif
