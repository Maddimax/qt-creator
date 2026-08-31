// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "processpickerdialog.h"

#include "profilertr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>
#include <utils/filepath.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QAbstractTableModel>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Utils;

namespace Profiler::Internal {

enum Column { PidColumn, NameColumn, CommandColumn, ColumnCount };

// What a process is listed as. The executable's own name rather than its whole
// path: the path is in the command line beside it.
QString processDisplayName(const ProcessInfo &info)
{
    return FilePath::fromUserInput(info.executable).fileName();
}

class ProcessListModel final : public QAbstractTableModel
{
public:
    void setProcesses(const QList<ProcessInfo> &processes)
    {
        beginResetModel();
        m_processes = processes;
        endResetModel();
    }

    std::optional<ProcessInfo> processAt(int row) const
    {
        if (row < 0 || row >= m_processes.size())
            return std::nullopt;
        return m_processes.at(row);
    }

    int rowCount(const QModelIndex &parent = {}) const override
    { return parent.isValid() ? 0 : m_processes.size(); }
    int columnCount(const QModelIndex &parent = {}) const override
    { return parent.isValid() ? 0 : ColumnCount; }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role == AspectTable::EditableRole)
            return false;
        if (!index.isValid() || index.row() >= m_processes.size() || role != Qt::DisplayRole)
            return {};
        const ProcessInfo &info = m_processes.at(index.row());
        switch (index.column()) {
        case PidColumn: return QString::number(info.processId);
        case NameColumn: return processDisplayName(info);
        case CommandColumn: return info.commandLine;
        default: return {};
        }
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};
        switch (section) {
        case PidColumn: return Tr::tr("Process ID");
        case NameColumn: return Tr::tr("Name");
        case CommandColumn: return Tr::tr("Command Line");
        default: return {};
        }
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    { return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags; }

    QHash<int, QByteArray> roleNames() const override
    { return AspectTable::withRoleNames(QAbstractTableModel::roleNames()); }

private:
    QList<ProcessInfo> m_processes;
};

class ProcessPickerSettings final : public AspectContainer
{
public:
    ProcessPickerSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Profiler/ProcessPickerDialog.qml"));

        processes.setQmlName("Processes");
        processes.setModel(&model);
        // The field over the list and the sorted rows were a proxy model and a
        // line edit wired to it; the table does both when it is told to.
        processes.setFilterPlaceholderText(
            Tr::tr("Filter by name, command line or process id"));
        processes.setSortColumn(NameColumn);

        if (const Result<QList<ProcessInfo>> infos = ProcessInfo::processInfoList())
            model.setProcesses(*infos);
    }

    std::optional<ProcessInfo> chosen() const { return model.processAt(processes.currentRow()); }

    TableAspect processes{this};
    ProcessListModel model;
};

ProcessPickerDialog::ProcessPickerDialog(QWidget *parent)
    : QDialog(parent)
    , d(new ProcessPickerSettings)
{
    setWindowTitle(Tr::tr("Attach to Process"));
    resize(700, 500);

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = buttons->button(QDialogButtonBox::Ok);
    m_okButton->setText(Tr::tr("Attach"));

    auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(d.get()));
    layout->addWidget(buttons);

    connect(&d->processes, &TableAspect::chosenChanged,
            this, &ProcessPickerDialog::updateOkButton);
    // A process chosen and meant - a double click, or Return - is the same as
    // pressing Attach.
    connect(&d->processes, &TableAspect::rowActivated, this, &QDialog::accept);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateOkButton();
}

ProcessPickerDialog::~ProcessPickerDialog() = default;

std::optional<ProcessInfo> ProcessPickerDialog::selectedProcess() const
{
    return d->chosen();
}

