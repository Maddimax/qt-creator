// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "perfloaddialog.h"

#include "perfprofilerconstants.h"
#include "perfprofilertr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/kit.h>
#include <projectexplorer/kitmanager.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/target.h>

#include <utils/aspects.h>
#include <utils/pathvalidation.h>

#include <QDialogButtonBox>
#include <QStandardItem>
#include <QVBoxLayout>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace Utils;

namespace Profiler::Internal {

// What to load and what to read it against. The two paths are file choosers
// rather than a line edit and a Browse button each, so the browsing is the
// aspect's and there is nothing here to connect.
class PerfLoadSettings final : public AspectContainer
{
public:
    PerfLoadSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Profiler/PerfLoadDialog.qml"));

        traceFile.setQmlName("TraceFile");
        traceFile.setLabelText(Tr::tr("Trace file:"));
        // No setExpectedKind(): a FilePathAspect looks for a file already. The
        // test still says it must, since that is a requirement of this dialog
        // rather than a detail of the aspect's default.
        traceFile.setPromptDialogTitle(Tr::tr("Choose Perf Trace"));
        traceFile.setPromptDialogFilter(
            Tr::tr("Perf traces (*%1)").arg(Constants::TraceFileExtension));

        executableDir.setQmlName("ExecutableDir");
        executableDir.setLabelText(Tr::tr("Directory of executable:"));
        executableDir.setExpectedKind(PathChooserKind::ExistingDirectory);
        executableDir.setPromptDialogTitle(Tr::tr("Choose Directory of Executable"));

        kit.setQmlName("Kit");
        kit.setLabelText(Tr::tr("Kit:"));
        kit.setFillCallback([](const StringSelectionAspect::ResultCallback &cb) {
            QList<QStandardItem *> items;
            for (ProjectExplorer::Kit * const k : ProjectExplorer::KitManager::sortedKits()) {
                auto *item = new QStandardItem(k->displayName());
                item->setData(k->id().toSetting());
                items.append(item);
            }
            cb(items);
        });
    }

    // What the project is already using, where there is one: a trace is nearly
    // always read against the kit that produced it.
    void chooseDefaults()
    {
        ProjectExplorer::Kit * const active = ProjectExplorer::activeKitForActiveProject();
        if (!active)
            return;
        kit.setValue(active->id().toSetting().toString());
        if (auto *bc = ProjectExplorer::activeBuildConfigForActiveProject())
            executableDir.setValue(bc->buildDirectory());
    }

    ProjectExplorer::Kit *chosenKit() const
    {
        return ProjectExplorer::KitManager::kit(Id::fromSetting(kit()));
    }

    FilePathAspect traceFile{this};
    FilePathAspect executableDir{this};
    StringSelectionAspect kit{this};
};

PerfLoadDialog::PerfLoadDialog(QWidget *parent)
    : QDialog(parent)
    , m_settings(new PerfLoadSettings)
{
    setWindowTitle(Tr::tr("Load Perf Trace"));
    resize(710, 164);

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_settings->chooseDefaults();
}

PerfLoadDialog::~PerfLoadDialog() = default;

QString PerfLoadDialog::traceFilePath() const
{
    return m_settings->traceFile().toUserOutput();
}

QString PerfLoadDialog::executableDirPath() const
{
    return m_settings->executableDir().toUrlishString();
}

ProjectExplorer::Kit *PerfLoadDialog::kit() const
{
    return m_settings->chosenKit();
}

#ifdef WITH_TESTS

class PerfLoadDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testItAsksForATraceAndAKitToReadItAgainst()
    {
        PerfLoadSettings settings;
        const Utils::Result<> rendered = Core::aspectFormRenders(&settings, "PerfLoadDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));

        // Both paths browse for themselves, which is what replaced a Browse
        // button and its handler for each - and each browses for the kind of
        // thing it wants. Asserting the control alone would say nothing: a
        // FilePathAspect is a path chooser whatever it expects to find.
        QCOMPARE(settings.traceFile.presentation().pathKind, AspectControls::PathKind::File);
        QCOMPARE(settings.executableDir.presentation().pathKind,
                 AspectControls::PathKind::ExistingDirectory);
        QVERIFY2(!settings.traceFile.presentation().promptDialogFilter.isEmpty(),
                 "the trace chooser offers every file rather than traces");

        // The kit is one of the kits, not a string somebody types.
        QCOMPARE(settings.kit.presentation().control, AspectControls::ComboBox);
        const int kitCount = ProjectExplorer::KitManager::kits().size();
        QCOMPARE(settings.kit.presentation().choices.size(), kitCount);

        // An id that is not a kit answers no kit rather than the first one.
        // Asked of the resolution directly: the aspect refuses a value that is
        // not one of its choices, so it cannot be made to hold one, and going
        // through it would assert nothing.
        QVERIFY2(!ProjectExplorer::KitManager::kit(Id::fromSetting("no.such.kit")),
                 "an unknown id was resolved to a kit anyway");
    }
};

QObject *createPerfLoadDialogTest()
{
    return new PerfLoadDialogTest;
}

#endif // WITH_TESTS

} // namespace Profiler::Internal

#ifdef WITH_TESTS
#include "perfloaddialog.moc"
#endif
