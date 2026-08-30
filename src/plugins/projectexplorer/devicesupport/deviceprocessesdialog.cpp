// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "deviceprocessesdialog.h"

#include "devicekitaspects.h"
#include "idevice.h"
#include "processlist.h"
#include "../kitchooser.h"
#include "../projectexplorertr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>
#include <utils/guiutils.h>
#include <utils/processinfo.h>
#include <utils/qtcassert.h>

#ifdef WITH_TESTS
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTest>
#endif

#include <QDialogButtonBox>
#include <QIdentityProxyModel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Utils;

namespace ProjectExplorer {
namespace Internal {

// The rows a device's process list holds, and which of them the reader is on.
//
// The proxy is the point: the aspect hands the same model out for as long as it
// lives, while what is *behind* it changes with the device. A Quick table reads
// its model once - the binding calls AspectModels.tableModel() and a call
// records no dependency - so an aspect that answered a different model after
// the form was built would be showing the first one forever.
class DeviceProcessesSettings final : public AspectContainer
{
public:
    DeviceProcessesSettings()
    {
        setAutoApply(true);
        setQmlSource(
            QUrl("qrc:/qt/qml/QtCreator/ProjectExplorer/DeviceProcessesDialog.qml"));

        kitChooser.setQmlName("Kit");
        kitChooser.kit.setLabelText(Tr::tr("Kit:"));

        processes.setQmlName("Processes");
        // The proxy is the point: the aspect hands out the same model for as
        // long as it lives, while what is *behind* it changes with the device.
        // A Quick table reads its model once, so an aspect that answered a
        // different model after the form was built would show the first one
        // forever.
        processes.setModel(&rows);
        processes.setFilterPlaceholderText(Tr::tr("Filter"));
        // Sorted by the command line, as the widget view opened.
        processes.setSortColumn(1);

        error.setQmlName("Error");
        error.setVisible(false);
    }

    // Swapping what the proxy shows, and forgetting what was chosen with it:
    // the row the reader was on is a row of the list that has gone.
    void showRowsOf(QAbstractItemModel *model)
    {
        rows.setSourceModel(model);
        processes.setCurrentRow(-1);
        processes.setSelectedRows({});
    }

    KitChooserAspect kitChooser{this};
    QIdentityProxyModel rows;
    TableAspect processes{this};
    TextDisplay error{this};
};

} // namespace Internal

using namespace Internal;

/*!
     \class ProjectExplorer::DeviceProcessesDialog

     \brief The DeviceProcessesDialog class shows a list of processes.

     The dialog can be used as a:
     \list
     \li Non-modal dialog showing a list of processes. Call addCloseButton()
         to add a \gui Close button.
     \li Modal dialog with an \gui Accept button to select a process. Call
         addAcceptButton() passing the label text. This will create a
         \gui Cancel button as well.
     \endlist
*/

class Internal::DeviceProcessesDialogPrivate
{
public:
    std::unique_ptr<ProcessList> processList;
    DeviceProcessesSettings settings;
    QPushButton *updateListButton = nullptr;
    QPushButton *killProcessButton = nullptr;
    QPushButton *acceptButton = nullptr;
    QDialogButtonBox *buttonBox = nullptr;
};

DeviceProcessesDialog::DeviceProcessesDialog()
    : QDialog(dialogParent())
    , d(std::make_unique<Internal::DeviceProcessesDialogPrivate>())
{
    setWindowTitle(Tr::tr("List of Processes"));
    setMinimumHeight(500);

    d->settings.kitChooser.populate();

    d->buttonBox = new QDialogButtonBox(this);
    d->updateListButton = new QPushButton(Tr::tr("&Update List"), this);
    d->killProcessButton = new QPushButton(Tr::tr("&Kill Process"), this);
    d->buttonBox->addButton(d->updateListButton, QDialogButtonBox::ActionRole);
    d->buttonBox->addButton(d->killProcessButton, QDialogButtonBox::ActionRole);

    const auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(&d->settings));
    layout->addWidget(d->buttonBox);

