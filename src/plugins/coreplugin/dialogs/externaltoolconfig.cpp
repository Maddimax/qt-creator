// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "externaltoolconfig.h"

#ifdef WITH_TESTS
#include <QTest>
#endif

#include "ioptionspage.h"
#include "../coreconstants.h"
#include "../coreplugintr.h"
#include "../externaltool.h"
#include "../externaltoolmanager.h"
#include "../icore.h"

#include <utils/algorithm.h>
#include <utils/aspectpresentation.h>
#include <utils/environment.h>
#include <utils/environmentdialog.h>
#include <utils/guiutils.h>
#include <utils/hostosinfo.h>
#include <utils/macroexpander.h>
#include <utils/pathchooser.h>
#include <utils/qtcassert.h>
#include <utils/shutdownguard.h>

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QRandomGenerator>
#include <QTextStream>

using namespace Utils;

namespace Core::Internal {

const Qt::ItemFlags TOOLSMENU_ITEM_FLAGS = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDropEnabled;
const Qt::ItemFlags CATEGORY_ITEM_FLAGS = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDropEnabled | Qt::ItemIsEditable;
const Qt::ItemFlags TOOL_ITEM_FLAGS = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled | Qt::ItemIsEditable;

class ExternalToolModel final : public QAbstractItemModel
{
public:
    ExternalToolModel() = default;
    ~ExternalToolModel() final;

    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &modelIndex, int role = Qt::DisplayRole) const override;
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    Qt::ItemFlags flags(const QModelIndex &modelIndex) const override;
    bool setData(const QModelIndex &modelIndex, const QVariant &value, int role = Qt::EditRole) override;

    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    bool dropMimeData(const QMimeData *data,
                      Qt::DropAction action,
                      int row,
                      int column,
                      const QModelIndex &parent) override;
    QStringList mimeTypes() const override;

    // The column has no name, and a header bar with nothing in it is not what
    // the widget view showed - it hid its header.
    QVariant headerData(int, Qt::Orientation, int) const override { return {}; }

    // What a Qt Quick view reads off a cell: which ones may be renamed in
    // place, out of the same flags a QTreeView reads them from.
    QHash<int, QByteArray> roleNames() const override
    {
        return AspectTable::withRoleNames(QAbstractItemModel::roleNames());
    }

    void setTools(const QMap<QString, QList<ExternalTool *> > &tools);
    QMap<QString, QList<ExternalTool *> > tools() const { return m_tools; }

    static ExternalTool *toolForIndex(const QModelIndex &modelIndex);
    std::optional<QString> categoryForIndex(const QModelIndex &modelIndex) const;
    void revertTool(const QModelIndex &modelIndex);
    QModelIndex addCategory();
    QModelIndex addTool(const QModelIndex &atIndex);
    void removeToolOrCategory(const QModelIndex &modelIndex);
    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }

private:
    static QVariant data(ExternalTool *tool, int role = Qt::DisplayRole);
    static QVariant data(const QString &category, int role = Qt::DisplayRole);

    QMap<QString, QList<ExternalTool *> > m_tools;
};

ExternalToolModel::~ExternalToolModel()
{
    for (QList<ExternalTool *> &toolInCategory : m_tools)
        qDeleteAll(toolInCategory);
}

int ExternalToolModel::columnCount(const QModelIndex &parent) const
{
    const bool categoryFound = categoryForIndex(parent).has_value();
    if (!parent.isValid() || toolForIndex(parent) || categoryFound)
        return 1;
    return 0;
}

QVariant ExternalToolModel::data(const QModelIndex &index, int role) const
{
    if (role == AspectTable::EditableRole)
        return AspectTable::isWritable(flags(index));
    if (role == AspectTable::CheckableRole)
        return false;
    if (ExternalTool *tool = toolForIndex(index))
        return data(tool, role);
    const std::optional<QString> category = categoryForIndex(index);
    if (category)
        return data(*category, role);
    return QVariant();
}

QVariant ExternalToolModel::data(ExternalTool *tool, int role)
{
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return tool->displayName();
    default:
        break;
    }
    return QVariant();
}

QVariant ExternalToolModel::data(const QString &category, int role)
{
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return category.isEmpty() ? Tr::tr("Uncategorized") : category;
    case Qt::ToolTipRole:
        return category.isEmpty() ? Tr::tr("Tools that will appear directly under the External Tools menu.") : QVariant();
    default:
        break;
    }
    return QVariant();
}

QMimeData *ExternalToolModel::mimeData(const QModelIndexList &indexes) const
{
    if (indexes.isEmpty())
        return nullptr;
    QModelIndex modelIndex = indexes.first();
    ExternalTool *tool = toolForIndex(modelIndex);
    QTC_ASSERT(tool, return nullptr);
    const std::optional<QString> category = categoryForIndex(modelIndex.parent());
    QTC_ASSERT(category, return nullptr);
    auto md = new QMimeData();
    QByteArray ba;
    QDataStream stream(&ba, QIODevice::WriteOnly);
    stream << *category << m_tools.value(*category).indexOf(tool);
    md->setData("application/qtcreator-externaltool-config", ba);
    return md;
}

