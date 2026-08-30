// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "parseissuesdialog.h"

#include "devicesupport/devicekitaspects.h"
#include "kitchooser.h"
#include "kitmanager.h"
#include "projectexplorertr.h"
#include "projectexplorerconstants.h"
#include "taskhub.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>
#include <utils/filedialogs.h>
#include <utils/fileutils.h>
#include <utils/guiutils.h>
#include <utils/outputformatter.h>
#include <utils/qtcassert.h>

#ifdef WITH_TESTS
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#endif

#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <memory>

using namespace Utils;

namespace ProjectExplorer::Internal {

// The kit the dialog opens on when there is no active project to take one
// from: parsing a build log is a desktop job unless the reader says otherwise.
static Id firstDesktopKitId(const QList<Kit *> &kits)
{
    for (const Kit *const k : kits) {
        if (RunDeviceTypeKitAspect::deviceTypeId(k) == Constants::DESKTOP_DEVICE_TYPE)
            return k->id();
    }
    return {};
}

// Build output is read as the local encoding, which is what the build wrote it
// in.
static Result<QString> readBuildOutput(const FilePath &filePath)
{
    const Result<QByteArray> res = filePath.fileContents();
    if (!res)
        return ResultError(res.error());
    return QString::fromLocal8Bit(*res);
}

// Every line goes to the parsers as a message of its own, with its newline put
// back: that is the shape a running build delivers, and the parsers that keep
// state across lines depend on it.
static void parseOutput(const QString &text,
                        OutputFormat format,
                        const QList<OutputLineParser *> &lineParsers)
{
    OutputFormatter formatter;
    formatter.setLineParsers(lineParsers);
    const QStringList lines = text.split('\n');
    for (const QString &line : lines)
        formatter.appendMessage(line + '\n', format);
    formatter.flush();
}

class ParseIssuesSettings final : public AspectContainer
{
public:
    ParseIssuesSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ProjectExplorer/ParseIssuesDialog.qml"));

        output.setQmlName("Output");
        output.setDisplayStyle(StringAspect::TextEditDisplay);

        loadFile.setQmlName("LoadFile");
        loadFile.setActionText(Tr::tr("Load from File..."));

        stderrOutput.setQmlName("Stderr");
        stderrOutput.setLabel(Tr::tr("Output went to stderr"),
                              BoolAspect::LabelPlacement::AtCheckBox);
        stderrOutput.setDefaultValue(true);
        stderrOutput.setValue(true);

        kitChooser.setQmlName("Kit");
        kitChooser.kit.setLabelText(Tr::tr("Use parsers from kit:"));

        clearTasks.setQmlName("ClearTasks");
        clearTasks.setLabel(Tr::tr("Clear existing tasks"),
                            BoolAspect::LabelPlacement::AtCheckBox);
        clearTasks.setDefaultValue(true);
        clearTasks.setValue(true);
    }

    // Which kit's parsers to use, and what to do with what they find.
    void chooseKit()
    {
        kitChooser.populate();
        if (!kitChooser.hasStartupKit()) {
            if (const Id desktop = firstDesktopKitId(KitManager::kits()); desktop.isValid())
                kitChooser.setCurrentKitId(desktop);
        }
    }

    StringAspect output{this};
    ActionAspect loadFile{this};
    BoolAspect stderrOutput{this};
    KitChooserAspect kitChooser{this};
    BoolAspect clearTasks{this};
};

class ParseIssuesDialog final : public QDialog
{
public:
    ParseIssuesDialog();
    ~ParseIssuesDialog() override;

private:
    void accept() final;

    const std::unique_ptr<ParseIssuesSettings> m_settings;
    QDialogButtonBox *m_buttonBox;

#ifdef WITH_TESTS
    friend class ParseIssuesDialogTest;
#endif
};

ParseIssuesDialog::ParseIssuesDialog()
    : QDialog(dialogParent())
    , m_settings(new ParseIssuesSettings)
{
    setWindowTitle(Tr::tr("Parse Build Output"));

    m_settings->loadFile.setAction([this] {
        const FilePath filePath = FileUtils::getOpenFilePath(Tr::tr("Choose File"));
        if (filePath.isEmpty())
            return;
        const Result<QString> text = readBuildOutput(filePath);
        if (!text) {
            QMessageBox::critical(this, Tr::tr("Could Not Open File"),
                                  Tr::tr("Could not open file: \"%1\": %2")
                                      .arg(filePath.toUserOutput(), text.error()));
            return;
        }
        m_settings->output.setValue(*text);
    });

    m_settings->chooseKit();

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // The widget dialog decided this once and never again, so a reader who
    // changed the kit kept whatever the button was at the start.
    const auto followKit = [this] {
        m_buttonBox->button(QDialogButtonBox::Ok)
            ->setEnabled(m_settings->kitChooser.currentKit() != nullptr);
    };
    connect(&m_settings->kitChooser.kit, &BaseAspect::changed, this, followKit);
    followKit();

    const auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(m_buttonBox);
}

ParseIssuesDialog::~ParseIssuesDialog() = default;

void ParseIssuesDialog::accept()
{
    const Kit *const kit = m_settings->kitChooser.currentKit();
    QTC_ASSERT(kit, return);

    const QList<OutputLineParser *> lineParsers = kit->createOutputParsers();
    if (lineParsers.isEmpty()) {
        QMessageBox::critical(this, Tr::tr("Cannot Parse"),
                              Tr::tr("Cannot parse: The chosen kit does "
                                     "not provide an output parser."));
        return;
    }
    if (m_settings->clearTasks())
        TaskHub::clearTasks();

    parseOutput(m_settings->output(),
                m_settings->stderrOutput() ? StdErrFormat : StdOutFormat,
                lineParsers);
    QDialog::accept();
}

