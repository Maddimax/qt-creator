// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "customparserssettingspage.h"

#include "customparser.h"
#include "customparserconfigdialog.h"
#include "projectexplorerconstants.h"
#include "projectexplorertr.h"

#include <utils/filedialogs.h>
#include <utils/fileutils.h>
#include <utils/guiutils.h>
#include <utils/infolabel.h>
#include <utils/itemviews.h>

#include <QAbstractTableModel>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QList>
#include <QMessageBox>
#include <QPushButton>

#include <coreplugin/icore.h>

#include <utils/aspectpresentation.h>
#include <utils/aspects.h>
#include <QVBoxLayout>

using namespace Utils;

namespace ProjectExplorer::Internal {

class CustomParsersModel : public QAbstractTableModel
{
public:
    explicit CustomParsersModel(QObject *parent = nullptr);

    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QHash<int, QByteArray> roleNames() const override;
    bool removeRows(int row, int count, const QModelIndex &parent = {}) override;

    bool add(const ProjectExplorer::CustomParserSettings& s);
    bool remove(const QModelIndexList &indexes);

    const QList<CustomParserSettings> &parsers() const { return m_customParsers; }
    void reload();

    void apply();

protected:
    QList<CustomParserSettings> m_customParsers;
};

CustomParsersModel::CustomParsersModel(QObject *parent)
    : QAbstractTableModel(parent)
    , m_customParsers(CustomParsers::parsersAvailableInProject(nullptr))
{
    connect(
        &CustomParsers::instance(),
        &CustomParsers::changed,
        this,
        [this] {
            beginResetModel();
            m_customParsers = CustomParsers::parsersAvailableInProject(nullptr);
            endResetModel();
        });
}

QVariant CustomParsersModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    QVariant result;
    if (orientation == Qt::Vertical)
        return QVariant();

    switch (role) {
    case Qt::DisplayRole:
        switch (section) {
        case 0:
            result = Tr::tr("Name");
            break;
        case 1:
            result = Tr::tr("Build default");
            break;
        case 2:
            result = Tr::tr("Run default");
            break;
        }
        break;
    case Qt::ToolTipRole:
        switch (section) {
        case 0:
            result = Tr::tr("The name of the custom parser.");
            break;
        case 1:
            result = Tr::tr("This custom parser is used by default for all build configurations of "
                            "the project.");
            break;
        case 2:
            result = Tr::tr(
                "This custom parser is used by default for all run configurations of the project.");
            break;
        }
    }
    return result;
}

int CustomParsersModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return 3;
}

int CustomParsersModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return m_customParsers.size();
}

QVariant CustomParsersModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    const CustomParserSettings &s = m_customParsers[index.row()];

    switch (role) {
    case Qt::CheckStateRole:
        switch (index.column()) {
        case 1:
            return s.buildDefault ? Qt::Checked : Qt::Unchecked;
        case 2:
            return s.runDefault ? Qt::Checked : Qt::Unchecked;
        }
        break;

    case Qt::DisplayRole:
        switch (index.column()) {
        case 0:
            return s.displayName;
        }
        break;

    case Qt::EditRole:
        switch (index.column()) {
        case 1:
            return s.buildDefault;
        case 2:
            return s.runDefault;
        }
        break;

    case Qt::TextAlignmentRole:
        switch (index.column()) {
        case 1:
        case 2:
            return Qt::AlignCenter;
        }
        break;

    case Qt::FontRole: {
        QFont f;
        if (index.column() == 0 && s.readOnly)
            f.setItalic(true);
        return f;
    }

    case Qt::ToolTipRole:
        if (s.readOnly)
            return Tr::tr("Cannot modify parser because it was auto-imported.");
        return {};

    case Qt::UserRole:
        return QVariant::fromValue<CustomParserSettings>(s);

    case AspectTable::EditableRole:
        return AspectTable::isWritable(flags(index));

    case AspectTable::CheckableRole:
        return index.column() > 0 && !s.readOnly;
    }

    return QVariant();
}

QHash<int, QByteArray> CustomParsersModel::roleNames() const
{
    return AspectTable::withRoleNames(QAbstractTableModel::roleNames());
}

bool CustomParsersModel::removeRows(int row, int count, const QModelIndex &parent)
{
    if (parent.isValid() || row < 0 || row + count > m_customParsers.size())
        return false;
    // An auto-imported parser is not the user's to take away, and the table
    // offers one Remove for whatever is selected.
    for (int i = row; i < row + count; ++i) {
        if (m_customParsers.at(i).readOnly)
            return false;
    }
    beginRemoveRows({}, row, row + count - 1);
    m_customParsers.remove(row, count);
    endRemoveRows();
    return true;
}

