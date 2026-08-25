// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "mimetypesettings.h"

#include "coreconstants.h"
#include "coreplugintr.h"
#include "dialogs/ioptionspage.h"
#include "editormanager/ieditorfactory.h"
#include "editormanager/ieditorfactory_p.h"
#include "icore.h"

#include <utils/algorithm.h>

#ifdef WITH_TESTS
#include <QTest>
#endif
#include <utils/shutdownguard.h>
#include <utils/fancylineedit.h>
#include <utils/guiutils.h>
#include <utils/layoutbuilder.h>
#include <utils/mimeutils.h>
#include <utils/patternvalidator.h>
#include <utils/qtcassert.h>
#include <utils/stringutils.h>
#include <utils/widgets.h>

#include <QAbstractTableModel>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QGroupBox>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScopedPointer>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QTreeWidget>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

const char kModifiedMimeTypesFile[] = "mimetypes/modifiedmimetypes.xml";

const char mimeInfoTagC[] = "mime-info";
const char mimeTypeTagC[] = "mime-type";
const char mimeTypeAttributeC[] = "type";
const char patternAttributeC[] = "pattern";
const char matchTagC[] = "match";
const char matchValueAttributeC[] = "value";
const char matchTypeAttributeC[] = "type";
const char matchOffsetAttributeC[] = "offset";
const char priorityAttributeC[] = "priority";
const char matchMaskAttributeC[] = "mask";

using namespace Utils;

namespace Core::Internal {

// A magic rule and the priority it sits at. It lived in the header of the
// dialog that edited one; the rules are edited in place now, so this is all
// that is left of it.
class MagicData
{
public:
    MagicData()
        : m_rule(MimeMagicRule::String, QByteArray(" "), 0, 0)
    {}

    MagicData(const MimeMagicRule &rule, int priority)
        : m_rule(rule)
        , m_priority(priority)
    {}

    // The mask a rule was made with, rather than the one it ended up holding:
    // a string rule with no mask of its own is filled with 0xff, which is not
    // something to show or to write out again.
    static QByteArray normalizedMask(const MimeMagicRule &rule)
    {
        QByteArray mask = rule.mask();
        if (rule.type() == MimeMagicRule::String) {
            const QByteArray actual = QByteArray::fromHex(
                QByteArray::fromRawData(mask.constData() + 2, mask.size() - 2));
            if (actual.count(char(-1)) == actual.size())
                mask.clear();
        }
        return mask;
    }

    MimeMagicRule m_rule;
    int m_priority = 0;
};

class UserMimeType
{
public:
    bool isValid() const { return !name.isEmpty(); }
    QString name;
    QStringList globPatterns;
    QMap<int, QList<Utils::MimeMagicRule> > rules;
};

// MimeTypeSettingsModel
class MimeTypeSettingsModel : public QAbstractTableModel
{
public:
    enum class Role { DefaultHandler = Qt::UserRole, MimeType };

    MimeTypeSettingsModel() = default;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    QVariant data(const QModelIndex &modelIndex, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) final;
    Qt::ItemFlags flags(const QModelIndex &index) const final;
    QHash<int, QByteArray> roleNames() const override;

    void load();

    QList<IEditorFactory *> handlersForMimeType(const Utils::MimeType &mimeType) const;
    IEditorFactory *defaultHandlerForMimeType(const Utils::MimeType &mimeType) const;
    void resetUserDefaults();

    QList<Utils::MimeType> m_mimeTypes;
    mutable QHash<Utils::MimeType, QList<IEditorFactory *>> m_handlersByMimeType;
    QHash<QString, IEditorFactory *> m_userDefault;
};

int MimeTypeSettingsModel::rowCount(const QModelIndex &) const
{
    return m_mimeTypes.size();
}

int MimeTypeSettingsModel::columnCount(const QModelIndex &) const
{
    return 2;
}

QVariant MimeTypeSettingsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return QVariant();

    if (section == 0)
        return Tr::tr("MIME Type");
    else
        return Tr::tr("Handler");
}

