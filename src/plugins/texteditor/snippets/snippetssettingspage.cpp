// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "snippetssettingspage.h"

#include "snippeteditor.h"
#include "snippetprovider.h"
#include "snippet.h"
#include "snippetscollection.h"
#include "../fontsettings.h"
#include "../textdocument.h"
#include "../texteditorconstants.h"
#include "../texteditortr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <utils/aspectpresentation.h>
#include <utils/aspectwidgets.h>
#include <utils/aspects.h>
#include <utils/guiutils.h>
#include <utils/itemviews.h>
#include <utils/layoutbuilder.h>

#include <QAbstractButton>
#ifdef WITH_TESTS
#include <QTest>
#endif
#include <QAbstractTableModel>
#include <QComboBox>
#include <QItemSelectionModel>
#include <QList>
#include <QMessageBox>
#include <QModelIndex>
#include <QPointer>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextStream>

using namespace Utils;

namespace TextEditor::Internal {

const char kLastUsedSnippetGroup[] = "TextSnippetsSettings/LastUsedSnippetGroup";

// SnippetsTableModel

class SnippetsTableModel final : public QAbstractTableModel
{
public:
    SnippetsTableModel() = default;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    Qt::ItemFlags flags(const QModelIndex &modelIndex) const override;
    QVariant data(const QModelIndex &modelIndex, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &modelIndex, const QVariant &value,
                 int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QStringList groupIds() const;
    void load(const QString &groupId);

    QModelIndex createSnippet();
    QModelIndex insertSnippet(const Snippet &snippet);
    void removeSnippet(const QModelIndex &modelIndex);
    const Snippet &snippetAt(const QModelIndex &modelIndex) const;
    void setSnippetContent(const QModelIndex &modelIndex, const QString &content);
    void revertBuitInSnippet(const QModelIndex &modelIndex);
    void restoreRemovedBuiltInSnippets();
    void resetSnippets();
    void reloadSnippets();

private:
    void replaceSnippet(const Snippet &snippet, const QModelIndex &modelIndex);

    QString m_activeGroupId;
};

int SnippetsTableModel::rowCount(const QModelIndex &) const
{
    return SnippetsCollection::instance()->totalActiveSnippets(m_activeGroupId);
}

int SnippetsTableModel::columnCount(const QModelIndex &) const
{
    return 2;
}

Qt::ItemFlags SnippetsTableModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags itemFlags = QAbstractTableModel::flags(index);
    if (index.isValid())
        itemFlags |= Qt::ItemIsEditable;
    return itemFlags;
}

QVariant SnippetsTableModel::data(const QModelIndex &modelIndex, int role) const
{
    if (!modelIndex.isValid())
        return QVariant();

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        const Snippet &snippet = SnippetsCollection::instance()->snippet(modelIndex.row(), m_activeGroupId);
        if (modelIndex.column() == 0)
            return snippet.trigger();
        else
            return snippet.complement();
    } else if (role == Utils::AspectTable::EditableRole) {
        return Utils::AspectTable::isWritable(flags(modelIndex));
    } else {
        return QVariant();
    }
}

bool SnippetsTableModel::setData(const QModelIndex &modelIndex, const QVariant &value, int role)
{
    if (modelIndex.isValid() && role == Qt::EditRole) {
        Snippet snippet(SnippetsCollection::instance()->snippet(modelIndex.row(), m_activeGroupId));
        if (modelIndex.column() == 0) {
            const QString &s = value.toString();
            if (!Snippet::isValidTrigger(s)) {
                QMessageBox::critical(
                    Core::ICore::dialogParent(),
                    Tr::tr("Error"),
                    Tr::tr("Not a valid trigger. A valid trigger can only contain letters, "
                       "numbers, or underscores, where the first character is "
                       "limited to letter or underscore."));
                if (snippet.trigger().isEmpty())
                    removeSnippet(modelIndex);
                return false;
            }
            snippet.setTrigger(s);
        } else {
            snippet.setComplement(value.toString());
        }

        replaceSnippet(snippet, modelIndex);
        return true;
    }
    return false;
}

QVariant SnippetsTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return QVariant();

    if (section == 0)
        return Tr::tr("Trigger");
    else
        return Tr::tr("Trigger Variant");
}

