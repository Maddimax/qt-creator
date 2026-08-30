// Copyright (C) 2022 The Qt Company Ltd
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "opensquishsuitesdialog.h"

#include "squishtr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspectpresentation.h>
#include <utils/aspects.h>
#include <utils/treemodel.h>
#include <utils/pathchooser.h>

#include <QAbstractButton>
#include <QDialog>
#ifdef WITH_TESTS
#include <QTemporaryDir>
#include <QTest>
#endif

#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Utils;

namespace Squish::Internal {

// What counts as a test suite under \a baseDir: a subdirectory whose name
// begins with "suite_" and which holds a readable suite.conf. Both halves
// matter - the name alone is a convention, the file is what makes it a suite.
//
// Kept out of the dialog because it is a question about a directory, and there
// the only way to ask it was to open the dialog and point it somewhere.
FilePaths suiteDirectoriesIn(const FilePath &baseDir)
{
    if (!baseDir.exists())
        return {};

    FilePaths suites;
    const FilePaths subDirs
        = baseDir.dirEntries(DirFilterFlag::Dirs | DirFilterFlag::NoDotAndDotDot);
    for (const FilePath &subDir : subDirs) {
        if (!subDir.baseName().startsWith("suite_"))
            continue;
        if (subDir.pathAppended("suite.conf").isReadableFile())
            suites.append(subDir);
    }
    return suites;
}

class SuiteItem : public TreeItem
{
public:
    SuiteItem(const FilePath &path, bool chosen) : m_path(path), m_chosen(chosen) {}

    FilePath path() const { return m_path; }
    bool chosen() const { return m_chosen; }
    void setChosen(bool chosen) { m_chosen = chosen; }

private:
    QVariant data(int column, int role) const override
    {
        if (role == AspectTable::CheckableRole)
            return flags(column).testFlag(Qt::ItemIsUserCheckable);
        if (role == Qt::DisplayRole)
            return m_path.baseName();
        if (role == Qt::CheckStateRole)
            return m_chosen ? Qt::Checked : Qt::Unchecked;
        return {};
    }

    bool setData(int, const QVariant &data, int role) override
    {
        if (role != Qt::CheckStateRole)
            return false;
        m_chosen = data.toInt() == Qt::Checked;
        return true;
    }

    Qt::ItemFlags flags(int) const override
    {
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
    }

    const FilePath m_path;
    bool m_chosen;
};

class SuiteModel : public TreeModel<TreeItem, SuiteItem>
{
public:
    SuiteModel() { setHeader({Tr::tr("Test suites:")}); }

    QHash<int, QByteArray> roleNames() const override
    {
        return AspectTable::withRoleNames(TreeModel::roleNames());
    }
};

class SuitesAspect final : public BaseAspect
{
public:
    SuitesAspect(AspectContainer *container, SuiteModel *model)
        : BaseAspect(container), m_model(model)
    {}

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Table;
        return p;
    }

    QAbstractItemModel *tableModel() override { return m_model; }

private:
    SuiteModel * const m_model;
};

class OpenSuitesSettings final : public AspectContainer
{
    Q_OBJECT

public:
    OpenSuitesSettings()
        : suites(this, &m_model)
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Squish/OpenSquishSuitesDialog.qml"));

        directory.setQmlName("Directory");
        directory.setLabelText(Tr::tr("Base directory:"));
        directory.setExpectedKind(PathChooserKind::ExistingDirectory);
        directory.setHistoryCompleter("Squish.SuitesBase");

        suites.setQmlName("Suites");

        selectAll.setQmlName("SelectAll");
        selectAll.setActionText(Tr::tr("Select All"));
        selectAll.setAction([this] { setAllChosen(true); });

        deselectAll.setQmlName("DeselectAll");
        deselectAll.setActionText(Tr::tr("Deselect All"));
        deselectAll.setAction([this] { setAllChosen(false); });

        directory.addOnChanged(this, [this] { rescan(); });
        rescan();
    }

    // A suite the reader has not seen before starts chosen: they asked for a
    // directory, so everything in it is what they meant.
    void rescan()
    {
        m_model.clear();
        for (const FilePath &suite : suiteDirectoriesIn(directory()))
            m_model.rootItem()->appendChild(new SuiteItem(suite, true));
        emit chosenChanged();
    }

    void setAllChosen(bool chosen)
    {
        TreeItem *const root = m_model.rootItem();
        for (int row = 0; row < root->childCount(); ++row) {
            m_model.setData(m_model.index(row, 0),
                            chosen ? Qt::Checked : Qt::Unchecked,
                            Qt::CheckStateRole);
        }
    }

    FilePaths chosenSuites() const
    {
        FilePaths chosen;
        const TreeItem *const root = m_model.rootItem();
        for (int row = 0; row < root->childCount(); ++row) {
            const auto item = static_cast<const SuiteItem *>(root->childAt(row));
            if (item->chosen())
                chosen.append(item->path());
        }
        return chosen;
    }

    // Opening none of them is not opening anything.
    bool hasChosenSuite() const { return !chosenSuites().isEmpty(); }

    FilePathAspect directory{this};
    SuitesAspect suites;
    ActionAspect selectAll{this};
    ActionAspect deselectAll{this};