QVariant MimeTypeSettingsModel::data(const QModelIndex &modelIndex, int role) const
{
    if (!modelIndex.isValid())
        return QVariant();

    const int column = modelIndex.column();
    if (role == Qt::DisplayRole) {
        const Utils::MimeType &type = m_mimeTypes.at(modelIndex.row());
        if (column == 0) {
            return type.name();
        } else {
            IEditorFactory *defaultHandler = defaultHandlerForMimeType(type);
            return defaultHandler ? defaultHandler->displayName() : QString();
        }
    } else if (role == Qt::EditRole) {
        return QVariant::fromValue(handlersForMimeType(m_mimeTypes.at(modelIndex.row())));
    } else if (role == int(Role::DefaultHandler)) {
        return QVariant::fromValue(defaultHandlerForMimeType(m_mimeTypes.at(modelIndex.row())));
    } else if (role == Qt::FontRole) {
        if (column == 1) {
            const Utils::MimeType &type = m_mimeTypes.at(modelIndex.row());
            if (m_userDefault.contains(type.name())) {
                QFont font = QGuiApplication::font();
                font.setItalic(true);
                return font;
            }
        }
        return QVariant();
    } else if (role == int(Role::MimeType)) {
        return QVariant::fromValue(m_mimeTypes.at(modelIndex.row()));
    } else if (role == AspectTable::ChoicesRole) {
        // Which handler a MIME type opens with, by the factory's id: a display
        // name is what a view shows, not what it writes back.
        if (column != 1)
            return {};
        QVariantList choices;
        for (IEditorFactory *factory : handlersForMimeType(m_mimeTypes.at(modelIndex.row()))) {
            choices.append(QVariantMap{{"display", factory->displayName()},
                                       {"id", factory->id().toSetting()}});
        }
        return choices.size() > 1 ? choices : QVariantList();
    } else if (role == AspectTable::EditableRole) {
        return AspectTable::isWritable(flags(modelIndex));
    } else if (role == AspectTable::FilterTextRole) {
        // A MIME type is looked for by what it matches as much as by its name.
        return m_mimeTypes.at(modelIndex.row()).globPatterns().join(' ');
    }
    return QVariant();
}

QHash<int, QByteArray> MimeTypeSettingsModel::roleNames() const
{
    return AspectTable::withRoleNames(QAbstractTableModel::roleNames());
}

bool MimeTypeSettingsModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (index.column() != 1)
        return false;
    IEditorFactory *factory = nullptr;
    if (role == int(Role::DefaultHandler)) {
        factory = value.value<IEditorFactory *>();
    } else if (role == Qt::EditRole) {
        // What a view writes back is the id it was offered, not a pointer.
        const Id id = Id::fromSetting(value);
        factory = Utils::findOrDefault(handlersForMimeType(m_mimeTypes.at(index.row())),
                                       [id](IEditorFactory *f) { return f->id() == id; });
    } else {
        return false;
    }
    QTC_ASSERT(factory, return false);
    const int row = index.row();
    QTC_ASSERT(row >= 0 && row < m_mimeTypes.size(), return false);
    const Utils::MimeType mimeType = m_mimeTypes.at(row);
    const QList<IEditorFactory *> handlers = handlersForMimeType(mimeType);
    QTC_ASSERT(handlers.contains(factory), return false);
    if (handlers.first() == factory) // selection is the default anyhow
        m_userDefault.remove(mimeType.name());
    else
        m_userDefault.insert(mimeType.name(), factory);
    emit dataChanged(index, index);
    return true;
}

Qt::ItemFlags MimeTypeSettingsModel::flags(const QModelIndex &index) const
{
    if (index.column() == 0 || handlersForMimeType(m_mimeTypes.at(index.row())).size() < 2)
        return QAbstractTableModel::flags(index);
    return QAbstractTableModel::flags(index) | Qt::ItemIsEditable;
}

void MimeTypeSettingsModel::load()
{
    beginResetModel();
    m_userDefault = Core::Internal::userPreferredEditorTypes();
    m_mimeTypes = Utils::sorted(Utils::allMimeTypes(),
                                [](const Utils::MimeType &a, const Utils::MimeType &b) {
        return a.name().compare(b.name(), Qt::CaseInsensitive) < 0;
    });
    m_handlersByMimeType.clear();
    endResetModel();
}

QList<IEditorFactory *> MimeTypeSettingsModel::handlersForMimeType(const Utils::MimeType &mimeType) const
{
    if (!m_handlersByMimeType.contains(mimeType))
        m_handlersByMimeType.insert(mimeType, IEditorFactory::defaultEditorFactories(mimeType));
    return m_handlersByMimeType.value(mimeType);
}

IEditorFactory *MimeTypeSettingsModel::defaultHandlerForMimeType(const Utils::MimeType &mimeType) const
{
    if (m_userDefault.contains(mimeType.name()))
        return m_userDefault.value(mimeType.name());
    const QList<IEditorFactory *> handlers = handlersForMimeType(mimeType);
    return handlers.isEmpty() ? nullptr : handlers.first();
}

void MimeTypeSettingsModel::resetUserDefaults()
{
    beginResetModel();
    m_userDefault.clear();
    endResetModel();
    markSettingsDirty();
}

// MimeTypeSettings

const QChar kSemiColon(QLatin1Char(';'));

class MimeTypeSettingsPage final : public IOptionsPage
{
public:
    MimeTypeSettingsPage();

    void writeUserModifiedMimeTypes();

public:
    using UserMimeTypeHash = QHash<QString, UserMimeType>; // name -> mime type
    UserMimeTypeHash readUserModifiedMimeTypes();
    void applyUserModifiedMimeTypes(const UserMimeTypeHash &mimeTypes);

    UserMimeTypeHash m_userModifiedMimeTypes; // these are already in mime database
    MimeTypeSettingsModel m_model;
    UserMimeTypeHash m_pendingModifiedMimeTypes; // currently edited in the options page
};