QHash<int, QByteArray> SnippetsTableModel::roleNames() const
{
    return Utils::AspectTable::withRoleNames(QAbstractTableModel::roleNames());
}

void SnippetsTableModel::load(const QString &groupId)
{
    beginResetModel();
    m_activeGroupId = groupId;
    endResetModel();
}

QStringList SnippetsTableModel::groupIds() const
{
    return SnippetsCollection::instance()->groupIds();
}

QModelIndex SnippetsTableModel::createSnippet()
{
    Snippet snippet(m_activeGroupId);
    return insertSnippet(snippet);
}

QModelIndex SnippetsTableModel::insertSnippet(const Snippet &snippet)
{
    const SnippetsCollection::Hint &hint = SnippetsCollection::instance()->computeInsertionHint(snippet);
    beginInsertRows(QModelIndex(), hint.index(), hint.index());
    SnippetsCollection::instance()->insertSnippet(snippet, hint);
    endInsertRows();

    return index(hint.index(), 0);
}

void SnippetsTableModel::removeSnippet(const QModelIndex &modelIndex)
{
    beginRemoveRows(QModelIndex(), modelIndex.row(), modelIndex.row());
    SnippetsCollection::instance()->removeSnippet(modelIndex.row(), m_activeGroupId);
    endRemoveRows();
}

const Snippet &SnippetsTableModel::snippetAt(const QModelIndex &modelIndex) const
{
    return SnippetsCollection::instance()->snippet(modelIndex.row(), m_activeGroupId);
}

void SnippetsTableModel::setSnippetContent(const QModelIndex &modelIndex, const QString &content)
{
    SnippetsCollection::instance()->setSnippetContent(modelIndex.row(), m_activeGroupId, content);
}

void SnippetsTableModel::revertBuitInSnippet(const QModelIndex &modelIndex)
{
    const Snippet &snippet = SnippetsCollection::instance()->revertedSnippet(modelIndex.row(), m_activeGroupId);
    if (snippet.id().isEmpty()) {
        QMessageBox::critical(Core::ICore::dialogParent(), Tr::tr("Error"),
                              Tr::tr("Error reverting snippet."));
        return;
    }
    replaceSnippet(snippet, modelIndex);
}

void SnippetsTableModel::restoreRemovedBuiltInSnippets()
{
    beginResetModel();
    SnippetsCollection::instance()->restoreRemovedSnippets(m_activeGroupId);
    endResetModel();
}

void SnippetsTableModel::resetSnippets()
{
    beginResetModel();
    SnippetsCollection::instance()->reset(m_activeGroupId);
    endResetModel();
}

void SnippetsTableModel::reloadSnippets()
{
    beginResetModel();
    SnippetsCollection::instance()->reload();
    endResetModel();
}

void SnippetsTableModel::replaceSnippet(const Snippet &snippet, const QModelIndex &modelIndex)
{
    const int row = modelIndex.row();
    const SnippetsCollection::Hint &hint =
        SnippetsCollection::instance()->computeReplacementHint(row, snippet);
    if (modelIndex.row() == hint.index()) {
        SnippetsCollection::instance()->replaceSnippet(row, snippet, hint);
        if (modelIndex.column() == 0)
            emit dataChanged(modelIndex, modelIndex.sibling(row, 1));
        else
            emit dataChanged(modelIndex.sibling(row, 0), modelIndex);
    } else {
        if (row < hint.index())
            // Rows will be moved down.
            beginMoveRows(QModelIndex(), row, row, QModelIndex(), hint.index() + 1);
        else
            beginMoveRows(QModelIndex(), row, row, QModelIndex(), hint.index());
        SnippetsCollection::instance()->replaceSnippet(row, snippet, hint);
        endMoveRows();
    }
}

// The snippets of the selected group, as rows. The collection is the value;
// which row is current and what its content is are the page's business, so they
// are siblings of this aspect rather than widgets it builds.
class SnippetsAspect final : public BaseAspect
{
    Q_OBJECT

    // What a snippet in the selected group is written in, so that the page can
    // colour it.
    Q_PROPERTY(QString mimeType READ mimeType NOTIFY currentSnippetChanged)
    // And which group it is, which is what says how it indents and completes.
    Q_PROPERTY(QString groupId READ groupId NOTIFY currentSnippetChanged)

public:
    explicit SnippetsAspect(Utils::AspectContainer *container)
        : BaseAspect(container)
    {
        // Handed to QML, which frees an unparented QObject it is given.
        m_model.setParent(this);
    }

