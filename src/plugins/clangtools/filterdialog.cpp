// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "filterdialog.h"

#include "clangtoolstr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/algorithm.h>
#include <utils/aspects.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QAbstractTableModel>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Utils;

namespace ClangTools::Internal {

enum Columns { CheckName, Count, ColumnCount };

// The order the checks are listed in: by what they are called, so that a
// reader looking for one can find it.
Checks sortedChecks(const Checks &checks)
{
    return Utils::sorted(checks, [](const Check &lhs, const Check &rhs) {
        return lhs.displayName < rhs.displayName;
    });
}

// Which rows a button picks. "Select All with Fixits" is only worth offering
// where some check has one.
QList<int> rowsWithFixits(const Checks &checks)
{
    QList<int> rows;
    for (int row = 0; row < checks.size(); ++row) {
        if (checks.at(row).hasFixit)
            rows.append(row);
    }
    return rows;
}

// Which rows the dialog opens on: the checks that are not filtered out now.
QList<int> rowsShown(const Checks &checks)
{
    QList<int> rows;
    for (int row = 0; row < checks.size(); ++row) {
        if (checks.at(row).isShown)
            rows.append(row);
    }
    return rows;
}

class FilterChecksModel final : public QAbstractTableModel
{
public:
    explicit FilterChecksModel(const Checks &checks)
        : m_checks(sortedChecks(checks))
    {}

    const Checks &checks() const { return m_checks; }

    QSet<QString> namesAt(const QList<int> &rows) const
    {
        QSet<QString> names;
        for (int row : rows) {
            if (row >= 0 && row < m_checks.size())
                names << m_checks.at(row).name;
        }
        return names;
    }

    int rowCount(const QModelIndex &parent = {}) const override
    { return parent.isValid() ? 0 : m_checks.size(); }
    int columnCount(const QModelIndex &parent = {}) const override
    { return parent.isValid() ? 0 : ColumnCount; }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role == AspectTable::EditableRole)
            return false;
        if (!index.isValid() || index.row() >= m_checks.size() || role != Qt::DisplayRole)
            return {};
        const Check &check = m_checks.at(index.row());
        return index.column() == CheckName ? QVariant(check.displayName) : QVariant(check.count);
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};
        return section == CheckName ? Tr::tr("Check") : QString("#");
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    { return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags; }

    QHash<int, QByteArray> roleNames() const override
    { return AspectTable::withRoleNames(QAbstractTableModel::roleNames()); }

private:
    Checks m_checks;
};

class FilterSettings final : public AspectContainer
{
public:
    explicit FilterSettings(const Checks &checks)
        : model(checks)
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ClangTools/FilterDialog.qml"));

        message.setQmlName("Message");
        message.setText(Tr::tr("Select the diagnostics to display."));

        selectAll.setQmlName("SelectAll");
        selectAll.setActionText(Tr::tr("Select All"));
        selectAll.setAction([this] { selectRows(allRows()); });

        selectWithFixits.setQmlName("SelectWithFixits");
        selectWithFixits.setActionText(Tr::tr("Select All with Fixits"));
        selectWithFixits.setAction([this] { selectRows(rowsWithFixits(model.checks())); });
        // Nothing to pick where no check offers one.
        selectWithFixits.setEnabled(!rowsWithFixits(model.checks()).isEmpty());

        selectNone.setQmlName("SelectNone");
        selectNone.setActionText(Tr::tr("Clear Selection"));
        selectNone.setAction([this] { selectRows({}); });

        this->checks.setQmlName("Checks");
        this->checks.setModel(&model);

        selectRows(rowsShown(model.checks()));
    }

    QSet<QString> selectedChecks() const { return model.namesAt(checks.selectedRows()); }

    TextDisplay message{this};
    ActionAspect selectAll{this};
    ActionAspect selectWithFixits{this};
    ActionAspect selectNone{this};
    TableAspect checks{this};
    FilterChecksModel model;

private:
    QList<int> allRows() const
    {
        QList<int> rows;
        for (int row = 0; row < model.rowCount(); ++row)
            rows.append(row);
        return rows;
    }
    void selectRows(const QList<int> &rows)
    {
        checks.setSelectedRows(Utils::transform(rows, [](int row) { return QVariant(row); }));
    }
};