// MagicHeadersModel

// The magic rules of the MIME type being looked at, as rows. They were a
// QTreeWidget carrying a MagicData on each item and a modal dialog to change
// one, so what the page held could only be read back out of widgets - and the
// mask, which the dialog could set and the tree never showed, was invisible.
class MagicHeadersModel final : public QAbstractTableModel
{
public:
    using QAbstractTableModel::QAbstractTableModel;

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : int(m_rules.size());
    }

    int columnCount(const QModelIndex &parent = {}) const override
    {
        Q_UNUSED(parent)
        return 5;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
            return {};
        switch (section) {
        case 0: return Tr::tr("Magic Header");
        case 1: return Tr::tr("Type");
        case 2: return Tr::tr("Mask");
        case 3: return Tr::tr("Range");
        case 4: return Tr::tr("Priority");
        }
        return {};
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() >= m_rules.size())
            return {};
        const MagicData &data = m_rules.at(index.row());
        if (role == Qt::DisplayRole || role == Qt::EditRole) {
            switch (index.column()) {
            case 0: return QString::fromUtf8(data.m_rule.value());
            case 1: return QString::fromLatin1(MimeMagicRule::typeName(data.m_rule.type()));
            case 2: return QString::fromLatin1(MagicData::normalizedMask(data.m_rule));
            case 3: return QString("%1:%2").arg(data.m_rule.startPos()).arg(data.m_rule.endPos());
            case 4: return QString::number(data.m_priority);
            }
            return {};
        }
        if (role == AspectTable::ChoicesRole) {
            if (index.column() != 1)
                return {};
            QVariantList choices;
            for (const QByteArray &name : typeNames()) {
                const QString display = QString::fromLatin1(name);
                choices.append(QVariantMap{{"display", display}, {"id", display}});
            }
            return choices;
        }
        if (role == AspectTable::EditableRole)
            return true;
        if (role == AspectTable::ValidatorRole) {
            switch (index.column()) {
            case 3: return QString(R"(\d+:\d+)");
            case 4: return QString(R"(\d+)");
            }
            return {};
        }
        if (role == Qt::ToolTipRole && index.column() == 2)
            return Tr::tr("A hexadecimal mask, applied to the value before matching.");
        return {};
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override
    {
        if (role != Qt::EditRole || !index.isValid() || index.row() >= m_rules.size())
            return false;
        MagicData data = m_rules.at(index.row());
        const QString text = value.toString();

        if (index.column() == 4) {
            data.m_priority = text.toInt();
        } else {
            QByteArray ruleValue = data.m_rule.value();
            MimeMagicRule::Type type = data.m_rule.type();
            QByteArray mask = MagicData::normalizedMask(data.m_rule);
            int start = data.m_rule.startPos();
            int end = data.m_rule.endPos();
            switch (index.column()) {
            case 0: ruleValue = text.toUtf8(); break;
            case 1: type = MimeMagicRule::type(text.toLatin1()); break;
            case 2: mask = text.toLatin1(); break;
            case 3: {
                const QStringList range = text.split(':');
                if (range.size() != 2)
                    return false;
                start = range.at(0).toInt();
                end = range.at(1).toInt();
                break;
            }
            }
            // A rule that cannot be made is not stored. The dialog said why;
            // in place, the cell simply keeps what it had.
            QString errorMessage;
            const MimeMagicRule rule(type, ruleValue, start, end, mask, &errorMessage);
            if (!rule.isValid())
                return false;
            data.m_rule = rule;
        }

        m_rules[index.row()] = data;
        emit dataChanged(index, index);
        return true;
    }

    bool insertRows(int row, int count, const QModelIndex &parent = {}) override
    {
        if (parent.isValid() || count < 1)
            return false;
        beginInsertRows({}, row, row + count - 1);
        for (int i = 0; i < count; ++i) {
            // What the dialog offered as its recommended values.
            m_rules.insert(row, MagicData(MimeMagicRule(MimeMagicRule::String, " ", 0, 0), 50));
        }
        endInsertRows();
        return true;
    }

    bool removeRows(int row, int count, const QModelIndex &parent = {}) override
    {
        if (parent.isValid() || row < 0 || row + count > m_rules.size())
            return false;
        beginRemoveRows({}, row, row + count - 1);
        m_rules.remove(row, count);
        endRemoveRows();
        return true;
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return AspectTable::withRoleNames(QAbstractTableModel::roleNames());
    }

    void setRules(const QMap<int, QList<MimeMagicRule>> &rules)
    {
        beginResetModel();
        m_rules.clear();
        for (auto it = rules.constBegin(); it != rules.constEnd(); ++it) {
            for (const MimeMagicRule &rule : it.value())
                m_rules.append(MagicData(rule, it.key()));
        }
        endResetModel();
    }

    QMap<int, QList<MimeMagicRule>> rules() const
    {
        QMap<int, QList<MimeMagicRule>> result;
        for (const MagicData &data : m_rules)
            result[data.m_priority].append(data.m_rule);
        return result;
    }