    connect(d->updateListButton, &QAbstractButton::clicked,
            this, &DeviceProcessesDialog::updateProcessList);
    connect(d->killProcessButton, &QAbstractButton::clicked,
            this, &DeviceProcessesDialog::killProcess);
    connect(&d->settings.kitChooser.kit, &BaseAspect::changed,
            this, &DeviceProcessesDialog::updateDevice);
    connect(&d->settings.processes, &TableAspect::chosenChanged,
            this, &DeviceProcessesDialog::updateButtons);
    connect(d->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(d->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateButtons();
}

DeviceProcessesDialog::~DeviceProcessesDialog() = default;

void DeviceProcessesDialog::addAcceptButton(const QString &label)
{
    d->acceptButton = new QPushButton(label);
    d->buttonBox->addButton(d->acceptButton, QDialogButtonBox::AcceptRole);
    d->buttonBox->addButton(QDialogButtonBox::Cancel);
    updateButtons();
}

void DeviceProcessesDialog::addCloseButton()
{
    d->buttonBox->addButton(QDialogButtonBox::Close);
}

void DeviceProcessesDialog::setKitVisible(bool v)
{
    d->settings.kitChooser.setVisible(v);
}

void DeviceProcessesDialog::setDevice(const IDevice::ConstPtr &device)
{
    setKitVisible(false);
    setDeviceToList(device);
}

void DeviceProcessesDialog::setDeviceToList(const IDevice::ConstPtr &device)
{
    d->processList.reset();
    d->settings.showRowsOf(nullptr);
    if (!device)
        return;

    d->processList.reset(new ProcessList(device->shared_from_this(), this));
    QTC_ASSERT(d->processList, return);
    d->settings.showRowsOf(d->processList->model());

    connect(d->processList.get(), &ProcessList::error,
            this, &DeviceProcessesDialog::handleRemoteError);
    connect(d->processList.get(), &ProcessList::processListUpdated,
            this, &DeviceProcessesDialog::handleProcessListUpdated);
    connect(d->processList.get(), &ProcessList::processKilled,
            this, &DeviceProcessesDialog::updateProcessList, Qt::QueuedConnection);

    updateButtons();
    updateProcessList();
}

void DeviceProcessesDialog::showAllDevices()
{
    setKitVisible(true);
    updateDevice();
}

void DeviceProcessesDialog::updateDevice()
{
    setDeviceToList(RunDeviceKitAspect::device(d->settings.kitChooser.currentKit()));
}

void DeviceProcessesDialog::updateProcessList()
{
    d->updateListButton->setEnabled(false);
    d->killProcessButton->setEnabled(false);
    if (d->processList)
        d->processList->update();
}

void DeviceProcessesDialog::killProcess()
{
    if (!d->processList || !d->settings.processes.hasSelection())
        return;
    d->updateListButton->setEnabled(false);
    d->killProcessButton->setEnabled(false);
    d->processList->killProcess(d->settings.processes.currentRow());
}

void DeviceProcessesDialog::handleRemoteError(const QString &errorMsg)
{
    QMessageBox::critical(this, Tr::tr("Remote Error"), errorMsg);
    d->updateListButton->setEnabled(true);
    updateButtons();
}

void DeviceProcessesDialog::handleProcessListUpdated()
{
    d->updateListButton->setEnabled(true);
    updateButtons();
}

void DeviceProcessesDialog::updateButtons()
{
    const bool hasSelection = d->settings.processes.hasSelection();
    if (d->acceptButton)
        d->acceptButton->setEnabled(hasSelection);
    d->killProcessButton->setEnabled(hasSelection);
    d->settings.error.setVisible(!d->settings.error.text().isEmpty());
}

ProcessInfo DeviceProcessesDialog::currentProcess() const
{
    if (!d->processList || !d->settings.processes.hasSelection())
        return {};
    return d->processList->at(d->settings.processes.currentRow());
}

KitChooserAspect &DeviceProcessesDialog::kitChooser() const
{
    return d->settings.kitChooser;
}

void DeviceProcessesDialog::logMessage(const QString &line)
{
    const QString text = d->settings.error.text();
    d->settings.error.setText(text.isEmpty() ? line : text + '\n' + line);
    d->settings.error.setVisible(true);
}

#ifdef WITH_TESTS

namespace Internal {

class DeviceProcessesDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        DeviceProcessesSettings settings;
        const Result<> rendered
            = Core::aspectFormRenders(&settings, "DeviceProcessesDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testTheTableItAsksFor()
    {
        DeviceProcessesSettings settings;
        const AspectPresentation p = settings.processes.presentation();

        QCOMPARE(p.control, AspectControls::Table);
        // The widget view opened sorted by the command line and offered a
        // filter field over the list.
        QCOMPARE(p.sortColumn, 1);
        QVERIFY2(!p.filterPlaceholderText.isEmpty(), "the list offered no filter");
    }

    void testTheModelItHandsOutDoesNotChange()
    {
        // A Quick table reads its model once, so the aspect has to answer the
        // same one for as long as it lives - the device behind it is what
        // changes.
        DeviceProcessesSettings settings;
        QAbstractItemModel *const first = settings.processes.tableModel();
        QVERIFY(first);
        QCOMPARE(first->rowCount(), 0);

        ProcessList list(nullptr, nullptr);
        settings.showRowsOf(list.model());
        QCOMPARE(settings.processes.tableModel(), first);

        settings.showRowsOf(nullptr);
        QCOMPARE(settings.processes.tableModel(), first);
        QCOMPARE(first->rowCount(), 0);
    }

    void testWhatTheReaderIsOn()
    {
        DeviceProcessesSettings settings;
        QVERIFY2(!settings.processes.hasSelection(), "a fresh list had something selected");
        QCOMPARE(settings.processes.currentRow(), -1);

        QSignalSpy spy(&settings.processes, &TableAspect::chosenChanged);
        settings.processes.setCurrentRow(3);
        QCOMPARE(settings.processes.currentRow(), 3);
        QVERIFY(settings.processes.hasSelection());
        QCOMPARE(spy.count(), 1);

        // Saying the same thing again is not a change.
        settings.processes.setCurrentRow(3);
        QCOMPARE(spy.count(), 1);

        // And a new device starts with nothing chosen: the row the reader was
        // on is a row of the list that has gone.
        settings.processes.setCurrentRow(2);
        settings.showRowsOf(nullptr);
        QVERIFY2(!settings.processes.hasSelection(),
                 "the row from the previous device was still selected");
    }

    void testPickingARowInTheDrawnTableReachesTheDialog()
    {
        // The one thing C++ cannot see by calling setCurrentRow() itself: the
        // .qml has to hand the row back, and a page that dropped that line
        // would still pass every test above.
        DeviceProcessesSettings settings;

        QStandardItemModel rows(3, 2);
        for (int i = 0; i < 3; ++i) {
            rows.setItem(i, 0, new QStandardItem(QString::number(1000 + i)));
            rows.setItem(i, 1, new QStandardItem(QString("process%1").arg(i)));
        }
        settings.showRowsOf(&rows);

        const std::unique_ptr<QWidget> form(Core::createAspectForm(&settings));
        QVERIFY(form);
        QObject *const root = Core::aspectFormRoot(form.get());
        QVERIFY2(root, "no front end said what the page was drawn from");

        QObject *table = nullptr;
        QTRY_VERIFY(table = root->findChild<QObject *>("processTable"));

        QVERIFY(QMetaObject::invokeMethod(table, "selectRow", Q_ARG(int, 1)));
        QTRY_COMPARE(settings.processes.currentRow(), 1);
        QVERIFY(settings.processes.hasSelection());
    }

    void testTheErrorLineIsShownOnlyWhenThereIsOne()
    {
        // The container's own default, not the dialog's: the dialog calls
        // updateButtons() on the way up, which hides it too, so asking the
        // dialog tests whichever of the two happens to run.
        DeviceProcessesSettings bare;
        QVERIFY2(!bare.error.isVisible(), "an empty error line was shown");

        DeviceProcessesDialog dlg;
        QVERIFY2(!dlg.d->settings.error.isVisible(), "an empty error line was shown");

        dlg.logMessage("could not reach the device");
        QVERIFY(dlg.d->settings.error.isVisible());
        QCOMPARE(dlg.d->settings.error.text(), QString("could not reach the device"));

        // Lines accumulate, as the text browser appended them.
        dlg.logMessage("and again");
        QCOMPARE(dlg.d->settings.error.text(),
                 QString("could not reach the device\nand again"));
    }

    void testTheButtonsFollowTheSelection()
    {
        DeviceProcessesDialog dlg;
        dlg.addAcceptButton("Attach");

        QVERIFY2(!dlg.d->acceptButton->isEnabled(),
                 "a process could be attached to without one being chosen");
        QVERIFY2(!dlg.d->killProcessButton->isEnabled(),
                 "a process could be killed without one being chosen");

        dlg.d->settings.processes.setCurrentRow(0);
        QVERIFY(dlg.d->acceptButton->isEnabled());
        QVERIFY(dlg.d->killProcessButton->isEnabled());
    }

    void testTheKitIsShownOnlyWhereThereIsAChoice()
    {
        // Told which device to list, there is nothing to choose.
        DeviceProcessesDialog chosen;
        chosen.setDevice({});
        QVERIFY2(!chosen.kitChooser().isVisible(), "the kit was offered for a fixed device");

        DeviceProcessesDialog all;
        all.showAllDevices();
        QVERIFY(all.kitChooser().isVisible());
    }
};

QObject *createDeviceProcessesDialogTest()
{
    return new DeviceProcessesDialogTest;
}

} // namespace Internal

#endif // WITH_TESTS

} // namespace ProjectExplorer

#include "deviceprocessesdialog.moc"