std::optional<ProcessInfo> ProcessPickerDialog::pickProcess(QWidget *parent)
{
#ifdef Q_OS_WASM
    // Attaching to a local process is a desktop-only capability (there are no host processes to
    // enumerate in the browser). A blocking QDialog::exec() would additionally abort on the
    // WebAssembly main thread, since QEventLoop::WaitForMoreEvents needs asyncify.
    Q_UNUSED(parent)
    return std::nullopt;
#else
    ProcessPickerDialog dialog(parent);
    if (dialog.exec() != QDialog::Accepted)
        return std::nullopt;
    return dialog.selectedProcess();
#endif
}

void ProcessPickerDialog::updateOkButton()
{
    m_okButton->setEnabled(selectedProcess().has_value());
}

#ifdef WITH_TESTS

class ProcessPickerDialogTest final : public QObject
{
    Q_OBJECT

private:
    static QList<ProcessInfo> twoProcesses()
    {
        ProcessInfo first;
        first.processId = 42;
        first.executable = "/usr/bin/zebra";
        first.commandLine = "/usr/bin/zebra --stripes";
        ProcessInfo second;
        second.processId = 7;
        second.executable = "/opt/bin/aardvark";
        second.commandLine = "/opt/bin/aardvark";
        return {first, second};
    }

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        ProcessPickerSettings settings;
        const Result<> rendered
            = Core::aspectFormRenders(&settings, "ProcessPickerDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testAProcessIsListedByItsOwnName()
    {
        // The path is in the command line beside it, so the name column is the
        // executable's name alone.
        QCOMPARE(processDisplayName(twoProcesses().at(0)), QString("zebra"));

        ProcessListModel model;
        model.setProcesses(twoProcesses());
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(0, NameColumn), Qt::DisplayRole).toString(),
                 QString("zebra"));
        QCOMPARE(model.data(model.index(0, PidColumn), Qt::DisplayRole).toString(),
                 QString("42"));
        QVERIFY(model.data(model.index(0, CommandColumn), Qt::DisplayRole)
                    .toString().contains("--stripes"));
    }

    void testARowMapsBackToItsProcess()
    {
        // The widget kept the list index in a role on the first column so that
        // a sorted or filtered row could be mapped back. The table reports in
        // the model's own rows, so the row is the index.
        ProcessListModel model;
        model.setProcesses(twoProcesses());
        QVERIFY(model.processAt(0).has_value());
        QCOMPARE(model.processAt(0)->processId, 42);
        QCOMPARE(model.processAt(1)->processId, 7);

        // And a row that is not there is no process, which is what the dialog
        // hands back before anything is picked.
        QVERIFY(!model.processAt(-1).has_value());
        QVERIFY(!model.processAt(2).has_value());
    }

    void testTheListIsFilteredAndSorted()
    {
        // Both were a QSortFilterProxyModel and a line edit wired to it.
        ProcessPickerSettings settings;
        QVERIFY2(!settings.processes.presentation().filterPlaceholderText.isEmpty(),
                 "there is no way to narrow a list of every process on the machine");
        QCOMPARE(settings.processes.presentation().sortColumn, int(NameColumn));
    }

    void testNothingPickedIsNoProcess()
    {
        const ProcessPickerDialog dialog;
        QVERIFY2(!dialog.selectedProcess().has_value(),
                 "the dialog names a process before one is picked");
    }

    void testTheRowsAreReadNotWritten()
    {
        ProcessListModel model;
        model.setProcesses(twoProcesses());
        const QVariant editable = model.data(model.index(0, 0), AspectTable::EditableRole);
        QVERIFY2(editable.isValid(), "the table was never told whether a cell may be written to");
        QVERIFY2(!editable.toBool(), "a process could be renamed by typing in the list");
        QCOMPARE(model.headerData(PidColumn, Qt::Horizontal, Qt::DisplayRole).toString(),
                 Tr::tr("Process ID"));
    }
};

QObject *createProcessPickerDialogTest()
{
    return new ProcessPickerDialogTest;
}

#endif // WITH_TESTS

} // namespace Profiler::Internal

#ifdef WITH_TESTS
#include "processpickerdialog.moc"
#endif