private:
    static QList<QByteArray> typeNames()
    {
        static const QList<QByteArray> names{
            MimeMagicRule::typeName(MimeMagicRule::String),
            MimeMagicRule::typeName(MimeMagicRule::Host16),
            MimeMagicRule::typeName(MimeMagicRule::Host32),
            MimeMagicRule::typeName(MimeMagicRule::Big16),
            MimeMagicRule::typeName(MimeMagicRule::Big32),
            MimeMagicRule::typeName(MimeMagicRule::Little16),
            MimeMagicRule::typeName(MimeMagicRule::Little32),
            MimeMagicRule::typeName(MimeMagicRule::Byte),
        };
        return names;
    }

    QList<MagicData> m_rules;
};

// MimeTypesAspects

// The MIME types themselves, and which handler each opens with. The model is
// the page's, so it outlives the form.
class MimeTypesAspect final : public BaseAspect
{
    Q_OBJECT

public:
    MimeTypesAspect(AspectContainer *container, MimeTypeSettingsModel *model)
        : BaseAspect(container)
        , m_model(model)
    {}

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Table;
        p.filterPlaceholderText = Tr::tr("Filter");
        // The list is what is installed; nothing is added to it or taken away.
        p.allowAdding = false;
        p.allowRemoving = false;
        return p;
    }

    QAbstractItemModel *tableModel() override { return m_model; }

    // Which one the details below are about. The view says so; the page reads
    // it. -1 when nothing is picked.
    Q_INVOKABLE void setCurrentRow(int row)
    {
        if (row == m_currentRow)
            return;
        m_currentRow = row;
        emit currentRowChanged(row);
    }

    int currentRow() const { return m_currentRow; }

signals:
    void currentRowChanged(int row);

private:
    MimeTypeSettingsModel *m_model = nullptr;
    int m_currentRow = -1;
};

// The magic rules of whichever MIME type is being looked at.
class MagicHeadersAspect final : public BaseAspect
{
public:
    using BaseAspect::BaseAspect;

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Table;
        return p;
    }

    QAbstractItemModel *tableModel() override { return &m_model; }
    MagicHeadersModel &magicModel() { return m_model; }

private:
    // Parented: a model handed to QML from an invokable with no parent belongs
    // to the engine, and its collector frees it.
    MagicHeadersModel m_model{this};
};

// What the MIME Types page edits. The types themselves live in the MIME
// database and the user's changes in a file beside the settings, so these
// aspects have no settings keys of their own.
class MimeTypesAspects final : public AspectContainer
{
    // First, because the aspects below are built from it: members are set up
    // in the order they are declared, whatever the initialiser list says.
    MimeTypeSettingsPage *m_page = nullptr;

public:
    explicit MimeTypesAspects(MimeTypeSettingsPage *page)
        : m_page(page)
    {
        setAutoApply(false);
        setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Core/MimeTypesPage.qml"));

        types.setQmlName("Types");

        resetMimeTypes.setQmlName("ResetMimeTypes");
        resetMimeTypes.setActionText(Tr::tr("Reset MIME Types"));
        resetMimeTypes.setToolTip(Tr::tr("Reset all MIME type definitions to their defaults."));
        resetMimeTypes.setAction([this] {
            m_page->m_pendingModifiedMimeTypes.clear();
            // The settings file goes with the next settings save.
            m_page->m_userModifiedMimeTypes.clear();
            QMessageBox::information(ICore::dialogParent(),
                                     Tr::tr("Reset MIME Types"),
                                     Tr::tr("Changes will take effect after restart."));
            showMimeType(types.currentRow());
        });

        resetHandlers.setQmlName("ResetHandlers");
        resetHandlers.setActionText(Tr::tr("Reset Handlers"));
        resetHandlers.setToolTip(Tr::tr("Reset the assigned handler for all MIME type "
                                        "definitions to the default."));
        resetHandlers.setAction([this] { m_page->m_model.resetUserDefaults(); });

        details.setQmlName("Details");

        patterns.setQmlName("Patterns");
        patterns.setLabelText(Tr::tr("Patterns:"));
        patterns.setDisplayStyle(StringAspect::LineEditDisplay);
        patterns.setToolTip(Tr::tr("A semicolon-separated list of wildcarded file names."));

        magic.setQmlName("Magic");
        magic.setLabelText(Tr::tr("Magic headers:"));

        // Behaviour, not layout.
        connect(&types, &MimeTypesAspect::currentRowChanged,
                this, [this](int row) { showMimeType(row); });
        patterns.addOnVolatileValueChanged(this, [this] { storePatterns(); });
        connect(&magic.magicModel(), &QAbstractItemModel::dataChanged,
                this, [this] { storeMagicRules(); });
        connect(&magic.magicModel(), &QAbstractItemModel::rowsInserted,
                this, [this] { storeMagicRules(); });
        connect(&magic.magicModel(), &QAbstractItemModel::rowsRemoved,
                this, [this] { storeMagicRules(); });
        showMimeType(-1);
    }

