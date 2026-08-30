// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "customqbspropertiesdialog.h"

#include "qbsprofilemanager.h"
#include "qbsprojectmanagertr.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspectpresentation.h>
#include <utils/aspects.h>
#include <utils/treemodel.h>
#include <utils/qtcassert.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QPushButton>
#include <QTableWidgetItem>

namespace QbsProjectManager::Internal {

class PropertyItem : public Utils::TreeItem
{
public:
    PropertyItem(const QString &name, const QString &value)
        : m_name(name), m_value(value)
    {}

    QString name() const { return m_name; }
    QString value() const { return m_value; }

private:
    QVariant data(int column, int role) const override
    {
        // Which cells can be typed into is in flags(), which QML cannot reach.
        if (role == Utils::AspectTable::EditableRole)
            return Utils::AspectTable::isWritable(flags(column));
        if (role != Qt::DisplayRole && role != Qt::EditRole)
            return {};
        return column == 0 ? m_name : m_value;
    }

    bool setData(int column, const QVariant &data, int) override
    {
        if (column == 0) {
            m_name = data.toString();
            return true;
        }
        if (column == 1) {
            m_value = data.toString();
            return true;
        }
        return false;
    }

    Qt::ItemFlags flags(int column) const override
    {
        return TreeItem::flags(column) | Qt::ItemIsEditable;
    }

    QString m_name;
    QString m_value;
};

class PropertyModel : public Utils::TreeModel<Utils::TreeItem, PropertyItem>
{
public:
    PropertyModel() { setHeader({Tr::tr("Key"), Tr::tr("Value")}); }

    QHash<int, QByteArray> roleNames() const override
    {
        return Utils::AspectTable::withRoleNames(TreeModel::roleNames());
    }
};

// The table, and which of its rows are picked - there is no view to ask.
class PropertiesAspect final : public Utils::BaseAspect
{
    Q_OBJECT

public:
    PropertiesAspect(Utils::AspectContainer *container, PropertyModel *model)
        : BaseAspect(container), m_model(model)
    {}

    Utils::AspectPresentation presentation() const override
    {
        Utils::AspectPresentation p = BaseAspect::presentation();
        p.control = Utils::AspectControls::Table;
        return p;
    }

    QAbstractItemModel *tableModel() override { return m_model; }

    Q_INVOKABLE void setSelectedRows(const QVariantList &rows)
    {
        m_selectedRows.clear();
        for (const QVariant &row : rows)
            m_selectedRows.append(row.toInt());
        emit selectionChanged();
    }

    QList<int> selectedRows() const { return m_selectedRows; }

signals:
    void selectionChanged();

private:
    PropertyModel * const m_model;
    QList<int> m_selectedRows;
};

class CustomPropertiesSettings final : public Utils::AspectContainer
{
public:
    explicit CustomPropertiesSettings(const QVariantMap &properties)
        : table(this, &m_model)
    {
        setAutoApply(true);
        setQmlSource(
            QUrl("qrc:/qt/qml/QtCreator/QbsProjectManager/CustomQbsPropertiesDialog.qml"));

        for (auto it = properties.constBegin(); it != properties.constEnd(); ++it)
            m_model.rootItem()->appendChild(new PropertyItem(it.key(), toJSLiteral(it.value())));

        table.setQmlName("Properties");

        add.setQmlName("Add");
        add.setActionText(Tr::tr("&Add"));
        add.setAction([this] { m_model.rootItem()->appendChild(new PropertyItem({}, {})); });

        remove.setQmlName("Remove");
        remove.setActionText(Tr::tr("&Remove"));
        remove.setAction([this] { removeSelected(); });
        // Nothing picked, nothing to remove. The widget dialog read this off
        // the table's current item.
        remove.setEnabled(false);
        connect(&table, &PropertiesAspect::selectionChanged, this, [this] {
            remove.setEnabled(!table.selectedRows().isEmpty());
        });
    }

    // What the table holds, as qbs wants it. A row with no key is a row the
    // reader added and has not filled in, and is not a property.
    QVariantMap properties() const
    {
        QVariantMap properties;
        const Utils::TreeItem *const root = m_model.rootItem();
        for (int row = 0; row < root->childCount(); ++row) {
            const auto item = static_cast<const PropertyItem *>(root->childAt(row));
            if (item->name().isEmpty())
                continue;
            properties.insert(item->name(), fromJSLiteral(item->value()));
        }
        return properties;
    }