FilterDialog::FilterDialog(const Checks &checks, QWidget *parent)
    : QDialog(parent)
    , d(new FilterSettings(checks))
{
    resize(400, 400);
    setWindowTitle(Tr::tr("Filter Diagnostics"));

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);

    auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(d.get()));
    layout->addWidget(buttonBox);

    // Showing nothing is not a filter, so Ok is off until something is picked.
    const auto updateOk = [this, buttonBox] {
        buttonBox->button(QDialogButtonBox::Ok)
            ->setEnabled(!d->checks.selectedRows().isEmpty());
    };
    connect(&d->checks, &TableAspect::chosenChanged, this, updateOk);
    updateOk();

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

FilterDialog::~FilterDialog() = default;

QSet<QString> FilterDialog::selectedChecks() const
{
    return d->selectedChecks();
}

#ifdef WITH_TESTS

class FilterDialogTest final : public QObject
{
    Q_OBJECT

private:
    static Checks threeChecks()
    {
        return {{"modernize-b", "Modernize B", 2, true, false},
                {"bugprone-a", "Bugprone A", 5, false, true},
                {"readability-c", "Readability C", 1, true, true}};
    }

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        FilterSettings settings(threeChecks());
        const Result<> rendered = Core::aspectFormRenders(&settings, "FilterDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testTheChecksAreListedByName()
    {
        // So that a reader looking for one can find it, whatever order the
        // diagnostics came in.
        const Checks sorted = sortedChecks(threeChecks());
        QCOMPARE(sorted.size(), 3);
        QCOMPARE(sorted.at(0).displayName, QString("Bugprone A"));
        QCOMPARE(sorted.at(1).displayName, QString("Modernize B"));
        QCOMPARE(sorted.at(2).displayName, QString("Readability C"));
    }

    void testWhichRowsTheButtonsPick()
    {
        // Against the sorted order, because that is what the rows are.
        const Checks sorted = sortedChecks(threeChecks());
        QCOMPARE(rowsWithFixits(sorted), (QList<int>{0, 2}));
        QCOMPARE(rowsShown(sorted), (QList<int>{1, 2}));

        QVERIFY(rowsWithFixits({}).isEmpty());
        QVERIFY(rowsShown({}).isEmpty());
    }

    void testTheDialogOpensOnWhatIsNotFilteredOut()
    {
        FilterSettings settings(threeChecks());
        QCOMPARE(settings.selectedChecks(), QSet<QString>({"modernize-b", "readability-c"}));
    }

    void testTheThreeButtonsPickTheThreeSets()
    {
        FilterSettings settings(threeChecks());

        settings.selectAll.triggerAction();
        QCOMPARE(settings.selectedChecks(),
                 QSet<QString>({"modernize-b", "bugprone-a", "readability-c"}));

        settings.selectWithFixits.triggerAction();
        QCOMPARE(settings.selectedChecks(), QSet<QString>({"bugprone-a", "readability-c"}));

        settings.selectNone.triggerAction();
        QVERIFY(settings.selectedChecks().isEmpty());
    }

    void testFixitsAreNotOfferedWhenThereAreNone()
    {
        const Checks none = {{"a", "A", 1, true, false}};
        FilterSettings settings(none);
        QVERIFY2(!settings.selectWithFixits.isEnabled(),
                 "a button that would pick nothing was offered");

        FilterSettings some(threeChecks());
        QVERIFY(some.selectWithFixits.isEnabled());
    }

    void testTheRowsAreReadNotWritten()
    {
        FilterChecksModel model(threeChecks());
        const QVariant editable = model.data(model.index(0, 0), AspectTable::EditableRole);
        QVERIFY2(editable.isValid(), "the table was never told whether a cell may be written to");
        QVERIFY2(!editable.toBool(), "a check could be renamed by typing in the list");
        QCOMPARE(model.headerData(CheckName, Qt::Horizontal, Qt::DisplayRole).toString(),
                 Tr::tr("Check"));
        QCOMPARE(model.data(model.index(0, Count), Qt::DisplayRole).toInt(), 5);
    }
};

QObject *createFilterDialogTest()
{
    return new FilterDialogTest;
}

#endif // WITH_TESTS

} // ClangTools::Internal

#include "filterdialog.moc"