    void apply() override
    {
        AspectContainer::apply();
        m_page->applyUserModifiedMimeTypes(m_page->m_pendingModifiedMimeTypes);
        setUserPreferredEditorTypes(m_page->m_model.m_userDefault);
        m_page->m_pendingModifiedMimeTypes.clear();
        m_page->m_model.load();
        m_page->writeUserModifiedMimeTypes();
    }

    void cancel() override
    {
        AspectContainer::cancel();
        m_page->m_pendingModifiedMimeTypes.clear();
        showMimeType(types.currentRow());
    }

    MimeTypesAspect types{this, &m_page->m_model};
    ActionAspect resetMimeTypes{this};
    ActionAspect resetHandlers{this};
    AspectContainer details{this};
    StringAspect patterns{&details};
    MagicHeadersAspect magic{&details};

private:
    MimeType currentMimeType() const
    {
        const int row = types.currentRow();
        if (row < 0 || row >= m_page->m_model.m_mimeTypes.size())
            return {};
        return m_page->m_model.m_mimeTypes.at(row);
    }

    void showMimeType(int row)
    {
        const MimeType type = row >= 0 && row < m_page->m_model.m_mimeTypes.size()
                                  ? m_page->m_model.m_mimeTypes.at(row)
                                  : MimeType();
        details.setVisible(type.isValid());
        // Nothing is current while the form is being filled in, so nothing the
        // fields say on the way is written back.
        m_loadedName.clear();
        if (!type.isValid()) {
            patterns.setValue(QString());
            magic.magicModel().setRules({});
            return;
        }
        const UserMimeType modified = m_page->m_pendingModifiedMimeTypes.value(type.name());
        patterns.setValue(modified.isValid() ? modified.globPatterns.join(kSemiColon)
                                             : type.globPatterns().join(kSemiColon));
        magic.magicModel().setRules(modified.isValid() ? modified.rules
                                                       : magicRulesForMimeType(type));
        m_loadedName = type.name();
    }

    // A MIME type is copied into the pending set the first time it is touched,
    // and everything after that edits the copy.
    UserMimeType &pending()
    {
        const MimeType type = currentMimeType();
        if (!m_page->m_pendingModifiedMimeTypes.contains(type.name())) {
            UserMimeType copy;
            copy.name = type.name();
            copy.globPatterns = type.globPatterns();
            copy.rules = magicRulesForMimeType(type);
            m_page->m_pendingModifiedMimeTypes.insert(copy.name, copy);
        }
        return m_page->m_pendingModifiedMimeTypes[type.name()];
    }

    void storePatterns()
    {
        if (m_loadedName.isEmpty())
            return;
        pending().globPatterns = patterns.volatileValue().split(kSemiColon, Qt::SkipEmptyParts);
    }

    void storeMagicRules()
    {
        if (m_loadedName.isEmpty())
            return;
        pending().rules = magic.magicModel().rules();
    }

    // The MIME type the form is showing, and nothing while it is being filled
    // in.
    QString m_loadedName;
};

void MimeTypeSettingsPage::writeUserModifiedMimeTypes()
{
    static FilePath modifiedMimeTypesFile = ICore::userResourcePath(kModifiedMimeTypesFile);

    if (modifiedMimeTypesFile.parentDir().ensureWritableDir()) {
        QFile file(modifiedMimeTypesFile.toFSPathString());
        if (file.open(QFile::WriteOnly | QFile::Truncate)) {
            // Notice this file only represents user modifications. It is writen in a
            // convienient way for synchronization, which is similar to but not exactly the
            // same format we use for the embedded mime type files.
            QXmlStreamWriter writer(&file);
            writer.setAutoFormatting(true);
            writer.writeStartDocument();
            writer.writeStartElement(QLatin1String(mimeInfoTagC));

            for (const UserMimeType &mt : std::as_const(m_userModifiedMimeTypes)) {
                writer.writeStartElement(QLatin1String(mimeTypeTagC));
                writer.writeAttribute(QLatin1String(mimeTypeAttributeC), mt.name);
                writer.writeAttribute(QLatin1String(patternAttributeC),
                                      mt.globPatterns.join(kSemiColon));
                for (auto prioIt = mt.rules.constBegin(); prioIt != mt.rules.constEnd(); ++prioIt) {
                    const QString priorityString = QString::number(prioIt.key());
                    for (const Utils::MimeMagicRule &rule : prioIt.value()) {
                        writer.writeStartElement(QLatin1String(matchTagC));
                        writer.writeAttribute(QLatin1String(matchValueAttributeC),
                                              QString::fromUtf8(rule.value()));
                        writer.writeAttribute(QLatin1String(matchTypeAttributeC),
                                              QString::fromUtf8(Utils::MimeMagicRule::typeName(rule.type())));
                        writer.writeAttribute(QLatin1String(matchOffsetAttributeC),
                                              QString::fromLatin1("%1:%2").arg(rule.startPos())
                                              .arg(rule.endPos()));
                        writer.writeAttribute(QLatin1String(priorityAttributeC),
                                              priorityString);
                        writer.writeAttribute(QLatin1String(matchMaskAttributeC),
                                              QString::fromLatin1(MagicData::normalizedMask(rule)));
                        writer.writeEndElement();
                    }
                }
                writer.writeEndElement();
            }
            writer.writeEndElement();
            writer.writeEndDocument();
            file.close();
        }
    }
}