    QString mimeType() const { return SnippetProvider::mimeTypeForGroup(m_groupId); }

    QString groupId() const { return m_groupId; }

    void apply() override;
    void cancel() override;
    bool isDirty() const override { return m_snippetsCollectionChanged; }

    Utils::AspectPresentation presentation() const override
    {
        Utils::AspectPresentation p = BaseAspect::presentation();
        p.control = Utils::AspectControls::Table;
        // Add and Remove are the page's own buttons: adding starts an edit on
        // the new row and removing complains when there is nothing selected.
        p.allowAdding = false;
        p.allowRemoving = false;
        return p;
    }

    QAbstractItemModel *tableModel() override { return &m_model; }

    void load(const QString &groupId)
    {
        m_groupId = groupId;
        m_model.load(groupId);
        emit currentSnippetChanged();
    }

    // The row a page is showing the content of, in the model's own numbering.
    int currentRow() const { return m_currentRow; }
    Q_INVOKABLE void setCurrentRow(int row);

    const Snippet *currentSnippet() const;

    void createSnippet();
    void removeCurrentSnippet();
    void revertCurrentBuiltIn();
    void restoreRemovedBuiltIns() { m_model.restoreRemovedBuiltInSnippets(); }
    void resetAll() { m_model.resetSnippets(); }

    // What the current snippet holds, and putting an edit of it back.
    QString currentContent() const;
    void setCurrentContent(const QString &content);

signals:
    void currentSnippetChanged();

private:
    void markSnippetsCollection();

    SnippetsTableModel m_model;
    QString m_groupId;
    bool m_snippetsCollectionChanged = false;
    int m_currentRow = -1;
};

void SnippetsAspect::apply()
{
    if (!m_snippetsCollectionChanged)
        return;

    if (const Result<> res = SnippetsCollection::instance()->synchronize()) {
        m_snippetsCollectionChanged = false;
    } else {
        QMessageBox::critical(Core::ICore::dialogParent(),
                              Tr::tr("Error While Saving Snippet Collection"), res.error());
    }
}

void SnippetsAspect::cancel()
{
    if (m_snippetsCollectionChanged) {
        m_model.reloadSnippets();
        m_snippetsCollectionChanged = false;
    }
}

void SnippetsAspect::markSnippetsCollection()
{
    m_snippetsCollectionChanged = true;
    markSettingsDirty();
}

void SnippetsAspect::setCurrentRow(int row)
{
    const int clamped = row >= 0 && row < m_model.rowCount() ? row : -1;
    if (m_currentRow == clamped)
        return;
    m_currentRow = clamped;
    emit currentSnippetChanged();
}

const Snippet *SnippetsAspect::currentSnippet() const
{
    if (m_currentRow < 0 || m_currentRow >= m_model.rowCount())
        return nullptr;
    return &m_model.snippetAt(m_model.index(m_currentRow, 0));
}

QString SnippetsAspect::currentContent() const
{
    const Snippet *snippet = currentSnippet();
    return snippet ? snippet->content() : QString();
}

void SnippetsAspect::setCurrentContent(const QString &content)
{
    const Snippet *snippet = currentSnippet();
    if (!snippet || snippet->content() == content)
        return;
    m_model.setSnippetContent(m_model.index(m_currentRow, 0), content);
    markSnippetsCollection();
}

void SnippetsAspect::createSnippet()
{
    const QModelIndex index = m_model.createSnippet();
    setCurrentRow(index.row());
    emit currentSnippetChanged();
}

void SnippetsAspect::removeCurrentSnippet()
{
    if (!currentSnippet()) {
        QMessageBox::critical(Core::ICore::dialogParent(), Tr::tr("Error"),
                              Tr::tr("No snippet selected."));
        return;
    }
    const int row = m_currentRow;
    m_model.removeSnippet(m_model.index(row, 0));
    setCurrentRow(row < m_model.rowCount() ? row : m_model.rowCount() - 1);
    emit currentSnippetChanged();
}

void SnippetsAspect::revertCurrentBuiltIn()
{
    if (const Snippet *snippet = currentSnippet(); snippet && snippet->isBuiltIn()) {
        m_model.revertBuitInSnippet(m_model.index(m_currentRow, 0));
        emit currentSnippetChanged();
    }
}

