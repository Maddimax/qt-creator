// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "envvarseparatoraspect.h"

#include "coreplugintr.h"
#include "dialogs/ioptionspage.h"

#include <utils/aspectpresentation.h>
#include <utils/aspects.h>

#include <utils/environment.h>
#include <utils/guiutils.h>
#include <utils/namevaluedictionary.h>
#include <utils/treemodel.h>

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <QDialog>
#include <QDialogButtonBox>
#include <QVBoxLayout>

using namespace Utils;

namespace Core::Internal {

class EnvVarSeparatorItem : public TreeItem
{
public:
    EnvVarSeparatorItem(const QString &var, const QString &sep)
        : m_var(var)
        , m_sep(sep)
    {}

    QString var() const { return m_var; }
    QString sep() const { return m_sep; }

private:
    QVariant data(int column, int role) const override
    {
        // Which cells can be typed into is in flags(), which QML cannot reach.
        if (role == AspectTable::EditableRole)
            return AspectTable::isWritable(flags(column));
        if (role != Qt::DisplayRole && role != Qt::EditRole)
            return {};
        return column == 0 ? m_var : m_sep;
    }

    bool setData(int column, const QVariant &data, int) override
    {
        if (column == 0) {
            m_var = data.toString();
            return true;
        }
        if (column == 1) {
            m_sep = data.toString();
            return true;
        }
        return false;
    }

    Qt::ItemFlags flags(int column) const override
    {
        return TreeItem::flags(column) | Qt::ItemIsEditable;
    }

    QString m_var;
    QString m_sep;
};

class EnvVarSeparatorModel final : public TreeModel<TreeItem, EnvVarSeparatorItem>
{
public:
    EnvVarSeparatorModel() { setHeader({Tr::tr("Variable"), Tr::tr("Separator")}); }

    // A Quick table addresses roles by name, and a TreeModel names only the
    // ones it was registered for.
    QHash<int, QByteArray> roleNames() const override
    {
        return AspectTable::withRoleNames(TreeModel::roleNames());
    }
};

// The table itself, and which of its rows are picked. The widget dialog asked
// the view's selection model; there is no view to ask here, so the table says.
class SeparatorsAspect final : public BaseAspect
{
    Q_OBJECT

public:
    SeparatorsAspect(AspectContainer *container, EnvVarSeparatorModel *model)
        : BaseAspect(container)
        , m_model(model)
    {}

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Table;
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
    EnvVarSeparatorModel * const m_model;
    QList<int> m_selectedRows;
};

// What a variable with no separator of its own is called when one is added.
// Named here because the dialog's Add button is the only thing that knew it.
const char newVariableName[] = "CUSTOM_VAR";

class EnvVarSeparatorsSettings final : public AspectContainer
{
public:
    explicit EnvVarSeparatorsSettings(const NameValueDictionary &separators)
        : table(this, &m_model)
    {
        setAutoApply(true);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Core/EnvVarSeparatorsDialog.qml"));

        for (auto it = separators.begin(); it != separators.end(); ++it)
            m_model.rootItem()->appendChild(new EnvVarSeparatorItem(it.key(), it.value()));

        explanation.setQmlName("Explanation");
        explanation.setText(Tr::tr(
            "For environment variables with list semantics that do not use the standard path list "
            "separator, you need to configure the respective separators here if you plan to "
            "aggregate them from several places (for instance from the kit and from the project)."));
        explanation.setWordWrap(true);

        table.setQmlName("Separators");

        add.setQmlName("Add");
        add.setActionText(Tr::tr("&Add"));
        add.setAction([this] {
            m_model.rootItem()->appendChild(new EnvVarSeparatorItem(newVariableName, {}));
        });

        remove.setQmlName("Remove");
        remove.setActionText(Tr::tr("&Remove"));
        remove.setAction([this] { removeSelected(); });
        // Nothing picked, nothing to remove. The widget dialog worked this out
        // from the view's selection model, where only the view could see it.
        remove.setEnabled(false);
        connect(&table, &SeparatorsAspect::selectionChanged, this, [this] {
            remove.setEnabled(!table.selectedRows().isEmpty());
        });
    }

    // What the table holds, as the caller wants it back. A question about the
    // rows, which inside the dialog could only be asked by opening one.
    NameValueDictionary separators() const
    {
        NameValueDictionary seps;
        const TreeItem *const root = m_model.rootItem();
        for (int i = 0; i < root->childCount(); ++i) {
            const auto item = static_cast<const EnvVarSeparatorItem *>(root->childAt(i));
            seps.set(item->var(), item->sep());
        }
        return seps;
    }

    void removeSelected()
    {
        // Back to front: removing a row renumbers the ones under it.
        QList<int> rows = table.selectedRows();
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        TreeItem *const root = m_model.rootItem();
        for (const int row : std::as_const(rows)) {
            if (row >= 0 && row < root->childCount())
                root->removeChildAt(row);
        }
        table.setSelectedRows({});
    }

    Utils::TextDisplay explanation{this};
    SeparatorsAspect table;
    Utils::ActionAspect add{this};
    Utils::ActionAspect remove{this};

private:
    EnvVarSeparatorModel m_model;
};

class EnvVarSeparatorsDialog : public QDialog
{
public:
    EnvVarSeparatorsDialog(const NameValueDictionary &separators, QWidget *parent)
        : QDialog(parent)
        , m_settings(separators)
    {
        setWindowTitle(Tr::tr("Environment Variable Separators"));

        const auto buttonBox
            = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

        const auto layout = new QVBoxLayout(this);
        layout->addWidget(createAspectForm(&m_settings));
        layout->addWidget(buttonBox);

        resize(500, 300);
    }