static QPair<int, int> rangeFromString(const QString &offset)
{
    const QStringList list = offset.split(QLatin1Char(':'));
    QPair<int, int> range;
    QTC_ASSERT(list.size() > 0, return range);
    range.first = list.at(0).toInt();
    if (list.size() > 1)
        range.second = list.at(1).toInt();
    else
        range.second = range.first;
    return range;
}

MimeTypeSettingsPage::UserMimeTypeHash MimeTypeSettingsPage::readUserModifiedMimeTypes()
{
    static Utils::FilePath modifiedMimeTypesPath = ICore::userResourcePath(kModifiedMimeTypesFile);
    UserMimeTypeHash userMimeTypes;
    QFile file(modifiedMimeTypesPath.toFSPathString());
    if (file.open(QFile::ReadOnly)) {
        UserMimeType mt;
        QXmlStreamReader reader(&file);
        QXmlStreamAttributes atts;
        while (!reader.atEnd()) {
            switch (reader.readNext()) {
            case QXmlStreamReader::StartElement:
                atts = reader.attributes();
                if (reader.name() == QLatin1String(mimeTypeTagC)) {
                    mt.name = atts.value(QLatin1String(mimeTypeAttributeC)).toString();
                    mt.globPatterns = atts.value(QLatin1String(patternAttributeC)).toString()
                            .split(kSemiColon, Qt::SkipEmptyParts);
                } else if (reader.name() == QLatin1String(matchTagC)) {
                    QByteArray value = atts.value(QLatin1String(matchValueAttributeC)).toUtf8();
                    QByteArray typeName = atts.value(QLatin1String(matchTypeAttributeC)).toUtf8();
                    const QString rangeString = atts.value(QLatin1String(matchOffsetAttributeC)).toString();
                    QPair<int, int> range = rangeFromString(rangeString);
                    int priority = atts.value(QLatin1String(priorityAttributeC)).toString().toInt();
                    QByteArray mask = atts.value(QLatin1String(matchMaskAttributeC)).toLatin1();
                    QString errorMessage;
                    Utils::MimeMagicRule rule(Utils::MimeMagicRule::type(typeName),
                                                        value, range.first, range.second, mask,
                                                        &errorMessage);
                    if (rule.isValid()) {
                        mt.rules[priority].append(rule);
                    } else {
                        qWarning("Error reading magic rule in custom mime type %s: %s",
                                 qPrintable(mt.name), qPrintable(errorMessage));
                    }
                }
                break;
            case QXmlStreamReader::EndElement:
                if (reader.name() == QLatin1String(mimeTypeTagC)) {
                    userMimeTypes.insert(mt.name, mt);
                    mt.name.clear();
                    mt.globPatterns.clear();
                    mt.rules.clear();
                }
                break;
            default:
                break;
            }
        }
        if (reader.hasError())
            qWarning() << modifiedMimeTypesPath << reader.errorString() << reader.lineNumber()
                       << reader.columnNumber();
        file.close();
    }
    return userMimeTypes;
}

static void registerUserModifiedMimeTypes(const MimeTypeSettingsPage::UserMimeTypeHash &mimeTypes)
{
    for (auto it = mimeTypes.constBegin(); it != mimeTypes.constEnd(); ++it) {
        Utils::MimeType mt = Utils::mimeTypeForName(it.key());
        if (!mt.isValid())
            continue;
        Utils::setGlobPatternsForMimeType(mt, it.value().globPatterns);
        Utils::setMagicRulesForMimeType(mt, it.value().rules);
    }
}

void MimeTypeSettingsPage::applyUserModifiedMimeTypes(const UserMimeTypeHash &mimeTypes)
{
    // register in mime data base, and remember for later
    for (auto it = mimeTypes.constBegin(); it != mimeTypes.constEnd(); ++it)
        m_userModifiedMimeTypes.insert(it.key(), it.value());
    registerUserModifiedMimeTypes(mimeTypes);
}