void executeParseIssuesDialog()
{
    ParseIssuesDialog dialog;
    dialog.exec();
}

#ifdef WITH_TESTS

// What a parser was handed. Kept outside the parser because a formatter owns
// the parsers it is given and deletes them with itself, so reading them
// afterwards is reading freed memory.
struct Recorded
{
    QStringList lines;
    QList<OutputFormat> formats;
};

class RecordingParser final : public OutputLineParser
{
public:
    explicit RecordingParser(Recorded *into) : m_into(into) {}

private:
    Result handleLine(const QString &line, OutputFormat format) override
    {
        m_into->lines << line;
        m_into->formats << format;
        return Status::NotHandled;
    }

    Recorded *const m_into;
};

class ParseIssuesDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        ParseIssuesSettings settings;
        const Result<> rendered = Core::aspectFormRenders(&settings, "ParseIssuesDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhatTheBoxesStartAt()
    {
        // Both boxes were checked in the widget dialog, which is what makes
        // the common case one click.
        ParseIssuesSettings settings;
        QVERIFY2(settings.stderrOutput(), "output was not taken to have gone to stderr");
        QVERIFY2(settings.clearTasks(), "existing tasks were not cleared");

        // And each box carries its own text rather than a label beside it.
        QCOMPARE(settings.stderrOutput.presentation().labelPlacement,
                 AspectControls::LabelPlacement::AtControl);
        QCOMPARE(settings.clearTasks.presentation().labelPlacement,
                 AspectControls::LabelPlacement::AtControl);
    }

    void testTheOutputIsEditedOverSeveralLines()
    {
        // A build log is many lines; the one-line field would show the first.
        ParseIssuesSettings settings;
        QCOMPARE(settings.output.presentation().control, AspectControls::TextEdit);
    }

    void testWhichKitItOpensOn()
    {
        // With no active project to take a kit from, a desktop kit is the
        // sensible default for parsing a build log.
        QVERIFY2(firstDesktopKitId({}).isValid() == false,
                 "a kit was found among none");

        const QList<Kit *> kits = KitManager::kits();
        const Id desktop = firstDesktopKitId(kits);
        if (desktop.isValid()) {
            Kit *const kit = KitManager::kit(desktop);
            QVERIFY(kit);
            QCOMPARE(RunDeviceTypeKitAspect::deviceTypeId(kit),
                     Id(Constants::DESKTOP_DEVICE_TYPE));

            // And it is the first such kit, not merely one of them.
            for (const Kit *const k : kits) {
                if (k->id() == desktop)
                    break;
                QVERIFY2(RunDeviceTypeKitAspect::deviceTypeId(k)
                             != Id(Constants::DESKTOP_DEVICE_TYPE),
                         "an earlier desktop kit was passed over");
            }
        } else {
            QSKIP("no desktop kit is configured on this machine");
        }
    }

    void testHowTheOutputReachesTheParsers()
    {
        // One call per line, which is how a build feeds the parsers that carry
        // state from one line to the next. The newline the dialog puts back is
        // what ends each one - the formatter takes it off again before the
        // parser sees it, so without it the whole log would arrive as a single
        // line.
        Recorded recorded;
        parseOutput("first\nsecond\nthird", StdErrFormat, {new RecordingParser(&recorded)});

        QCOMPARE(recorded.lines.size(), 3);
        QCOMPARE(recorded.lines.at(0), QString("first"));
        QCOMPARE(recorded.lines.at(1), QString("second"));
        // The last line has no newline of its own in the box; flush() is what
        // gets it to the parsers.
        QCOMPARE(recorded.lines.at(2), QString("third"));

        // The format the reader chose is what they are told.
        QCOMPARE(recorded.formats.at(0), StdErrFormat);

        Recorded other;
        parseOutput("only", StdOutFormat, {new RecordingParser(&other)});
        QCOMPARE(other.formats, QList<OutputFormat>{StdOutFormat});
    }

    void testLoadingOutputFromAFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const FilePath file = FilePath::fromString(dir.filePath("log.txt"));
        QVERIFY(file.writeFileContents("error: something\n"));
        const Result<QString> text = readBuildOutput(file);
        QVERIFY2(text, qPrintable(text ? QString() : text.error()));
        QCOMPARE(*text, QString("error: something\n"));

        // A file that cannot be read is said so rather than emptying the box.
        const Result<QString> missing
            = readBuildOutput(FilePath::fromString(dir.filePath("nope.txt")));
        QVERIFY2(!missing, "a file that is not there was read anyway");
    }

    void testTheOkButtonFollowsTheKit()
    {
        ParseIssuesDialog dlg;
        QPushButton *const ok = dlg.m_buttonBox->button(QDialogButtonBox::Ok);
        QVERIFY(ok);
        QCOMPARE(ok->isEnabled(), dlg.m_settings->kitChooser.currentKit() != nullptr);
    }
};

QObject *createParseIssuesDialogTest()
{
    return new ParseIssuesDialogTest;
}

#endif // WITH_TESTS

} // ProjectExplorer::Internal

#ifdef WITH_TESTS
#include "parseissuesdialog.moc"
#endif