bool ExternalToolModel::dropMimeData(const QMimeData *data,
                                     Qt::DropAction action,
                                     int row,
                                     int column,
                                     const QModelIndex &parent)
{
    Q_UNUSED(column)
    if (action != Qt::MoveAction || !data)
        return false;
    const std::optional<QString> toCategory = categoryForIndex(parent);
    QTC_ASSERT(toCategory, return false);
    QByteArray ba = data->data("application/qtcreator-externaltool-config");
    if (ba.isEmpty())
        return false;
    QDataStream stream(&ba, QIODevice::ReadOnly);
    QString category;
    qsizetype pos = -1;
    stream >> category;
    stream >> pos;
    QList<ExternalTool *> &items = m_tools[category];
    QTC_ASSERT(pos >= 0 && pos < items.count(), return false);
    const int sourceCategoryIndex = std::distance(m_tools.constBegin(), m_tools.constFind(category));
    const int targetCategoryIndex
        = std::distance(m_tools.constBegin(), m_tools.constFind(*toCategory));
    QTC_ASSERT(sourceCategoryIndex >= 0 && targetCategoryIndex >= 0, return false);
    if (row < 0) // target row can be -1 when dropping onto the category itself
        row = 0;
    if (sourceCategoryIndex == targetCategoryIndex) {
        if (row == pos || row == pos + 1) // would end at the same place, don't
            return false;
    }
    beginMoveRows(index(sourceCategoryIndex, 0), pos, pos, index(targetCategoryIndex, 0), row);
    ExternalTool *tool = items.takeAt(pos);
    if (category == *toCategory && pos < row) // adapt the target row for the removed item
        --row;
    m_tools[*toCategory].insert(row, tool);
    endMoveRows();
    return true;
}

QStringList ExternalToolModel::mimeTypes() const
{
    return {"application/qtcreator-externaltool-config"};
}

QModelIndex ExternalToolModel::index(int row, int column, const QModelIndex &parent) const
{
    if (column == 0 && parent.isValid()) {
        const std::optional<QString> category = categoryForIndex(parent);
        if (category) {
            QList<ExternalTool *> items = m_tools.value(*category);
            if (row < items.count())
                return createIndex(row, 0, items.at(row));
        }
    } else if (column == 0 && row < m_tools.size()) {
        return createIndex(row, 0);
    }
    return QModelIndex();
}

QModelIndex ExternalToolModel::parent(const QModelIndex &child) const
{
    if (ExternalTool *tool = toolForIndex(child)) {
        int categoryIndex = 0;
        for (const QList<ExternalTool *> &toolsInCategory : m_tools) {
            if (toolsInCategory.contains(tool))
                return index(categoryIndex, 0);
            ++categoryIndex;
        }
    }
    return QModelIndex();
}

int ExternalToolModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return m_tools.size();
    if (toolForIndex(parent))
        return 0;
    std::optional<QString> category = categoryForIndex(parent);
    if (category)
        return m_tools.value(*category).count();

    return 0;
}

Qt::ItemFlags ExternalToolModel::flags(const QModelIndex &index) const
{
    if (toolForIndex(index))
        return TOOL_ITEM_FLAGS;
    const std::optional<QString> category = categoryForIndex(index);
    if (category) {
        if (category->isEmpty())
            return TOOLSMENU_ITEM_FLAGS;
        return CATEGORY_ITEM_FLAGS;
    }
    return {};
}

bool ExternalToolModel::setData(const QModelIndex &modelIndex, const QVariant &value, int role)
{
    if (role != Qt::EditRole)
        return false;
    QString string = value.toString();
    if (ExternalTool *tool = toolForIndex(modelIndex)) {
        if (string.isEmpty() || tool->displayName() == string)
            return false;
        // rename tool
        tool->setDisplayName(string);
        emit dataChanged(modelIndex, modelIndex);
        return true;
    } else {
        std::optional<QString> category = categoryForIndex(modelIndex);
        if (category) {
            if (string.isEmpty() || m_tools.contains(string))
                return false;
            // rename category
            QStringList categories = m_tools.keys();
            int previousIndex = categories.indexOf(*category);
            categories.removeAt(previousIndex);
            categories.append(string);
            Utils::sort(categories);
            int newIndex = categories.indexOf(string);
            if (newIndex != previousIndex) {
                // we have same parent so we have to do special stuff for beginMoveRows...
                int beginMoveRowsSpecialIndex = (previousIndex < newIndex ? newIndex + 1 : newIndex);
                beginMoveRows(QModelIndex(), previousIndex, previousIndex, QModelIndex(), beginMoveRowsSpecialIndex);
            }
            QList<ExternalTool *> items = m_tools.take(*category);
            m_tools.insert(string, items);
            if (newIndex != previousIndex)
                endMoveRows();
            return true;
        }
    }
    return false;
}