MimeTypeSettingsPage::MimeTypeSettingsPage()
{
    setId(Constants::SETTINGS_ID_MIMETYPES);
    setDisplayName(Tr::tr("MIME Types"));
    setCategory(Constants::SETTINGS_CATEGORY_CORE);
    setSettingsProvider([this] {
        static GuardedObject<MimeTypesAspects> theAspects(this);
        return theAspects.get();
    });
    setFixedKeywords({
        Tr::tr("Reset MIME Types"),
        Tr::tr("Reset Handlers"),
        Tr::tr("Registered MIME Types"),
        Tr::tr("Patterns:"),
        Tr::tr("Add..."),
        Tr::tr("Edit..."),
        Tr::tr("Remove"),
        Tr::tr("Details")
    });

    m_userModifiedMimeTypes = readUserModifiedMimeTypes();
    Utils::addMimeInitializer([this] { registerUserModifiedMimeTypes(m_userModifiedMimeTypes); });
}

#ifdef WITH_TESTS

// The page's MIME types lived in a QTreeView with a widget item delegate, and
// its magic rules on QTreeWidgetItems edited through a modal dialog - which
// could set a mask the tree never showed. None of it could be read back
// without opening the page.

class MimeTypeSettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testTheHandlerColumnOffersTheHandlersItHas();
    void testAMimeTypeIsFoundByWhatItMatches();
    void testNoDetailsAreShownUntilATypeIsPicked();
    void testEditingThePatternsRecordsTheChange();
    void testAMagicRuleCanBeAddedAndTakenAway();
    void testAMaskSurvivesEditingTheRuleAroundIt();
    void testARuleThatCannotBeMadeIsRefused();

private:
    MimeTypeSettingsPage *m_page = nullptr;

    int rowOf(const QString &name) const
    {
        for (int row = 0; row < m_page->m_model.m_mimeTypes.size(); ++row) {
            if (m_page->m_model.m_mimeTypes.at(row).name() == name)
                return row;
        }
        return -1;
    }

    // A MIME type that is installed everywhere and has more than one pattern.
    int rowOfSomethingWithPatterns() const
    {
        for (int row = 0; row < m_page->m_model.m_mimeTypes.size(); ++row) {
            if (!m_page->m_model.m_mimeTypes.at(row).globPatterns().isEmpty())
                return row;
        }
        return -1;
    }
};

void MimeTypeSettingsTest::init()
{
    m_page = new MimeTypeSettingsPage;
    m_page->m_model.load();
}

void MimeTypeSettingsTest::cleanup()
{
    delete m_page;
    m_page = nullptr;
}

void MimeTypeSettingsTest::testTheHandlerColumnOffersTheHandlersItHas()
{
    QAbstractItemModel *model = &m_page->m_model;
    QVERIFY(model->rowCount({}) > 0);
    QCOMPARE(model->columnCount({}), 2);

    // The MIME type itself is not the user's to change; only which handler
    // opens it, and only where there is more than one.
    for (int row = 0; row < model->rowCount({}); ++row) {
        QVERIFY(!model->index(row, 0).data(AspectTable::EditableRole).toBool());
        const QVariantList choices
            = model->index(row, 1).data(AspectTable::ChoicesRole).toList();
        const bool editable = model->index(row, 1).data(AspectTable::EditableRole).toBool();
        QCOMPARE(editable, choices.size() > 1);
        if (!choices.isEmpty()) {
            // A view writes back the id it was offered, not the display text.
            QVERIFY(choices.first().toMap().contains("id"));
            QVERIFY(!choices.first().toMap().value("display").toString().isEmpty());
        }
    }
}

void MimeTypeSettingsTest::testAMimeTypeIsFoundByWhatItMatches()
{
    const int row = rowOfSomethingWithPatterns();
    QVERIFY(row >= 0);
    const MimeType type = m_page->m_model.m_mimeTypes.at(row);

    // Typing "*.cpp" into the filter has to find the type that matches it, and
    // the patterns are not a column - so the model says what else a row is
    // found by. See Utils::AspectTable::FilterTextRole.
    const QString found
        = m_page->m_model.index(row, 0).data(AspectTable::FilterTextRole).toString();
    for (const QString &pattern : type.globPatterns())
        QVERIFY2(found.contains(pattern), qPrintable(type.name() + ": " + found));
}

void MimeTypeSettingsTest::testNoDetailsAreShownUntilATypeIsPicked()
{
    MimeTypesAspects page(m_page);
    QVERIFY(!page.details.isVisible());
    QVERIFY(page.patterns.volatileValue().isEmpty());
    QCOMPARE(page.magic.tableModel()->rowCount({}), 0);

    const int row = rowOfSomethingWithPatterns();
    QVERIFY(row >= 0);
    page.types.setCurrentRow(row);
    QVERIFY(page.details.isVisible());
    QCOMPARE(page.patterns.volatileValue(),
             m_page->m_model.m_mimeTypes.at(row).globPatterns().join(';'));
}

