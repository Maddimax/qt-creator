// Copyright (C) 2016 Nicolas Arnaud-Cormos
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "macrooptionspage.h"

#include "macro.h"
#include "macromanager.h"
#include "macrosconstants.h"
#include "macrostr.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/dialogs/ioptionspage.h>

#include <texteditor/texteditorconstants.h>

#include <utils/aspectpresentation.h>
#include <utils/aspects.h>
#include <utils/id.h>

#include <QAbstractTableModel>
#include <QAction>
#include <QDir>
#include <QFileInfo>

using namespace Utils;

namespace Macros::Internal {

// The macros stored in the macros directory. Ones loaded from elsewhere are not
// the user's to rename or remove, and the page never showed them.
static QList<Macro *> ownMacros()
{
    QList<Macro *> macros;
    const QDir dir(MacroManager::macrosDirectory());
    for (Macro *macro : MacroManager::macros()) {
        if (QFileInfo(macro->fileName()).absoluteDir() == dir.absolutePath())
            macros.append(macro);
    }
    return macros;
}

static MacroDescriptions descriptionsFromManager()
{
    MacroDescriptions descriptions;
    for (Macro *macro : ownMacros())
        descriptions.insert(macro->displayName(), macro->description());
    return descriptions;
}

// What a macro is called, what it does and how it is invoked. The name and the
// shortcut belong to the macro and to the action manager; only the description
// is the page's to change, and only for a macro that can be written.
class MacroTableModel final : public QAbstractTableModel
{
public:
    enum Column : int { Name = 0, Description, Shortcut };

    using QAbstractTableModel::QAbstractTableModel;

    void setDescriptions(const MacroDescriptions &descriptions)
    {
        beginResetModel();
        m_names = descriptions.keys();
        m_descriptions = descriptions;
        endResetModel();
    }

    MacroDescriptions descriptions() const { return m_descriptions; }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_names.size();
    }
    int columnCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : 3;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};
        switch (section) {
        case Name: return Tr::tr("Name");
        case Description: return Tr::tr("Description");
        case Shortcut: return Tr::tr("Shortcut");
        }
        return {};
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_names.size())
            return {};
        const QString name = m_names.at(index.row());

        if (role == Qt::DisplayRole || role == Qt::EditRole) {
            switch (index.column()) {
            case Name: return name;
            case Description: return m_descriptions.value(name);
            case Shortcut: return shortcutFor(name);
            }
        } else if (role == AspectTable::EditableRole) {
            return AspectTable::isWritable(flags(index));
        } else if (role == AspectTable::CheckableRole) {
            return false;
        }
        return {};
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override
    {
        if (role != Qt::EditRole || index.column() != Description
            || index.row() >= m_names.size()) {
            return false;
        }
        m_descriptions.insert(m_names.at(index.row()), value.toString());
        emit dataChanged(index, index);
        return true;
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        if (!index.isValid())
            return Qt::NoItemFlags;
        Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (index.column() == Description && isWritable(m_names.value(index.row())))
            flags |= Qt::ItemIsEditable;
        return flags;
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return AspectTable::withRoleNames(QAbstractTableModel::roleNames());
    }

    bool removeRows(int row, int count, const QModelIndex &parent = {}) override
    {
        if (parent.isValid() || row < 0 || row + count > m_names.size())
            return false;
        beginRemoveRows({}, row, row + count - 1);
        for (int i = 0; i < count; ++i)
            m_descriptions.remove(m_names.at(row + i));
        m_names.remove(row, count);
        endRemoveRows();
        return true;
    }

private:
    static bool isWritable(const QString &name)
    {
        Macro *macro = MacroManager::macros().value(name);
        return macro && macro->isWritable();
    }

    static QString shortcutFor(const QString &name)
    {
        const Id id = Id(Constants::PREFIX_MACRO).withSuffix(name);
        Core::Command *command = Core::ActionManager::command(id);
        if (!command || !command->action())
            return {};
        return command->action()->shortcut().toString(QKeySequence::NativeText);
    }

    QStringList m_names;
    MacroDescriptions m_descriptions;
};

MacrosAspect::MacrosAspect(AspectContainer *container)
    : TypedAspect<MacroDescriptions>(container)
    // Parented: a model handed to QML from a Q_INVOKABLE with no parent is one
    // QML takes ownership of and deletes.
    , m_model(new MacroTableModel(this))
{
    setQmlName("Macros");
    connect(m_model, &QAbstractItemModel::dataChanged, this, [this] { takeFromModel(); });
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, [this] { takeFromModel(); });
}

AspectPresentation MacrosAspect::presentation() const
{
    AspectPresentation p = TypedAspect<MacroDescriptions>::presentation();
    p.control = AspectControls::Table;
    // A macro is recorded, not added here; removing one is what the page
    // offered besides renaming it.
    p.allowAdding = false;
    p.allowRemoving = true;
    return p;
}

QAbstractItemModel *MacrosAspect::tableModel()
{
    return m_model;
}

void MacrosAspect::volatileValueToGui()
{
    if (m_takingFromModel)
        return;
    m_model->setDescriptions(m_volatileValue);
}

void MacrosAspect::takeFromModel()
{
    m_takingFromModel = true;
    setVolatileValue(m_model->descriptions());
    m_takingFromModel = false;
}

// What the Macros page edits. The macros themselves live in MacroManager, so
// the aspect is a view of them: read when the page is built, and the removals
// and renames worked out and applied on apply().
class MacroSettings final : public AspectContainer
{
public:
    MacroSettings()
    {
        setAutoApply(false);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Macros/MacroSettingsPage.qml"));

        m_macros.setLabelText(Tr::tr("Macros:"));
        readFromManager();

        connect(MacroManager::instance(), &MacroManager::macroAdded,
                this, &MacroSettings::readFromManager);
    }

    void apply() override
    {
        AspectContainer::apply();

        const MacroDescriptions wanted = m_macros();
        for (Macro *macro : ownMacros()) {
            const QString name = macro->displayName();
            if (!wanted.contains(name))
                MacroManager::instance()->deleteMacro(name);
            else if (wanted.value(name) != macro->description())
                MacroManager::instance()->changeMacro(name, wanted.value(name));
        }

        readFromManager();
    }

    void cancel() override
    {
        AspectContainer::cancel();
        readFromManager();
    }

private:
    void readFromManager() { m_macros.setValue(descriptionsFromManager()); }

    MacrosAspect m_macros{this};
};

// MacroOptionsPage

MacroOptionsPage::MacroOptionsPage()
{
    setId(Constants::M_OPTIONS_PAGE);
    setDisplayName(Tr::tr("Macros"));
    setCategory(TextEditor::Constants::TEXT_EDITOR_SETTINGS_CATEGORY);
    setSettingsProvider([] {
        static MacroSettings theSettings;
        return &theSettings;
    });
}

} // Macros::Internal