void ExternalToolModel::setTools(const QMap<QString, QList<ExternalTool *> > &tools)
{
    beginResetModel();
    m_tools = tools;
    endResetModel();
}

ExternalTool *ExternalToolModel::toolForIndex(const QModelIndex &index)
{
    return static_cast<ExternalTool *>(index.internalPointer());
}

std::optional<QString> ExternalToolModel::categoryForIndex(const QModelIndex &index) const
{
    if (index.isValid() && !index.parent().isValid() && index.column() == 0 && index.row() >= 0) {
        const QStringList &keys = m_tools.keys();
        if (index.row() < keys.count())
            return keys.at(index.row());
    }
    return {};
}

void ExternalToolModel::revertTool(const QModelIndex &modelIndex)
{
    ExternalTool *tool = toolForIndex(modelIndex);
    QTC_ASSERT(tool, return);
    QTC_ASSERT(tool->preset() && !tool->preset()->filePath().isEmpty(), return);
    auto resetTool = new ExternalTool(tool->preset().get());
    resetTool->setPreset(tool->preset());
    (*tool) = (*resetTool);
    delete resetTool;
    emit dataChanged(modelIndex, modelIndex);
}

QModelIndex ExternalToolModel::addCategory()
{
    const QString &categoryBase = Tr::tr("New Category");
    QString category = categoryBase;
    int count = 0;
    while (m_tools.contains(category)) {
        ++count;
        category = categoryBase + QString::number(count);
    }
    QStringList categories = m_tools.keys();
    categories.append(category);
    Utils::sort(categories);
    int pos = categories.indexOf(category);

    beginInsertRows(QModelIndex(), pos, pos);
    m_tools.insert(category, QList<ExternalTool *>());
    endInsertRows();
    return index(pos, 0);
}

QModelIndex ExternalToolModel::addTool(const QModelIndex &atIndex)
{
    std::optional<QString> category = categoryForIndex(atIndex);
    if (!category)
        category = categoryForIndex(atIndex.parent());
    QTC_ASSERT(category, return QModelIndex());
    auto tool = new ExternalTool;
    tool->setDisplayCategory(*category);
    tool->setDisplayName(Tr::tr("New Tool"));
    tool->setDescription(Tr::tr("This tool prints a line of useful text"));
    //: Sample external tool text
    const QString text = Tr::tr("Useful text");
    if (HostOsInfo::isWindowsHost()) {
        tool->setExecutables({"cmd"});
        tool->setArguments("/c echo " + text);
    } else {
        tool->setExecutables({"echo"});
        tool->setArguments(text);
    }

    int pos;
    QModelIndex parent;
    if (atIndex.parent().isValid()) {
        pos = atIndex.row() + 1;
        parent = atIndex.parent();
    } else {
        pos = m_tools.value(*category).count();
        parent = atIndex;
    }
    beginInsertRows(parent, pos, pos);
    m_tools[*category].insert(pos, tool);
    endInsertRows();
    return index(pos, 0, parent);
}

void ExternalToolModel::removeToolOrCategory(const QModelIndex &modelIndex)
{
    if (modelIndex.parent().isValid()) {
        ExternalTool *tool = toolForIndex(modelIndex);
        QTC_ASSERT(tool, return);
        QTC_ASSERT(!tool->preset(), return);
        // remove the tool and the tree item
        int categoryIndex = 0;
        for (QList<ExternalTool *> &items : m_tools) {
            int pos = items.indexOf(tool);
            if (pos != -1) {
                beginRemoveRows(index(categoryIndex, 0), pos, pos);
                items.removeAt(pos);
                endRemoveRows();
                break;
            }
            ++categoryIndex;
        }
        delete tool;
    } else {
        // category
        const std::optional<QString> category = categoryForIndex(modelIndex);
        QTC_ASSERT(category, return);
        QTC_ASSERT(m_tools.value(*category).isEmpty(), return);
        beginRemoveRows(QModelIndex(), modelIndex.row(), modelIndex.row());
        m_tools.remove(*category);
        endRemoveRows();
    }
}

// The tools, as the page lists them: categories with tools under them, each
// renamed in place and dragged into whatever order and category the user
// wants. All of that is the model's answer already; the aspect only says the
// tree may be reordered.
class ToolTreeAspect final : public BaseAspect
{
    Q_OBJECT

public:
    using BaseAspect::BaseAspect;

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Tree;
        p.allowReordering = true;
        return p;
    }

    QAbstractItemModel *tableModel() override { return &m_model; }
    ExternalToolModel &model() { return m_model; }

    // Which tool the form beside the tree is about. The view says so; the page
    // reads it.
    Q_INVOKABLE void setCurrentIndex(const QModelIndex &index)
    {
        if (index == m_current)
            return;
        m_current = index;
        emit currentChanged();
    }

    QModelIndex currentIndex() const { return m_current; }
    ExternalTool *currentTool() const { return ExternalToolModel::toolForIndex(m_current); }