void MimeTypeSettingsTest::testEditingThePatternsRecordsTheChange()
{
    MimeTypesAspects page(m_page);
    const int row = rowOfSomethingWithPatterns();
    QVERIFY(row >= 0);
    const QString name = m_page->m_model.m_mimeTypes.at(row).name();

    // Nothing is pending until the user touches something.
    page.types.setCurrentRow(row);
    QVERIFY(!m_page->m_pendingModifiedMimeTypes.contains(name));

    page.patterns.setVolatileValue(QString("*.zzz;*.yyy"));
    QVERIFY(m_page->m_pendingModifiedMimeTypes.contains(name));
    QCOMPARE(m_page->m_pendingModifiedMimeTypes.value(name).globPatterns,
             QStringList({"*.zzz", "*.yyy"}));

    // Showing another type must not write the one that was on screen into it.
    const int other = row == 0 ? 1 : 0;
    const QString otherName = m_page->m_model.m_mimeTypes.at(other).name();
    page.types.setCurrentRow(other);
    QVERIFY(!m_page->m_pendingModifiedMimeTypes.contains(otherName));
    QCOMPARE(m_page->m_pendingModifiedMimeTypes.value(name).globPatterns,
             QStringList({"*.zzz", "*.yyy"}));
}

void MimeTypeSettingsTest::testAMagicRuleCanBeAddedAndTakenAway()
{
    MimeTypesAspects page(m_page);
    const int row = rowOfSomethingWithPatterns();
    QVERIFY(row >= 0);
    const QString name = m_page->m_model.m_mimeTypes.at(row).name();
    page.types.setCurrentRow(row);

    QAbstractItemModel *rules = page.magic.tableModel();
    const int before = rules->rowCount({});
    QVERIFY(rules->insertRows(before, 1));
    QCOMPARE(rules->rowCount({}), before + 1);
    QVERIFY(m_page->m_pendingModifiedMimeTypes.contains(name));

    // A new rule starts on what the dialog used to offer as its recommended
    // values, so it is a rule and not a blank row.
    QCOMPARE(rules->index(before, 1).data().toString(), QString("string"));
    QCOMPARE(rules->index(before, 3).data().toString(), QString("0:0"));
    QCOMPARE(rules->index(before, 4).data().toString(), QString("50"));

    QVERIFY(rules->setData(rules->index(before, 0), QString("MAGIC")));
    QCOMPARE(rules->index(before, 0).data().toString(), QString("MAGIC"));
    int stored = 0;
    for (const QList<MimeMagicRule> &list : m_page->m_pendingModifiedMimeTypes.value(name).rules)
        stored += list.size();
    QCOMPARE(stored, before + 1);

    QVERIFY(rules->removeRows(before, 1));
    QCOMPARE(rules->rowCount({}), before);
}

void MimeTypeSettingsTest::testAMaskSurvivesEditingTheRuleAroundIt()
{
    MimeTypesAspects page(m_page);
    const int row = rowOfSomethingWithPatterns();
    QVERIFY(row >= 0);
    page.types.setCurrentRow(row);

    QAbstractItemModel *rules = page.magic.tableModel();
    const int at = rules->rowCount({});
    QVERIFY(rules->insertRows(at, 1));
    QVERIFY(rules->setData(rules->index(at, 0), QString("ab")));
    QVERIFY(rules->setData(rules->index(at, 2), QString("0xff00")));
    QCOMPARE(rules->index(at, 2).data().toString(), QString("0xff00"));

    // The mask is a column of its own, so changing the range beside it leaves
    // it alone. The widget tree never showed it, and a dialog that did not
    // know about it wrote it away.
    QVERIFY(rules->setData(rules->index(at, 3), QString("2:4")));
    QCOMPARE(rules->index(at, 3).data().toString(), QString("2:4"));
    QCOMPARE(rules->index(at, 2).data().toString(), QString("0xff00"));
}

void MimeTypeSettingsTest::testARuleThatCannotBeMadeIsRefused()
{
    MimeTypesAspects page(m_page);
    const int row = rowOfSomethingWithPatterns();
    QVERIFY(row >= 0);
    page.types.setCurrentRow(row);

    QAbstractItemModel *rules = page.magic.tableModel();
    const int at = rules->rowCount({});
    QVERIFY(rules->insertRows(at, 1));
    QVERIFY(rules->setData(rules->index(at, 0), QString("ab")));

    // A byte rule needs a number, and "ab" is not one. The dialog said so and
    // refused to close; the cell keeps what it had.
    QVERIFY(!rules->setData(rules->index(at, 1), QString("byte")));
    QCOMPARE(rules->index(at, 1).data().toString(), QString("string"));
    QCOMPARE(rules->index(at, 0).data().toString(), QString("ab"));

    // A range that is not two numbers is not a range.
    QVERIFY(!rules->setData(rules->index(at, 3), QString("nonsense")));
    QCOMPARE(rules->index(at, 3).data().toString(), QString("0:0"));
}

QObject *createMimeTypeSettingsTest()
{
    return new MimeTypeSettingsTest;
}

#endif // WITH_TESTS

void setupMimeTypeSettings()
{
    static MimeTypeSettingsPage theMimeTypeSettingsPage;
}

} // Core::Internal

#include "mimetypesettings.moc"