// SnippetsSettingsPage

class SnippetsSettings final : public AspectContainer
{
public:
    SnippetsSettings()
    {
        setAutoApply(false);

        group.setQmlName("Group");
        group.setLabelText(Tr::tr("Group:"));
        group.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
        group.setDefaultValue(0);

        snippets.setQmlName("Snippets");
        snippets.setLabelText(Tr::tr("Snippets:"));

        content.setQmlName("Content");
        content.setDisplayStyle(StringAspect::DisplayStyle::TextEditDisplay);
        content.setLabelText(Tr::tr("Snippet:"));

        addSnippet.setQmlName("AddSnippet");
        addSnippet.setActionText(Tr::tr("Add"));
        addSnippet.setAction([this] { snippets.createSnippet(); });

        removeSnippet.setQmlName("RemoveSnippet");
        removeSnippet.setActionText(Tr::tr("Remove"));
        removeSnippet.setAction([this] { snippets.removeCurrentSnippet(); });

        revertBuiltIn.setQmlName("RevertBuiltIn");
        revertBuiltIn.setActionText(Tr::tr("Revert Built-in"));
        revertBuiltIn.setEnabled(false);
        revertBuiltIn.setAction([this] { snippets.revertCurrentBuiltIn(); });

        restoreRemovedBuiltIns.setQmlName("RestoreRemovedBuiltIns");
        restoreRemovedBuiltIns.setActionText(Tr::tr("Restore Removed Built-ins"));
        restoreRemovedBuiltIns.setAction([this] { snippets.restoreRemovedBuiltIns(); });

        resetAll.setQmlName("ResetAll");
        resetAll.setActionText(Tr::tr("Reset All"));
        resetAll.setAction([this] { snippets.resetAll(); });

        // Selecting a group is what fills the table, and the last one used is
        // remembered across sessions.
        const auto loadGroup = [this] {
            const QString groupId = group.itemValue().toString();
            snippets.load(groupId);
            snippets.setCurrentRow(-1);
            Core::ICore::settings()->setValue(kLastUsedSnippetGroup, group.stringValue());
        };
        connect(&group, &SelectionAspect::volatileValueChanged, this, loadGroup);

        // The content is a view of the current snippet: selecting one shows it,
        // and editing it writes back. Only ever one direction at a time.
        const auto showCurrent = [this] {
            const Snippet *snippet = snippets.currentSnippet();
            content.setValue(snippet ? snippet->content() : QString());
            content.setEnabled(snippet != nullptr);
            revertBuiltIn.setEnabled(snippet && snippet->isBuiltIn());
        };
        connect(&snippets, &SnippetsAspect::currentSnippetChanged, this, showCurrent);
        connect(&content, &StringAspect::volatileValueChanged, this, [this] {
            snippets.setCurrentContent(content.volatileValue());
        });

        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/TextEditor/SnippetsSettingsPage.qml"));

        m_loadGroup = loadGroup;
        m_showCurrent = showCurrent;
    }

    // The groups register themselves while the plugins start up, which is after
    // this is built - so the list is taken when the page is opened rather than
    // in the constructor.
    void refreshGroups()
    {
        const QList<SnippetProvider> providers = SnippetProvider::snippetProviders();
        if (providers.size() == group.optionCount())
            return;

        // Groups are only ever appended, so the ones already listed stay put
        // and their indices keep meaning what they meant.
        for (int i = group.optionCount(); i < providers.size(); ++i)
            group.addOption({providers.at(i).displayName(), {}, providers.at(i).groupId()});

        const QString lastUsed
            = Core::ICore::settings()->value(kLastUsedSnippetGroup, QString()).toString();
        const int index = group.indexForDisplay(lastUsed);
        group.setValue(index == -1 ? 0 : index);
        m_loadGroup();
        m_showCurrent();
    }

    SelectionAspect group{this};
    SnippetsAspect snippets{this};
    StringAspect content{this};
    ActionAspect addSnippet{this};
    ActionAspect removeSnippet{this};
    ActionAspect revertBuiltIn{this};
    ActionAspect restoreRemovedBuiltIns{this};
    ActionAspect resetAll{this};

private:
    std::function<void()> m_loadGroup;
    std::function<void()> m_showCurrent;
};

