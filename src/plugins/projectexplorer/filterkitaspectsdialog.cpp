// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "filterkitaspectsdialog.h"

#include "kitaspect.h"
#include "kitmanager.h"
#include "projectexplorertr.h"

#include <utils/itemviews.h>
#include <utils/qtcassert.h>
#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspectpresentation.h>
#include <utils/aspects.h>
#include <utils/treemodel.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QString>
#include <QTextDocument>
#include <QVBoxLayout>

using namespace Utils;

namespace ProjectExplorer::Internal {

// A kit aspect's name as a reader sees it. Some are HTML - CMake's generator
// aspect is "CMake <a href=\"generator\">generator</a>" - and a table cell
// showing the markup is not a name.
//
// Kept out of the item because it is a question about a string, and there the
// only way to ask it was to build a model out of the registered factories.
QString plainKitAspectName(const QString &displayName)
{
    if (displayName.indexOf('<') < 0)
        return displayName;

    QTextDocument html;
    html.setHtml(displayName);
    return html.toPlainText();
}

class FilterTreeItem : public TreeItem
{
public:
    FilterTreeItem(const KitAspectFactory *factory, bool enabled)
        : m_factory(factory), m_enabled(enabled)
    {}

    QString displayName() const { return plainKitAspectName(m_factory->displayName()); }
    Utils::Id id() const { return m_factory->id(); }
    bool enabled() const { return m_enabled; }

private:
    QVariant data(int column, int role) const override
    {
        QTC_ASSERT(column < 2, return QVariant());
        // Which cells can be ticked is in flags(), which QML cannot reach.
        if (role == AspectTable::CheckableRole)
            return flags(column).testFlag(Qt::ItemIsUserCheckable);
        if (role == AspectTable::EditableRole)
            return AspectTable::isWritable(flags(column));
        if (column == 0 && role == Qt::DisplayRole)
            return displayName();
        if (column == 1 && role == Qt::CheckStateRole)
            return m_enabled ? Qt::Checked : Qt::Unchecked;
        return {};
    }

    bool setData(int column, const QVariant &data, int role) override
    {
        QTC_ASSERT(column == 1 && !m_factory->isEssential(), return false);
        if (role == Qt::CheckStateRole) {
            m_enabled = data.toInt() == Qt::Checked;
            return true;
        }
        return false;
    }

    Qt::ItemFlags flags(int column) const override
    {
        QTC_ASSERT(column < 2, return Qt::ItemFlags());
        Qt::ItemFlags flags = Qt::ItemIsSelectable;
        if (column == 0 || !m_factory->isEssential())
            flags |= Qt::ItemIsEnabled;
        // An essential aspect is always shown, so its box is not the reader's
        // to untick.
        if (column == 1 && !m_factory->isEssential())
            flags |= Qt::ItemIsUserCheckable;
        return flags;
    }

    const KitAspectFactory * const m_factory;
    bool m_enabled;
};

class FilterKitAspectsModel : public TreeModel<TreeItem, FilterTreeItem>
{
public:
    FilterKitAspectsModel(const Kit *kit, QObject *parent) : TreeModel(parent)
    {
        setHeader({Tr::tr("Setting"), Tr::tr("Visible")});
        for (const KitAspectFactory * const factory : KitManager::kitAspectFactories()) {
            // Which aspects a *kit* shows is the kit's business; with no kit
            // the question is which ones are hidden everywhere.
            const bool enabled = kit ? kit->isAspectRelevant(factory->id())
                                     : !KitManager::irrelevantAspects().contains(factory->id());
            rootItem()->appendChild(new FilterTreeItem(factory, enabled));
        }
        static const auto cmp = [](const TreeItem *item1, const TreeItem *item2) {
            return static_cast<const FilterTreeItem *>(item1)->displayName()
                   < static_cast<const FilterTreeItem *>(item2)->displayName();
        };
        rootItem()->sortChildren(cmp);
    }

    // A Quick table addresses roles by name, and a TreeModel names only the
    // ones it was registered for.
    QHash<int, QByteArray> roleNames() const override
    {
        return AspectTable::withRoleNames(TreeModel::roleNames());
    }