signals:
    void chosenChanged();

private:
    SuiteModel m_model;
};

OpenSquishSuitesDialog::OpenSquishSuitesDialog(QWidget *parent)
    : QDialog(parent)
    , m_settings(new OpenSuitesSettings)
    , m_buttonBox(new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Open, this))
{
    setWindowTitle(Tr::tr("Open Squish Test Suites"));
    setModal(true);

    const auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(m_buttonBox);

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Whether there is anything to open follows both the directory and the
    // boxes, so the model is watched as well as the container.
    const auto refreshOpen = [this] {
        m_buttonBox->button(QDialogButtonBox::Open)->setEnabled(m_settings->hasChosenSuite());
    };
    connect(m_settings.get(), &OpenSuitesSettings::chosenChanged, this, refreshOpen);
    connect(m_settings->suites.tableModel(), &QAbstractItemModel::dataChanged,
            this, refreshOpen);
    connect(m_settings->suites.tableModel(), &QAbstractItemModel::modelReset,
            this, refreshOpen);
    refreshOpen();
}

OpenSquishSuitesDialog::~OpenSquishSuitesDialog() = default;

FilePaths OpenSquishSuitesDialog::chosenSuites() const
{
    return m_settings->chosenSuites();
}

#ifdef WITH_TESTS

class OpenSquishSuitesTest final : public QObject
{
    Q_OBJECT

    // A directory holding one real suite, one that only looks like one, and
    // one that is not named like one at all.
    static void makeSuites(const FilePath &root)
    {
        const FilePath real = root / "suite_real";
        QVERIFY(real.createDir());
        QVERIFY(real.pathAppended("suite.conf").writeFileContents("AUT app"));

        const FilePath named = root / "suite_without_conf";
        QVERIFY(named.createDir());

        const FilePath other = root / "not_a_suite";
        QVERIFY(other.createDir());
        QVERIFY(other.pathAppended("suite.conf").writeFileContents("AUT app"));
    }

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        OpenSuitesSettings settings;
        const Utils::Result<> rendered
            = Core::aspectFormRenders(&settings, "OpenSquishSuitesDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhatCountsAsASuite()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const FilePath root = FilePath::fromString(temp.path());
        makeSuites(root);

        const FilePaths found = suiteDirectoriesIn(root);
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().baseName(), QString("suite_real"));

        // Both halves of the rule are load-bearing: the name is a convention
        // and the file is what makes it a suite.
        QVERIFY2(!found.contains(root / "suite_without_conf"),
                 "a directory named like a suite but holding none was offered");
        QVERIFY2(!found.contains(root / "not_a_suite"),
                 "a directory holding a suite.conf but not named like one was offered");

        // A directory that is not there answers nothing rather than refusing.
        QVERIFY(suiteDirectoriesIn(root / "no-such-directory").isEmpty());
    }

    void testEverythingFoundStartsChosen()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const FilePath root = FilePath::fromString(temp.path());
        makeSuites(root);

        OpenSuitesSettings settings;
        settings.directory.setValue(root);

        // Asking for the directory is what asked for its suites.
        QCOMPARE(settings.chosenSuites().size(), 1);
        QVERIFY(settings.hasChosenSuite());

        settings.deselectAll.triggerAction();
        QVERIFY(settings.chosenSuites().isEmpty());
        QVERIFY2(!settings.hasChosenSuite(), "opening none of them counts as opening something");

        settings.selectAll.triggerAction();
        QCOMPARE(settings.chosenSuites().size(), 1);
    }

    void testPointingSomewhereElseForgetsWhatWasFound()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const FilePath root = FilePath::fromString(temp.path());
        makeSuites(root);

        OpenSuitesSettings settings;
        settings.directory.setValue(root);
        QCOMPARE(settings.chosenSuites().size(), 1);

        // The list is of the directory, not of everything ever looked at.
        settings.directory.setValue(root / "not_a_suite");
        QVERIFY2(settings.chosenSuites().isEmpty(),
                 "suites from the previous directory were still offered");
    }
};

QObject *createOpenSquishSuitesTest()
{
    return new OpenSquishSuitesTest;
}

#endif // WITH_TESTS

} // namespace Squish::Internal

#include "opensquishsuitesdialog.moc"