bool CustomParsersModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid())
        return false;

    if (index.row() >= m_customParsers.size())
        return false;

    CustomParserSettings &s = m_customParsers[index.row()];

    bool result = true;
    switch (role) {
    case Qt::EditRole:
        switch (index.column()) {
        case 0:
            s.displayName = value.toString();
            emit dataChanged(index, index);
            break;
        }
        break;

    case Qt::CheckStateRole:
        switch (index.column()) {
        case 1:
            s.buildDefault = value == Qt::Checked;
            emit dataChanged(index, index);
            break;
        case 2:
            s.runDefault = value == Qt::Checked;
            emit dataChanged(index, index);
            break;
        }
        break;

    case Qt::UserRole:
        if (value.canConvert<CustomParserSettings>()) {
            s = value.value<CustomParserSettings>();
            emit dataChanged(index, index);
        } else
            result = false;
    }
    return result;
}

Qt::ItemFlags CustomParsersModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;

    if (!m_customParsers.at(index.row()).readOnly) {
        if (index.column() > 0)
            flags |= Qt::ItemIsUserCheckable;
        else
            flags |= Qt::ItemIsEditable;
    }

    return flags;
}

bool CustomParsersModel::add(const CustomParserSettings &s)
{
    if (!CustomParsers::canAdd(s, m_customParsers))
        return false;

    beginInsertRows(index(-1, -1), m_customParsers.size(), m_customParsers.size());
    m_customParsers.append(s);
    endInsertRows();
    return true;
}

bool CustomParsersModel::remove(const QModelIndexList &indexes)
{
    beginResetModel();
    for (auto it = std::rbegin(indexes); it != std::rend(indexes); ++it)
        m_customParsers.removeAt(it->row());
    endResetModel();
    return true;
}

void CustomParsersModel::reload()
{
    beginResetModel();
    m_customParsers = CustomParsers::parsersAvailableInProject(nullptr);
    endResetModel();
}

void CustomParsersModel::apply()
{
    CustomParsers::set(m_customParsers);
}

// What the Custom Output Parsers page edits. The parsers live in
// CustomParsers, which the rest of the plugin reads, so the model holds a
// working copy and apply() hands it over.
class CustomParsersAspect final : public BaseAspect
{
    Q_OBJECT

public:
    explicit CustomParsersAspect(AspectContainer *container)
        : BaseAspect(container)
        // Parented: a model handed to QML from a Q_INVOKABLE with no parent is
        // one QML takes ownership of and deletes.
        , m_model(new CustomParsersModel(this))
    {
        setQmlName("Parsers");
        setLabelText(Tr::tr("Parsers:"));
        connect(m_model, &QAbstractItemModel::dataChanged, this, &BaseAspect::volatileValueChanged);
        connect(m_model, &QAbstractItemModel::rowsInserted, this, &BaseAspect::volatileValueChanged);
        connect(m_model, &QAbstractItemModel::rowsRemoved, this, &BaseAspect::volatileValueChanged);
        connect(m_model, &QAbstractItemModel::modelReset, this, &BaseAspect::volatileValueChanged);
    }

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Table;
        // A parser is added through the dialog that defines it, not by typing
        // a blank row; removing one is the table's own.
        p.allowAdding = false;
        p.allowRemoving = true;
        return p;
    }

    QAbstractItemModel *tableModel() override { return m_model; }

    CustomParsersModel *model() const { return m_model; }

    // Which rows the table has selected, in this model's own order. The page
    // acts on them - export these, edit that one - and only the table knows
    // which they are.
    Q_INVOKABLE void setSelectedRows(const QVariantList &rows)
    {
        m_selectedRows.clear();
        for (const QVariant &row : rows)
            m_selectedRows.append(row.toInt());
        emit selectionChanged();
    }

    QList<CustomParserSettings> selectedParsers() const
    {
        QList<CustomParserSettings> parsers;
        for (int row : m_selectedRows) {
            if (row >= 0 && row < m_model->parsers().size())
                parsers.append(m_model->parsers().at(row));
        }
        return parsers;
    }

    QList<int> selectedRows() const { return m_selectedRows; }

signals:
    void selectionChanged();

public:

    bool isDirty() const override { return m_model->parsers() != CustomParsers::parsersAvailableInProject(nullptr); }

    void apply() override { m_model->apply(); }
    void cancel() override { m_model->reload(); }

private:
    CustomParsersModel *m_model = nullptr;
    QList<int> m_selectedRows;
};