    NameValueDictionary separators() const { return m_settings.separators(); }

private:
    EnvVarSeparatorsSettings m_settings;
};

EnvVarSeparatorAspect::EnvVarSeparatorAspect(Utils::AspectContainer *container)
    : StringListAspect(container)
{
    // The summary is derived from the value, so it changes with it.
    connect(this, &Utils::BaseAspect::volatileValueChanged,
            this, &Utils::BaseAspect::displayTextChanged);

    Environment::setListSeparatorProvider([this](const QString &varName) -> std::optional<QString> {
        const NameValueDictionary seps = NameValueDictionary(value());
        if (const auto it = seps.find(varName); it != seps.end())
            return it.value();
        return {};
    });
}

Utils::AspectPresentation EnvVarSeparatorAspect::presentation() const
{
    Utils::AspectPresentation p = Utils::StringListAspect::presentation();
    p.control = Utils::AspectControls::TextWithAction;
    p.actionText = Tr::tr("Change...");
    return p;
}

QString EnvVarSeparatorAspect::displayText() const
{
    const NameValueDictionary seps = NameValueDictionary(volatileValue());
    QStringList parts;
    for (auto it = seps.begin(); it != seps.end(); ++it)
        parts.append(QString("%1: \"%2\"").arg(it.key(), it.value()));
    return parts.join(", ");
}

void EnvVarSeparatorAspect::triggerAction()
{
    EnvVarSeparatorsDialog dlg(NameValueDictionary(volatileValue()), Utils::dialogParent());
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QStringList newValues = dlg.separators().toStringList();
    if (volatileValue() != newValues)
        setVolatileValue(newValues);
}

#ifdef WITH_TESTS

class EnvVarSeparatorsTest final : public QObject
{
    Q_OBJECT

private slots:
    void testTheDialogDrawsWithTheQmlItNames()
    {
        EnvVarSeparatorsSettings settings{NameValueDictionary()};
        const Utils::Result<> rendered
            = aspectFormRenders(&settings, "EnvVarSeparatorsDialog.qml");
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error()));
    }

    void testWhatTheTableGivesBack()
    {
        NameValueDictionary given;
        given.set("FOO", ";");
        given.set("BAR", ",");

        EnvVarSeparatorsSettings settings(given);
        QCOMPARE(settings.separators(), given);

        // Adding a row adds a variable under a name the reader is meant to
        // replace - not an empty one, which would read back as no variable at
        // all.
        settings.add.triggerAction();
        const NameValueDictionary afterAdd = settings.separators();
        QCOMPARE(afterAdd.size(), given.size() + 1);
        QVERIFY(afterAdd.hasKey(newVariableName));
    }

    void testPickingARowInTheTableReachesTheAspect()
    {
        // The tests below set the selection from C++, which says nothing about
        // whether the *drawn* table ever reports one - and if it stopped,
        // Remove would simply never light up and nothing would fail.
        NameValueDictionary given;
        given.set("FOO", ";");
        given.set("BAR", ",");

        EnvVarSeparatorsSettings settings(given);
        const std::unique_ptr<QWidget> form(createAspectForm(&settings));
        QVERIFY(form);

        QObject * const root = aspectFormRoot(form.get());
        QVERIFY2(root, "no front end said what the form was drawn from");

        QObject *table = nullptr;
        QTRY_VERIFY(table = root->findChild<QObject *>("separatorsTable"));

        QVERIFY(!settings.remove.isEnabled());
        QVERIFY(QMetaObject::invokeMethod(table, "selectRow", Q_ARG(int, 1)));

        QTRY_COMPARE(settings.table.selectedRows(), QList<int>{1});
        QVERIFY2(settings.remove.isEnabled(),
                 "picking a row in the drawn table did not reach the aspect behind it");
    }

    void testRemovingWhatIsPicked()
    {
        NameValueDictionary given;
        given.set("FIRST", "1");
        given.set("SECOND", "2");
        given.set("THIRD", "3");

        EnvVarSeparatorsSettings settings(given);

        // Nothing picked, nothing to remove. In the widget dialog this came
        // off the view's selection model, where only the view could see it.
        QVERIFY2(!settings.remove.isEnabled(), "Remove is offered with nothing selected");

        settings.table.setSelectedRows({1});
        QVERIFY(settings.remove.isEnabled());

        settings.remove.triggerAction();
        NameValueDictionary left = settings.separators();
        QCOMPARE(left.size(), 2);
        QVERIFY(!left.hasKey("SECOND"));
        QVERIFY(left.hasKey("FIRST"));
        QVERIFY(left.hasKey("THIRD"));

        // And it is offered again only once something is picked again.
        QVERIFY2(!settings.remove.isEnabled(),
                 "Remove stayed offered after what was picked went away");

        // Several at once, which is where removing front to back would take
        // the wrong rows: each removal renumbers the ones under it.
        EnvVarSeparatorsSettings several(given);
        several.table.setSelectedRows({0, 2});
        several.remove.triggerAction();
        left = several.separators();
        QCOMPARE(left.size(), 1);
        QVERIFY2(left.hasKey("SECOND"), "removing two rows took the wrong ones");
    }
};

QObject *createEnvVarSeparatorsTest()
{
    return new EnvVarSeparatorsTest;
}

#endif // WITH_TESTS

} // namespace Core



#include "envvarseparatoraspect.moc"