signals:
    void currentChanged();

private:
    ExternalToolModel m_model;
    QModelIndex m_current;
};

// The environment a tool runs in: a summary of what was changed and one button
// that opens the dialog which changes it.
class EnvironmentChangesAspect final : public BaseAspect
{
    Q_OBJECT

public:
    using BaseAspect::BaseAspect;

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::TextWithAction;
        p.actionText = Tr::tr("Change...");
        return p;
    }

    QString displayText() const override
    {
        return m_changes.toShortSummary(globalMacroExpander(), true);
    }

    EnvironmentChanges changes() const { return m_changes; }

    void setChanges(const EnvironmentChanges &changes)
    {
        if (m_changes == changes)
            return;
        m_changes = changes;
        emit displayTextChanged();
        // As any other field says it: what the page listens to is the
        // container's volatile value, and a dialog's answer is an edit like
        // any other.
        emit volatileValueChanged();
    }

    void triggerAction() override
    {
        const QString placeholderText = HostOsInfo::isWindowsHost()
                                            ? Tr::tr("PATH=C:\\dev\\bin;${PATH}")
                                            : Tr::tr("PATH=/opt/bin:${PATH}");
        const std::optional<EnvironmentChanges> newItems
            = runEnvironmentItemsDialog(Utils::dialogParent(), m_changes, placeholderText);
        if (!newItems)
            return;
        setChanges(*newItems);
    }

private:
    EnvironmentChanges m_changes;
};

class ExternalToolsAspects final : public AspectContainer
{
public:
    ExternalToolsAspects();

    void apply() override;
    void cancel() override;

    QMap<QString, QList<ExternalTool *>> tools() { return m_tools.model().tools(); }

    ToolTreeAspect &toolTree() { return m_tools; }
    AspectContainer &details() { return m_details; }
    StringAspect &description() { return m_description; }
    EnvironmentChangesAspect &environment() { return m_environment; }
    void addToolFromTest() { m_addTool.triggerAction(); }
    void removeFromTest() { m_remove.triggerAction(); }

private:
    void setTools(const QMap<QString, QList<ExternalTool *>> &tools);
    void showCurrentTool();
    void writeCurrentTool();
    void updateButtons();
    void updateEffectiveArguments();
    bool m_showing = false;

    ToolTreeAspect m_tools{this};
    ActionAspect m_addTool{this};
    ActionAspect m_addCategory{this};
    ActionAspect m_remove{this};
    ActionAspect m_revert{this};

    AspectContainer m_details{this};
    StringAspect m_description{&m_details};
    FilePathAspect m_executable{&m_details};
    StringAspect m_arguments{&m_details};
    FilePathAspect m_workingDirectory{&m_details};
    SelectionAspect m_output{&m_details};
    SelectionAspect m_errorOutput{&m_details};
    SelectionAspect m_baseEnvironment{&m_details};
    EnvironmentChangesAspect m_environment{&m_details};
    BoolAspect m_modifiesDocument{&m_details};
    StringAspect m_input{&m_details};
};