class SnippetsSettingsPage final : public Core::IOptionsPage
{
public:
    SnippetsSettingsPage()
    {
        setId(Constants::TEXT_EDITOR_SNIPPETS_SETTINGS);
        setDisplayName(Tr::tr("Snippets"));
        setCategory(Constants::TEXT_EDITOR_SETTINGS_CATEGORY);
        setSettingsProvider([this] {
            m_snippets.refreshGroups();
            return &m_snippets;
        });
    }

private:
    SnippetsSettings m_snippets;
};

void setupSnippetsSettings()
{
    static SnippetsSettingsPage theSnippetsSettingsPage;
}

#ifdef WITH_TESTS

// The page shows the snippets of a group and the content of the selected one.
// Both used to live in the widgets it built, so neither could be checked
// without opening it.
class SnippetsSettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void testGroupsAreListedWhenThePageIsOpened();
    void testSelectingASnippetShowsIt();
    void testEditingTheContentWritesItBack();
};

void SnippetsSettingsTest::testGroupsAreListedWhenThePageIsOpened()
{
    SnippetsSettings settings;

    // The groups register while the plugins start up, so a page built before
    // that has none - which is what left the picker empty.
    QVERIFY(SnippetProvider::snippetProviders().size() > 1);
    settings.refreshGroups();
    QCOMPARE(settings.group.optionCount(), SnippetProvider::snippetProviders().size());
    QVERIFY(!settings.group.stringValue().isEmpty());

    // Asking twice does not double the list.
    settings.refreshGroups();
    QCOMPARE(settings.group.optionCount(), SnippetProvider::snippetProviders().size());

    // And the group says what its snippets are written in, which is what the
    // editor highlights by.
    QVERIFY(!settings.snippets.mimeType().isEmpty());
}

void SnippetsSettingsTest::testSelectingASnippetShowsIt()
{
    SnippetsSettings settings;
    settings.refreshGroups();

    QAbstractItemModel *model = settings.snippets.tableModel();
    QVERIFY(model->rowCount({}) > 0);

    // Nothing selected: nothing to show, and nothing to edit.
    settings.snippets.setCurrentRow(-1);
    QVERIFY(settings.content.value().isEmpty());
    QVERIFY(!settings.content.isEnabled());

    settings.snippets.setCurrentRow(0);
    const Snippet *snippet = settings.snippets.currentSnippet();
    QVERIFY(snippet);
    QCOMPARE(settings.content.value(), snippet->content());
    QVERIFY(settings.content.isEnabled());

    // Showing a snippet is not editing it. The content and the model each
    // change when the other does, so the only thing keeping them from taking
    // turns is that neither writes a value the other already has.
    QVERIFY(!static_cast<BaseAspect &>(settings.snippets).isDirty());
    settings.snippets.setCurrentRow(-1);
    settings.snippets.setCurrentRow(0);
    QVERIFY(!static_cast<BaseAspect &>(settings.snippets).isDirty());

    // A row past the end is no row, not a crash.
    settings.snippets.setCurrentRow(model->rowCount({}) + 5);
    QCOMPARE(settings.snippets.currentRow(), -1);
    QVERIFY(!settings.content.isEnabled());
}

void SnippetsSettingsTest::testEditingTheContentWritesItBack()
{
    SnippetsSettings settings;
    settings.refreshGroups();
    settings.snippets.setCurrentRow(0);

    const QString original = settings.content.value();
    QVERIFY(!original.isEmpty());

    settings.content.setValue(original + "\n// edited");
    QCOMPARE(settings.snippets.currentSnippet()->content(), original + "\n// edited");
    QVERIFY(static_cast<BaseAspect &>(settings.snippets).isDirty());

    // Showing the same snippet again must not write it back: the content and
    // the model would take turns telling each other what they already knew.
    settings.snippets.setCurrentRow(-1);
    settings.snippets.setCurrentRow(0);
    QCOMPARE(settings.snippets.currentSnippet()->content(), original + "\n// edited");

    static_cast<BaseAspect &>(settings.snippets).cancel();
    QVERIFY(!static_cast<BaseAspect &>(settings.snippets).isDirty());
}

QObject *createSnippetsSettingsTest()
{
    return new SnippetsSettingsTest;
}

#endif // WITH_TESTS

} // TextEditor::Internal

#include "snippetssettingspage.moc"