    void removeSelected()
    {
        // Back to front: removing a row renumbers the ones under it.
        QList<int> rows = table.selectedRows();
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        Utils::TreeItem *const root = m_model.rootItem();
        for (const int row : std::as_const(rows)) {
            if (row >= 0 && row < root->childCount())
                root->removeChildAt(row);
        }
        table.setSelectedRows({});
    }

    PropertiesAspect table;
    Utils::ActionAspect add{this};
    Utils::ActionAspect remove{this};

private:
    PropertyModel m_model;
};

CustomQbsPropertiesDialog::CustomQbsPropertiesDialog(const QVariantMap &properties, QWidget *parent)
    : QDialog(parent)
    , m_settings(new CustomPropertiesSettings(properties))
{
    setWindowTitle(Tr::tr("Custom Properties"));

    const auto buttonBox
        = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    const auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(buttonBox);
}

CustomQbsPropertiesDialog::~CustomQbsPropertiesDialog() = default;

QVariantMap CustomQbsPropertiesDialog::properties() const
{
    return m_settings->properties();
}

#ifdef WITH_TESTS

class CustomQbsPropertiesTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        CustomPropertiesSettings settings({});
        const Utils::Result<> rendered
            = Core::aspectFormRenders(&settings, "CustomQbsPropertiesDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhatTheTableGivesBackToQbs()
    {
        QVariantMap given;
        given.insert("project.foo", "bar");
        given.insert("modules.cpp.optimization", "fast");

        CustomPropertiesSettings settings(given);
        QCOMPARE(settings.properties(), given);

        // A row added and not filled in is not a property: it has no key, and
        // qbs would be handed an empty one.
        settings.add.triggerAction();
        QCOMPARE(settings.properties(), given);
    }

    void testRemovingWhatIsPicked()
    {
        QVariantMap given;
        given.insert("a", "1");
        given.insert("b", "2");
        given.insert("c", "3");

        CustomPropertiesSettings settings(given);
        QVERIFY2(!settings.remove.isEnabled(), "Remove is offered with nothing selected");

        settings.table.setSelectedRows({1});
        QVERIFY(settings.remove.isEnabled());
        settings.remove.triggerAction();

        QCOMPARE(settings.properties().size(), 2);
        QVERIFY(!settings.properties().contains("b"));
        QVERIFY2(!settings.remove.isEnabled(),
                 "Remove stayed offered after what was picked went away");

        // Several at once, which is where removing front to back takes the
        // wrong rows.
        CustomPropertiesSettings several(given);
        several.table.setSelectedRows({0, 2});
        several.remove.triggerAction();
        QCOMPARE(several.properties().size(), 1);
        QVERIFY2(several.properties().contains("b"), "removing two rows took the wrong ones");
    }

    void testPickingARowInTheDrawnTableReachesTheAspect()
    {
        // The tests above set the selection from C++; this one goes through
        // the drawn table, which is the only way the QML that reports it is
        // exercised at all.
        QVariantMap given;
        given.insert("a", "1");
        given.insert("b", "2");

        CustomPropertiesSettings settings(given);
        const std::unique_ptr<QWidget> form(Core::createAspectForm(&settings));
        QVERIFY(form);
        QObject * const root = Core::aspectFormRoot(form.get());
        QVERIFY2(root, "no front end said what the form was drawn from");

        QObject *table = nullptr;
        QTRY_VERIFY(table = root->findChild<QObject *>("propertiesTable"));

        QVERIFY(!settings.remove.isEnabled());
        QVERIFY(QMetaObject::invokeMethod(table, "selectRow", Q_ARG(int, 1)));
        QTRY_VERIFY2(settings.remove.isEnabled(),
                     "picking a row in the drawn table did not reach the aspect");
    }
};

QObject *createCustomQbsPropertiesTest()
{
    return new CustomQbsPropertiesTest;
}

#endif // WITH_TESTS

} // namespace QbsProjectManager::Internal

#include "customqbspropertiesdialog.moc"