ExternalToolsAspects::ExternalToolsAspects()
{
    setAutoApply(false);
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Core/ExternalToolsPage.qml"));

    m_tools.setQmlName("Tools");

    // What a new entry is is the page's business, and there are two kinds, so
    // there are two buttons rather than a menu.
    m_addTool.setQmlName("AddTool");
    m_addTool.setActionText(Tr::tr("Add Tool"));
    m_addTool.setToolTip(Tr::tr("Add tool."));
    m_addTool.setAction([this] {
        QModelIndex at = m_tools.currentIndex();
        if (!at.isValid()) // Default to Uncategorized.
            at = m_tools.model().index(0, 0);
        m_tools.setCurrentIndex(m_tools.model().addTool(at));
    });

    m_addCategory.setQmlName("AddCategory");
    m_addCategory.setActionText(Tr::tr("Add Category"));
    m_addCategory.setAction([this] {
        m_tools.setCurrentIndex(m_tools.model().addCategory());
    });

    m_remove.setQmlName("Remove");
    m_remove.setActionText(Tr::tr("Remove"));
    m_remove.setAction([this] {
        // Nothing is current first: moving off a tool writes the form back
        // into it, and the tool is about to be deleted.
        const QModelIndex index = m_tools.currentIndex();
        m_tools.setCurrentIndex({});
        m_tools.model().removeToolOrCategory(index);
    });

    m_revert.setQmlName("Revert");
    m_revert.setActionText(Tr::tr("Reset"));
    m_revert.setToolTip(Tr::tr("Revert tool to default."));
    m_revert.setAction([this] {
        m_tools.model().revertTool(m_tools.currentIndex());
        showCurrentTool();
    });

    m_details.setQmlName("Details");

    m_description.setQmlName("Description");
    m_description.setLabelText(Tr::tr("Description:"));
    m_description.setDisplayStyle(StringAspect::LineEditDisplay);

    m_executable.setQmlName("Executable");
    m_executable.setLabelText(Tr::tr("Executable:", "noun"));
    m_executable.setExpectedKind(PathChooserKind::ExistingCommand);

    m_arguments.setQmlName("Arguments");
    m_arguments.setLabelText(Tr::tr("Arguments:"));
    m_arguments.setDisplayStyle(StringAspect::LineEditDisplay);

    m_workingDirectory.setQmlName("WorkingDirectory");
    m_workingDirectory.setLabelText(Tr::tr("Working directory:"));
    m_workingDirectory.setExpectedKind(PathChooserKind::ExistingDirectory);

    const QString outputTip = Tr::tr(
        "<p>What to do with the executable's standard output?</p>\n"
        "<ul>\n"
        "<li>Ignore: Do nothing with it.</li>\n"
        "<li>Show in General Messages.</li>\n"
        "<li>Replace selection: Replace the current selection in the current document with it.</li>\n"
        "</ul>");
    m_output.setQmlName("Output");
    m_output.setLabelText(Tr::tr("Output:"));
    m_output.setToolTip(outputTip);
    m_output.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    // In OutputHandling's order, so the index is the enumerator.
    m_output.addOption(Tr::tr("Ignore"));
    m_output.addOption(Tr::tr("Show in General Messages"));
    m_output.addOption(Tr::tr("Replace Selection"));

    m_errorOutput.setQmlName("ErrorOutput");
    m_errorOutput.setLabelText(Tr::tr("Error output:"));
    m_errorOutput.setToolTip(Tr::tr(
        "<p>What to do with the executable's standard error output?</p>\n"
        "<ul>\n"
        "<li>Ignore: Do nothing with it.</li>\n"
        "<li>Show in General Messages.</li>\n"
        "<li>Replace selection: Replace the current selection in the current document with it.</li>\n"
        "</ul>"));
    m_errorOutput.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    m_errorOutput.addOption(Tr::tr("Ignore"));
    m_errorOutput.addOption(Tr::tr("Show in General Messages"));
    m_errorOutput.addOption(Tr::tr("Replace Selection"));

    m_baseEnvironment.setQmlName("BaseEnvironment");
    m_baseEnvironment.setLabelText(Tr::tr("Base environment:"));
    m_baseEnvironment.setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    m_baseEnvironment.setUseDataAsSavedValue();
    m_baseEnvironment.addOption({Tr::tr("System Environment"), {}, QByteArray()});
    for (const EnvironmentProvider &provider : EnvironmentProvider::providers())
        m_baseEnvironment.addOption({provider.displayName, {}, Id::fromName(provider.id).toSetting()});

    m_environment.setQmlName("Environment");
    m_environment.setLabelText(Tr::tr("Environment:"));

    m_modifiesDocument.setQmlName("ModifiesDocument");
    m_modifiesDocument.setLabelText(Tr::tr("Modifies current document"));
    m_modifiesDocument.setLabelPlacement(BoolAspect::LabelPlacement::AtCheckBox);
    m_modifiesDocument.setToolTip(
        Tr::tr("If the tool modifies the current document, "
               "set this flag to ensure that the document is saved before "
               "running the tool and is reloaded after the tool finished."));

    m_input.setQmlName("Input");
    m_input.setLabelText(Tr::tr("Input:"));
    m_input.setDisplayStyle(StringAspect::TextEditDisplay);
    m_input.setToolTip(Tr::tr("Text to pass to the executable via standard input. Leave "
                              "empty if the executable should not receive any input."));

    // Behaviour, not layout.
    connect(&m_tools, &ToolTreeAspect::currentChanged, this, [this] { showCurrentTool(); });
    connect(&m_details, &AspectContainer::volatileValueChanged, this, [this] {
        if (!m_showing)
            writeCurrentTool();
    });
    m_arguments.addOnVolatileValueChanged(this, [this] { updateEffectiveArguments(); });
    connect(&m_tools.model(), &QAbstractItemModel::dataChanged, this, &markSettingsDirty);
    connect(&m_tools.model(), &QAbstractItemModel::rowsMoved, this, &markSettingsDirty);
    connect(&m_tools.model(), &QAbstractItemModel::rowsInserted, this, &markSettingsDirty);
    connect(&m_tools.model(), &QAbstractItemModel::rowsRemoved, this, &markSettingsDirty);

    // A tool's arguments name Qt Creator's variables, so the fields expand
    // against them rather than against nothing.
    const QList<BaseAspect *> expanding{&m_executable, &m_arguments, &m_workingDirectory, &m_input};
    for (BaseAspect *field : expanding)
        field->setMacroExpander(globalMacroExpander());

    setTools(ExternalToolManager::toolsByCategory());
    showCurrentTool();
}

