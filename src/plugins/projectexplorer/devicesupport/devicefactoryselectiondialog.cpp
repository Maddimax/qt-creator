// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "devicefactoryselectiondialog.h"

#include "idevicefactory.h"
#include "../projectexplorertr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QAbstractTableModel>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Utils;

namespace ProjectExplorer::Internal {

// The kinds of device that can be made, by display name. Each row carries the
// factory's type, which is the whole answer the dialog gives.
class DeviceFactoryModel final : public QAbstractTableModel
{
public:
    DeviceFactoryModel()
    {
        for (const IDeviceFactory * const factory : IDeviceFactory::allDeviceFactories()) {
            if (factory->canCreate())
                m_rows.append({factory->displayName(), factory->deviceType()});
        }
    }

    Id idAt(int row) const
    { return row >= 0 && row < m_rows.size() ? m_rows.at(row).id : Id(); }

    int rowCount(const QModelIndex &parent = {}) const override
    { return parent.isValid() ? 0 : m_rows.size(); }
    int columnCount(const QModelIndex &parent = {}) const override
    { return parent.isValid() ? 0 : 1; }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role == AspectTable::EditableRole)
            return false;
        if (!index.isValid() || index.row() >= m_rows.size() || role != Qt::DisplayRole)
            return {};
        return m_rows.at(index.row()).displayName;
    }

    // One nameless column, so no heading.
    QVariant headerData(int, Qt::Orientation, int) const override { return {}; }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    { return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags; }

    QHash<int, QByteArray> roleNames() const override
    { return AspectTable::withRoleNames(QAbstractTableModel::roleNames()); }

private:
    struct Row { QString displayName; Id id; };
    QList<Row> m_rows;
};

class DeviceFactorySelectionSettings final : public AspectContainer
{
public:
    DeviceFactorySelectionSettings()
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ProjectExplorer/DeviceFactorySelectionDialog.qml"));

        types.setQmlName("Types");
        types.setLabelText(Tr::tr("Available device types:"));
        types.setModel(&model);
    }

    Id selectedId() const { return model.idAt(types.currentRow()); }

    TableAspect types{this};
    DeviceFactoryModel model;
};

DeviceFactorySelectionDialog::DeviceFactorySelectionDialog(QWidget *parent)
    : QDialog(parent)
    , m_settings(new DeviceFactorySelectionSettings)
{
    resize(420, 330);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_buttonBox->button(QDialogButtonBox::Ok)->setText(Tr::tr("Start Wizard"));

    auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(m_buttonBox);

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &DeviceFactorySelectionDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &DeviceFactorySelectionDialog::reject);
    // Nothing picked is no wizard to start.
    connect(&m_settings->types, &TableAspect::chosenChanged, this, [this] {
        m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(m_settings->types.hasSelection());
    });
    // A kind chosen and meant - a double click, or Return - is the same as
    // pressing the button.
    connect(&m_settings->types, &TableAspect::rowActivated, this, &QDialog::accept);

    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
}

DeviceFactorySelectionDialog::~DeviceFactorySelectionDialog() = default;

Utils::Id DeviceFactorySelectionDialog::selectedId() const
{
    return m_settings->selectedId();
}

#ifdef WITH_TESTS

class DeviceFactorySelectionDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        DeviceFactorySelectionSettings settings;
        const Result<> rendered
            = Core::aspectFormRenders(&settings, "DeviceFactorySelectionDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testOnlyTheKindsThatCanBeMadeAreOffered()
    {
        // A factory that cannot create is a device kind that can be shown but
        // not started from here, so the list leaves it out.
        DeviceFactoryModel model;
        int creatable = 0;
        for (const IDeviceFactory * const factory : IDeviceFactory::allDeviceFactories()) {
            if (factory->canCreate())
                ++creatable;
        }
        QCOMPARE(model.rowCount(), creatable);
    }

    void testEachRowCarriesTheTypeItStandsFor()
    {
        DeviceFactoryModel model;
        if (model.rowCount() == 0)
            QSKIP("no device factory in this build can create a device");

        QVERIFY2(model.idAt(0).isValid(), "a row was listed without the type it stands for");
        // And an index that is not there is no type at all, which is what the
        // dialog hands back before anything is picked.
        QVERIFY(!model.idAt(-1).isValid());
        QVERIFY(!model.idAt(model.rowCount()).isValid());
    }

    void testNothingPickedIsNoDeviceType()
    {
        const DeviceFactorySelectionDialog dialog;
        QVERIFY2(!dialog.selectedId().isValid(),
                 "the dialog names a device type before one is picked");
    }

    void testTheRowsAreReadNotWritten()
    {
        DeviceFactoryModel model;
        const QVariant editable = model.data(model.index(0, 0), AspectTable::EditableRole);
        QVERIFY2(editable.isValid(), "the table was never told whether a cell may be written to");
        QVERIFY2(!editable.toBool(), "a device kind could be renamed by typing in the list");
        QVERIFY2(model.headerData(0, Qt::Horizontal, Qt::DisplayRole).toString().isEmpty(),
                 "the nameless column came up with a heading");
    }
};

QObject *createDeviceFactorySelectionDialogTest()
{
    return new DeviceFactorySelectionDialogTest;
}

#endif // WITH_TESTS

} // namespace ProjectExplorer::Internal

#ifdef WITH_TESTS
#include "devicefactoryselectiondialog.moc"
#endif