    QSet<Utils::Id> disabledItems() const
    {
        QSet<Utils::Id> ids;
        for (int i = 0; i < rootItem()->childCount(); ++i) {
            const auto item = static_cast<FilterTreeItem *>(rootItem()->childAt(i));
            if (!item->enabled())
                ids << item->id();
        }
        return ids;
    }
};

class FilterKitAspectsSettings final : public AspectContainer
{
public:
    explicit FilterKitAspectsSettings(const Kit *kit)
        : m_model(new FilterKitAspectsModel(kit, nullptr))
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/ProjectExplorer/FilterKitAspectsDialog.qml"));
        table.setQmlName("Aspects");
        table.setModel(m_model.get());
    }

    QSet<Utils::Id> disabledItems() const { return m_model->disabledItems(); }

private:
    const std::unique_ptr<FilterKitAspectsModel> m_model;

public:
    TableAspect table{this};
};

FilterKitAspectsDialog::FilterKitAspectsDialog(const Kit *kit, QWidget *parent)
    : QDialog(parent)
    , m_settings(new FilterKitAspectsSettings(kit))
{
    const auto buttonBox
        = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    const auto layout = new QVBoxLayout(this);
    layout->addWidget(Core::createAspectForm(m_settings.get()));
    layout->addWidget(buttonBox);
}

FilterKitAspectsDialog::~FilterKitAspectsDialog() = default;

QSet<Utils::Id> FilterKitAspectsDialog::irrelevantAspects() const
{
    return m_settings->disabledItems();
}

#ifdef WITH_TESTS

class FilterKitAspectsTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        FilterKitAspectsSettings settings(nullptr);
        const Utils::Result<> rendered
            = Core::aspectFormRenders(&settings, "FilterKitAspectsDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testAnAspectNameIsShownAsWords()
    {
        // Some kit aspects name themselves in HTML - CMake's generator aspect
        // is a link - and a cell showing the markup is not a name.
        QCOMPARE(plainKitAspectName("Compiler"), QString("Compiler"));
        QCOMPARE(plainKitAspectName("CMake <a href=\"generator\">generator</a>"),
                 QString("CMake generator"));
        QCOMPARE(plainKitAspectName({}), QString());
    }

    void testWhichBoxesCanBeTicked()
    {
        FilterKitAspectsSettings settings(nullptr);
        QAbstractItemModel * const model = settings.table.tableModel();
        QVERIFY(model);
        QCOMPARE(model->columnCount(), 2);
        QVERIFY2(model->rowCount() > 0, "fixture: no kit aspects are registered");

        // The name is read, never ticked; the visibility is the box.
        QVERIFY(!model->index(0, 0).data(Utils::AspectTable::CheckableRole).toBool());

        // A kit cannot do without an essential aspect, so its box is not the
        // reader's to untick - and at least one is essential, or this says
        // nothing.
        int checkable = 0;
        int fixed = 0;
        for (int row = 0; row < model->rowCount(); ++row) {
            if (model->index(row, 1).data(Utils::AspectTable::CheckableRole).toBool())
                ++checkable;
            else
                ++fixed;
        }
        QVERIFY2(checkable > 0, "no kit setting could be hidden at all");
        QVERIFY2(fixed > 0, "fixture: no essential aspect, so the rule is not exercised");
    }

    void testWhatTheDialogReportsAsHidden()
    {
        FilterKitAspectsSettings settings(nullptr);
        QAbstractItemModel * const model = settings.table.tableModel();
        const qsizetype before = settings.disabledItems().size();

        // Untick the first one that can be unticked.
        int row = 0;
        while (row < model->rowCount()
               && !model->index(row, 1).data(Utils::AspectTable::CheckableRole).toBool()) {
            ++row;
        }
        QVERIFY(row < model->rowCount());
        QVERIFY(model->setData(model->index(row, 1), Qt::Unchecked, Qt::CheckStateRole));

        QCOMPARE(settings.disabledItems().size(), before + 1);
    }
};

QObject *createFilterKitAspectsTest()
{
    return new FilterKitAspectsTest;
}

#endif // WITH_TESTS

} // ProjectExplorer::Internal

#ifdef WITH_TESTS
#include "filterkitaspectsdialog.moc"
#endif