void ExternalToolsAspects::setTools(const QMap<QString, QList<ExternalTool *>> &tools)
{
    QMap<QString, QList<ExternalTool *>> toolsCopy;
    for (auto it = tools.cbegin(), end = tools.cend(); it != end; ++it) {
        QList<ExternalTool *> itemCopy;
        for (ExternalTool *tool : it.value())
            itemCopy.append(new ExternalTool(tool));
        toolsCopy.insert(it.key(), itemCopy);
    }
    if (!toolsCopy.contains(QString()))
        toolsCopy.insert(QString(), QList<ExternalTool *>());
    m_tools.setCurrentIndex({});
    m_tools.model().setTools(toolsCopy);
}

void ExternalToolsAspects::updateButtons()
{
    const QModelIndex index = m_tools.currentIndex();
    const ExternalTool *tool = ExternalToolModel::toolForIndex(index);
    if (!tool) {
        const bool isCategory = m_tools.model().categoryForIndex(index).has_value();
        const bool isEmpty = m_tools.model().rowCount(index) == 0;
        m_remove.setEnabled(isCategory && isEmpty);
        m_revert.setEnabled(false);
        return;
    }
    if (!tool->preset()) {
        m_remove.setEnabled(true);
        m_revert.setEnabled(false);
    } else {
        m_remove.setEnabled(false);
        m_revert.setEnabled((*tool) != (*(tool->preset())));
    }
}

void ExternalToolsAspects::showCurrentTool()
{
    updateButtons();
    const ExternalTool *tool = m_tools.currentTool();
    m_details.setEnabled(tool != nullptr);

    // Filling the form in is not the user editing it.
    m_showing = true;
    const QScopeGuard done([this] { m_showing = false; });
    if (!tool) {
        m_description.setValue({});
        m_executable.setValue(FilePath());
        m_arguments.setValue({});
        m_workingDirectory.setValue(FilePath());
        m_input.setValue({});
        m_environment.setChanges({});
        return;
    }

    m_description.setValue(tool->description());
    m_executable.setValue(tool->executables().isEmpty() ? FilePath()
                                                        : tool->executables().constFirst());
    m_arguments.setValue(tool->arguments());
    m_workingDirectory.setValue(tool->workingDirectory());
    m_output.setValue(int(tool->outputHandling()));
    m_errorOutput.setValue(int(tool->errorHandling()));
    m_modifiesDocument.setValue(tool->modifiesCurrentDocument());
    m_baseEnvironment.setValue(
        qMax(0, m_baseEnvironment.indexForItemValue(tool->baseEnvironmentProviderId().toSetting())));
    m_environment.setChanges(tool->environmentUserChanges());
    m_input.setValue(tool->input());
    updateEffectiveArguments();
}

void ExternalToolsAspects::writeCurrentTool()
{
    ExternalTool *tool = m_tools.currentTool();
    if (!tool)
        return;

    tool->setDescription(m_description.volatileValue());
    FilePaths executables = tool->executables();
    const FilePath executable = FilePath::fromUserInput(m_executable.volatileValue());
    if (executables.isEmpty())
        executables << executable;
    else
        executables[0] = executable;
    tool->setExecutables(executables);
    tool->setArguments(m_arguments.volatileValue());
    tool->setWorkingDirectory(FilePath::fromUserInput(m_workingDirectory.volatileValue()));
    tool->setBaseEnvironmentProviderId(Id::fromSetting(m_baseEnvironment.itemValue()));
    tool->setEnvironmentUserChanges(m_environment.changes());
    tool->setOutputHandling(ExternalTool::OutputHandling(m_output.volatileValue()));
    tool->setErrorHandling(ExternalTool::OutputHandling(m_errorOutput.volatileValue()));
    tool->setModifiesCurrentDocument(m_modifiesDocument.volatileValue() ? Qt::Checked
                                                                       : Qt::Unchecked);
    tool->setInput(m_input.volatileValue());
    updateButtons();
}

void ExternalToolsAspects::updateEffectiveArguments()
{
    const Result<QString> result
        = globalMacroExpander()->expandProcessArgs(m_arguments.volatileValue());
    m_arguments.setToolTip(result ? *result : result.error());
}

void ExternalToolsAspects::cancel()
{
    AspectContainer::cancel();
    // The page is built once, so it starts again from what the tools are
    // rather than from what was being edited.
    setTools(ExternalToolManager::toolsByCategory());
    showCurrentTool();
}


static FilePath getUserFilePath(const QString &proposalFileName)
{
    const FilePath resourceDir(ICore::userResourcePath());
    const FilePath externalToolsDir = resourceDir / "externaltools";
    if (!externalToolsDir.isDir())
        externalToolsDir.createDir();

    const FilePath proposal = FilePath::fromString(proposalFileName);
    const QString suffix = QLatin1Char('.') + proposal.suffix();
    const FilePath newFilePath = externalToolsDir / proposal.baseName();

    int count = 0;
    FilePath tryPath = newFilePath.stringAppended(suffix);
    while (tryPath.exists()) {
        if (++count > 15)
            return {};
        // add random number
        const int number = QRandomGenerator::global()->generate() % 1000;
        tryPath = newFilePath.stringAppended(QString::number(number) + suffix);
    }
    return tryPath;
}