class CustomParsersSettings final : public AspectContainer
{
public:
    CustomParsersSettings()
    {
        setAutoApply(false);
        setQmlSource(QUrl(
            "qrc:/qt/qml/QtCreator/ProjectExplorer/CustomParsersSettingsPage.qml"));

        m_hint.setQmlName("Hint");
        m_hint.setText(Tr::tr("Custom output parsers defined here can be enabled individually "
                              "in the project's build or run settings."));
        m_hint.setWordWrap(true);
        m_hint.setIconType(Utils::InfoType::Information);

        m_add.setQmlName("AddParser");
        m_add.setActionText(Tr::tr("Add..."));
        m_add.setAction([this] { addParser(); });

        m_edit.setQmlName("EditParser");
        m_edit.setActionText(Tr::tr("Edit..."));
        m_edit.setAction([this] { editParser(); });

        m_export.setQmlName("ExportParsers");
        m_export.setActionText(Tr::tr("Export..."));
        m_export.setAction([this] { exportParsers(); });

        m_import.setQmlName("ImportParsers");
        m_import.setActionText(Tr::tr("Import..."));
        m_import.setAction([this] { importParsers(); });

        connect(&m_parsers, &CustomParsersAspect::selectionChanged,
                this, &CustomParsersSettings::updateActions);
        updateActions();
    }

private:
    QList<CustomParserSettings> selectedParsers() const { return m_parsers.selectedParsers(); }

    void updateActions()
    {
        const QList<CustomParserSettings> selected = selectedParsers();
        const int modifiable = std::count_if(selected.cbegin(), selected.cend(),
                                            [](const CustomParserSettings &p) {
                                                return !p.readOnly;
                                            });
        m_export.setEnabled(!selected.isEmpty());
        // One at a time: the dialog edits a parser, not a set of them.
        m_edit.setEnabled(modifiable == 1);
    }

    void addParser()
    {
        CustomParserConfigDialog dlg(Core::ICore::dialogParent());
        dlg.setSettings(CustomParserSettings());
        if (dlg.exec() != QDialog::Accepted)
            return;
        CustomParserSettings newParser = dlg.settings();
        newParser.id = Utils::Id::generate();
        newParser.displayName = Tr::tr("New Parser");
        m_parsers.model()->add(newParser);
    }

    void editParser()
    {
        const QList<CustomParserSettings> selected = selectedParsers();
        if (selected.size() != 1)
            return;
        CustomParserSettings s = selected.first();

        CustomParserConfigDialog dlg(Core::ICore::dialogParent());
        dlg.setSettings(s);
        if (dlg.exec() != QDialog::Accepted)
            return;
        if (s.error == dlg.settings().error && s.warning == dlg.settings().warning)
            return;

        s.error = dlg.settings().error;
        s.warning = dlg.settings().warning;
        m_parsers.model()->setData(m_parsers.model()->index(m_parsers.selectedRows().first(), 0),
                                   QVariant::fromValue(s), Qt::UserRole);
    }

    void exportParsers()
    {
        QJsonArray jsonArray;
        for (const CustomParserSettings &parser : selectedParsers())
            jsonArray.append(parser.toJson());
        const FilePath jsonFile = FileUtils::getSaveFilePath(Tr::tr("Save Parsers"), {}, "*.json");
        if (jsonFile.isEmpty())
            return;
        const auto result = jsonFile.writeFileContents(QJsonDocument(jsonArray).toJson());
        if (!result) {
            QMessageBox::critical(Core::ICore::dialogParent(),
                                  Tr::tr("Error Saving Parsers"),
                                  Tr::tr("Error saving parsers: %1").arg(result.error()));
        }
    }

    void importParsers()
    {
        const FilePath jsonFile
            = FileUtils::getOpenFilePath(Tr::tr("Load Parsers"), {}, Tr::tr("*.json"));
        if (jsonFile.isEmpty())
            return;
        const auto parsersRead = CustomParsers::parsersFromFile(jsonFile);
        if (!parsersRead) {
            QMessageBox::critical(Core::ICore::dialogParent(),
                                  Tr::tr("Error Loading Parsers"),
                                  Tr::tr("Error loading parsers: %1").arg(parsersRead.error()));
            return;
        }
        for (const CustomParserSettings &parser : *parsersRead)
            m_parsers.model()->add(parser);
    }

    CustomParsersAspect m_parsers{this};
    TextDisplay m_hint{this};
    ActionAspect m_add{this};
    ActionAspect m_edit{this};
    ActionAspect m_export{this};
    ActionAspect m_import{this};
};

CustomParsersSettingsPage::CustomParsersSettingsPage()
{
    setId(Constants::CUSTOM_PARSERS_SETTINGS_PAGE_ID);
    setDisplayName(Tr::tr("Custom Output Parsers"));
    setCategory(Constants::BUILD_AND_RUN_SETTINGS_CATEGORY);
    setSettingsProvider([] {
        static CustomParsersSettings theSettings;
        return &theSettings;
    });
}

} // namespace ProjectExplorer::Internal

#include "customparserssettingspage.moc"