static QString idFromDisplayName(const QString &displayName)
{
    QString id = displayName;
    static const QRegularExpression regexp("&(?!&)");
    id.remove(regexp);
    QChar *c = id.data();
    while (!c->isNull()) {
        if (!c->isLetterOrNumber())
            *c = QLatin1Char('_');
        ++c;
    }
    return id;
}

static QString findUnusedId(const QString &proposal, const QMap<QString, QList<ExternalTool *> > &tools)
{
    int number = 0;
    QString result;
    bool found = false;
    do {
        result = proposal + (number > 0 ? QString::number(number) : QString::fromLatin1(""));
        ++number;
        found = false;
        for (auto it = tools.cbegin(), end = tools.cend(); it != end; ++it) {
            const QList<ExternalTool *> tools = it.value();
            for (const ExternalTool *tool : tools) {
                if (tool->id() == result) {
                    found = true;
                    break;
                }
            }
        }
    } while (found);
    return result;
}

void ExternalToolsAspects::apply()
{
    AspectContainer::apply();
    writeCurrentTool();

    QMap<QString, ExternalTool *> originalTools = ExternalToolManager::toolsById();
    QMap<QString, QList<ExternalTool *> > newToolsMap = tools();
    QMap<QString, QList<ExternalTool *> > resultMap;
    for (auto it = newToolsMap.cbegin(), end = newToolsMap.cend(); it != end; ++it) {
        QList<ExternalTool *> items;
        const QList<ExternalTool *> tools = it.value();
        for (ExternalTool *tool : tools) {
            ExternalTool *toolToAdd = nullptr;
            if (ExternalTool *originalTool = originalTools.take(tool->id())) {
                // check if it has different category and is custom tool
                if (tool->displayCategory() != it.key() && !tool->preset())
                    tool->setDisplayCategory(it.key());
                // check if the tool has changed
                if ((*originalTool) == (*tool)) {
                    toolToAdd = originalTool;
                } else {
                    // case 1: tool is changed preset
                    if (tool->preset() && (*tool) != (*(tool->preset()))) {
                        // check if we need to choose a new file name
                        if (tool->preset()->filePath() == tool->filePath()) {
                            const QString &fileName = tool->preset()->filePath().fileName();
                            const FilePath &newFilePath = getUserFilePath(fileName);
                            // TODO error handling if newFilePath.isEmpty() (i.e. failed to find a unused name)
                            tool->setFilePath(newFilePath);
                        }
                        // TODO error handling
                        tool->save();
                    // case 2: tool is previously changed preset but now same as preset
                    } else if (tool->preset() && (*tool) == (*(tool->preset()))) {
                        // check if we need to delete the changed description
                        if (originalTool->filePath() != tool->preset()->filePath()
                                && originalTool->filePath().exists()) {
                            // TODO error handling
                            originalTool->filePath().removeFile();
                        }
                        tool->setFilePath(tool->preset()->filePath());
                        // no need to save, it's the same as the preset
                    // case 3: tool is custom tool
                    } else {
                        // TODO error handling
                        tool->save();
                    }

                     // 'tool' is deleted by config page, 'originalTool' is deleted by setToolsByCategory
                    toolToAdd = new ExternalTool(tool);
                }
            } else {
                // new tool. 'tool' is deleted by config page
                QString id = idFromDisplayName(tool->displayName());
                id = findUnusedId(id, newToolsMap);
                tool->setId(id);
                // TODO error handling if newFilePath.isEmpty() (i.e. failed to find a unused name)
                tool->setFilePath(getUserFilePath(id + QLatin1String(".xml")));
                // TODO error handling
                tool->save();
                toolToAdd = new ExternalTool(tool);
            }
            items.append(toolToAdd);
        }
        if (!items.isEmpty())
            resultMap.insert(it.key(), items);
    }
    // Remove tools that have been deleted from the settings (and are no preset)
    for (const ExternalTool *tool : std::as_const(originalTools)) {
        QTC_ASSERT(!tool->preset(), continue);
        // TODO error handling
        tool->filePath().removeFile();
    }

    ExternalToolManager::setToolsByCategory(resultMap);
}

// ExternalToolSettings

class ExternalToolSettings final : public IOptionsPage
{
public:
    ExternalToolSettings()
    {
        setId(Constants::SETTINGS_ID_TOOLS);
        setDisplayName(Tr::tr("External Tools"));
        setCategory(Constants::SETTINGS_CATEGORY_CORE);
        setSettingsProvider([] {
            static GuardedObject<ExternalToolsAspects> theAspects;
            return theAspects.get();
        });
    }
};

#ifdef WITH_TESTS
class ExternalToolSettingsTest final : public QObject
{
    Q_OBJECT

private slots:
    void cleanup()
    {
        if (ExternalToolsAspects *p = page())
            static_cast<BaseAspect *>(p)->cancel();
    }

    void testPickingAToolFillsInTheFormAndEditingWritesItBack()
    {
        ExternalToolsAspects *p = page();
        QVERIFY(p);
        const QModelIndex tool = firstTool(p);
        if (!tool.isValid())
            QSKIP("No external tools are installed here");

        // Nothing is picked to begin with, so there is nothing to fill in.
        QVERIFY(!p->details().isEnabled());

        p->toolTree().setCurrentIndex(tool);
        QVERIFY(p->details().isEnabled());
        ExternalTool *original = ExternalToolModel::toolForIndex(tool);
        QVERIFY(original);
        QCOMPARE(p->description().volatileValue(), original->description());

        // And what is typed reaches the tool it is about.
        p->description().setValue("Something else");
        QCOMPARE(original->description(), QString("Something else"));
    }

    void testMovingOnLeavesTheToolThatWasBeingEdited()
    {
        // The form belongs to the tool that is going away, so it is written
        // back before the next one is shown - otherwise an edit is lost the
        // moment the selection moves.
        ExternalToolsAspects *p = page();
        QVERIFY(p);
        const QModelIndex first = firstTool(p);
        if (!first.isValid())
            QSKIP("No external tools are installed here");
        const QModelIndex category = first.parent();
        if (p->toolTree().model().rowCount(category) < 2)
            QSKIP("This needs a category with two tools in it");
        const QModelIndex second = p->toolTree().model().index(1, 0, category);

        p->toolTree().setCurrentIndex(first);
        p->description().setValue("Edited");
        p->toolTree().setCurrentIndex(second);
        QCOMPARE(ExternalToolModel::toolForIndex(first)->description(), QString("Edited"));
        // And the form is now about the other one.
        QCOMPARE(p->description().volatileValue(),
                 ExternalToolModel::toolForIndex(second)->description());
    }

    void testTheEnvironmentDialogsAnswerReachesTheTool()
    {
        // The environment is edited in a dialog rather than in a field, and a
        // dialog says what it did differently - it has to reach the tool all
        // the same.
        ExternalToolsAspects *p = page();
        QVERIFY(p);
        const QModelIndex tool = firstTool(p);
        if (!tool.isValid())
            QSKIP("No external tools are installed here");
        p->toolTree().setCurrentIndex(tool);

        EnvironmentChanges changes;
        changes.setItemsFromUser({{"SOME_VARIABLE", "some value"}});
        p->environment().setChanges(changes);

        ExternalTool *original = ExternalToolModel::toolForIndex(tool);
        QVERIFY(original);
        QCOMPARE(original->environmentUserChanges().itemsFromUser().size(), 1);
        QCOMPARE(original->environmentUserChanges().itemsFromUser().first().name,
                 QString("SOME_VARIABLE"));
    }

    void testAddingAndRemovingATool()
    {
        ExternalToolsAspects *p = page();
        QVERIFY(p);
        const QModelIndex uncategorized = p->toolTree().model().index(0, 0);
        QVERIFY(uncategorized.isValid());
        const int before = p->toolTree().model().rowCount(uncategorized);

        p->toolTree().setCurrentIndex(uncategorized);
        p->addToolFromTest();
        QCOMPARE(p->toolTree().model().rowCount(uncategorized), before + 1);
        // A tool the user made is theirs to take away again; a preset is not.
        QVERIFY(ExternalToolModel::toolForIndex(p->toolTree().currentIndex()));
        p->removeFromTest();
        QCOMPARE(p->toolTree().model().rowCount(uncategorized), before);
    }

private:
    static QModelIndex firstTool(ExternalToolsAspects *p)
    {
        ExternalToolModel &model = p->toolTree().model();
        for (int i = 0, count = model.rowCount(); i < count; ++i) {
            const QModelIndex category = model.index(i, 0);
            if (model.rowCount(category) > 0)
                return model.index(0, 0, category);
        }
        return {};
    }

    static ExternalToolsAspects *page()
    {
        Core::IOptionsPage *found = Utils::findOrDefault(
            Core::IOptionsPage::allOptionsPages(), [](Core::IOptionsPage *p) {
                return p->id() == Id(Constants::SETTINGS_ID_TOOLS);
            });
        if (!found)
            return nullptr;
        const std::optional<AspectContainer *> aspects = found->aspects();
        return aspects ? static_cast<ExternalToolsAspects *>(*aspects) : nullptr;
    }
};

QObject *createExternalToolSettingsTest()
{
    return new ExternalToolSettingsTest;
}
#endif // WITH_TESTS

void setupExternalToolSettings()
{
    static ExternalToolSettings theExternalToolSettings;
}

} // Core::Internal

#include "externaltoolconfig.moc"
