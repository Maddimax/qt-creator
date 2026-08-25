// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "shortcutsettings.h"

#include "ioptionspage.h"
#include "../actionmanager/actionmanager.h"
#include "../actionmanager/command.h"
#include "../coreconstants.h"
#include "../coreplugintr.h"
#include "../documentmanager.h"
#include "../icore.h"

#include <utils/aspectlist.h>
#include <utils/aspectpresentation.h>
#include <utils/filedialogs.h>
#include <utils/algorithm.h>
#include <utils/guiutils.h>
#include <utils/fileutils.h>
#include <utils/hostosinfo.h>
#include <utils/qtcassert.h>
#include <utils/shutdownguard.h>
#include <utils/treemodel.h>
#include <utils/theme/theme.h>

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QScopeGuard>
#include <QSortFilterProxyModel>
#include <QFile>
#include <QKeyEvent>
#include <QKeySequence>
#include <QTimer>
#include <QXmlStreamAttributes>

#ifdef WITH_TESTS
#include <QTest>
#endif
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <array>

using namespace Utils;

namespace Core::Internal {

const char kSeparator[] = " | ";

struct ShortcutItem final
{
    Command *m_cmd = nullptr;
    QList<QKeySequence> m_keys;
};

/*!
    \class Core::Internal::CommandsFile
    \internal
    \inmodule QtCreator
    \brief The CommandsFile class provides a collection of import and export commands.
*/

class CommandsFile final
{
public:
    CommandsFile(const FilePath &filePath) : m_filePath(filePath) {}

    QMap<QString, QList<QKeySequence> > importCommands() const;
    bool exportCommands(const QList<ShortcutItem *> &items);

private:
    const QString mappingElement = "mapping";
    const QString shortCutElement = "shortcut";
    const QString idAttribute = "id";
    const QString keyElement = "key";
    const QString valueAttribute = "value";

    FilePath m_filePath;
};


// XML attributes cannot contain these characters, and
// QXmlStreamWriter just bails out with an error.
// QKeySequence::toString() should probably not result in these
// characters, but it currently does, see QTCREATORBUG-29431
static bool containsInvalidCharacters(const QString &s)
{
    const auto end = s.constEnd();
    for (auto it = s.constBegin(); it != end; ++it) {
        // from QXmlStreamWriterPrivate::writeEscaped
        if (*it == u'\v' || *it == u'\f' || *it <= u'\x1F' || *it >= u'\uFFFE') {
            return true;
        }
    }
    return false;
}

static QString toAttribute(const QString &s)
{
    if (containsInvalidCharacters(s))
        return "0x" + QString::fromUtf8(s.toUtf8().toHex());
    return s;
}

static QString fromAttribute(const QStringView &s)
{
    if (s.startsWith(QLatin1String("0x")))
        return QString::fromUtf8(QByteArray::fromHex(s.sliced(2).toUtf8()));
    return s.toString();
}

/*!
    \internal
*/
QMap<QString, QList<QKeySequence>> CommandsFile::importCommands() const
{
    QMap<QString, QList<QKeySequence>> result;

    QFile file(m_filePath.toFSPathString());
    if (!file.open(QIODevice::ReadOnly|QIODevice::Text))
        return result;

    QXmlStreamReader r(&file);

    QString currentId;

    while (!r.atEnd()) {
        switch (r.readNext()) {
        case QXmlStreamReader::StartElement: {
            const auto name = r.name();
            if (name == shortCutElement) {
                currentId = r.attributes().value(idAttribute).toString();
                if (!result.contains(currentId))
                    result.insert(currentId, {});
            } else if (name == keyElement) {
                QTC_ASSERT(!currentId.isEmpty(), continue);
                const QXmlStreamAttributes attributes = r.attributes();
                if (attributes.hasAttribute(valueAttribute)) {
                    QString keyString = fromAttribute(attributes.value(valueAttribute));
                    if (HostOsInfo::isMacHost())
                        keyString = keyString.replace("AlwaysCtrl", "Meta");
                    else
                        keyString = keyString.replace("AlwaysCtrl", "Ctrl");

                    QList<QKeySequence> keys = result.value(currentId);
                    result.insert(currentId, keys << QKeySequence(keyString));
                }
            } // if key element
        } // case QXmlStreamReader::StartElement
        default:
            break;
        } // switch
    } // while !atEnd
    file.close();
    return result;
}

/*!
    \internal
*/
bool CommandsFile::exportCommands(const QList<ShortcutItem *> &items)
{
    FileSaver saver(m_filePath, QIODevice::Text);
    if (!saver.hasError()) {
        QXmlStreamWriter w(saver.file());
        w.setAutoFormatting(true);
        w.setAutoFormattingIndent(1); // Historical, used to be QDom.
        w.writeStartDocument();
        w.writeDTD(QLatin1String("<!DOCTYPE KeyboardMappingScheme>"));
        w.writeComment(QString::fromLatin1(" Written by %1, %2. ").
                       arg(ICore::versionString(),
                           QDateTime::currentDateTime().toString(Qt::ISODate)));
        w.writeStartElement(mappingElement);
        for (const ShortcutItem *item : std::as_const(items)) {
            const Id id = item->m_cmd->id();
            if (item->m_keys.isEmpty() || item->m_keys.first().isEmpty()) {
                w.writeEmptyElement(shortCutElement);
                w.writeAttribute(idAttribute, id.toString());
            } else {
                w.writeStartElement(shortCutElement);
                w.writeAttribute(idAttribute, id.toString());
                for (const QKeySequence &k : item->m_keys) {
                    w.writeEmptyElement(keyElement);
                    w.writeAttribute(valueAttribute, toAttribute(k.toString()));
                }
                w.writeEndElement(); // Shortcut
            }
        }
        w.writeEndElement();
        w.writeEndDocument();

        if (!saver.setResult(&w))
            qWarning() << saver.errorString();
    }
    return saver.finalize().has_value();
}

static int translateModifiers(Qt::KeyboardModifiers state, const QString &text)
{
    int result = 0;
    // The shift modifier only counts when it is not used to type a symbol
    // that is only reachable using the shift key anyway
    if ((state & Qt::ShiftModifier) && (text.isEmpty()
                                        || !text.at(0).isPrint()
                                        || text.at(0).isLetterOrNumber()
                                        || text.at(0).isSpace()))
        result |= Qt::SHIFT;
    if (state & Qt::ControlModifier)
        result |= Qt::CTRL;
    if (state & Qt::MetaModifier)
        result |= Qt::META;
    if (state & Qt::AltModifier)
        result |= Qt::ALT;
    return result;
}

static QList<QKeySequence> cleanKeys(const QList<QKeySequence> &ks)
{
    return Utils::filtered(ks, [](const QKeySequence &k) { return !k.isEmpty(); });
}

static QString keySequenceToEditString(const QKeySequence &sequence)
{
    QString text = sequence.toString(QKeySequence::PortableText);
    if (Utils::HostOsInfo::isMacHost()) {
        // adapt the modifier names
        text.replace(QLatin1String("Ctrl"), QLatin1String("Cmd"), Qt::CaseInsensitive);
        text.replace(QLatin1String("Alt"), QLatin1String("Opt"), Qt::CaseInsensitive);
        text.replace(QLatin1String("Meta"), QLatin1String("Ctrl"), Qt::CaseInsensitive);
    }
    return text;
}

static QString keySequencesToEditString(const QList<QKeySequence> &sequence)
{
    return Utils::transform(cleanKeys(sequence), keySequenceToEditString).join(kSeparator);
}

static QString keySequencesToNativeString(const QList<QKeySequence> &sequence)
{
    return Utils::transform(cleanKeys(sequence),
                            [](const QKeySequence &k) {
                                return k.toString(QKeySequence::NativeText);
                            })
        .join(kSeparator);
}

static QKeySequence keySequenceFromEditString(const QString &editString)
{
    QString text = editString.trimmed();
    if (Utils::HostOsInfo::isMacHost()) {
        // adapt the modifier names
        text.replace(QLatin1String("Opt"), QLatin1String("Alt"), Qt::CaseInsensitive);
        text.replace(QLatin1String("Ctrl"), QLatin1String("Meta"), Qt::CaseInsensitive);
        text.replace(QLatin1String("Cmd"), QLatin1String("Ctrl"), Qt::CaseInsensitive);
    }
    return QKeySequence::fromString(text, QKeySequence::PortableText);
}

static bool keySequenceIsValid(const QKeySequence &sequence)
{
    if (sequence.isEmpty())
        return false;
    for (int i = 0; i < sequence.count(); ++i) {
        if (sequence[i] == QKeyCombination(Qt::Key_unknown))
            return false;
    }
    return true;
}

static bool isTextKeySequence(const QKeySequence &sequence)
{
    if (sequence.isEmpty())
        return false;
    const QKeyCombination keyCombination = sequence[0];
    if (keyCombination.keyboardModifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier))
        return false;
    return keyCombination.key() < Qt::Key_Escape;
}

static FilePath schemesPath()
{
    return Core::ICore::resourcePath("schemes");
}

static bool checkValidity(const QKeySequence &key, QString *warningMessage)
{
    if (key.isEmpty())
        return true;
    QTC_ASSERT(warningMessage, return true);
    if (!keySequenceIsValid(key)) {
        *warningMessage = Tr::tr("Invalid key sequence.");
        return false;
    }
    if (isTextKeySequence(key))
        *warningMessage = Tr::tr("Key sequence will not work in editor."); // FIXME: return false missing?
    return true;
}

// One key sequence, typed in or recorded. Recording is the aspect's own job
// rather than a control's: what is being recorded is exactly the keys that
// would otherwise be shortcuts, and those never reach the control they were
// meant for - they have to be taken from the application.
class KeySequenceAspect final : public StringAspect
{
    Q_OBJECT

    Q_PROPERTY(bool recording READ isRecording NOTIFY recordingChanged)

public:
    explicit KeySequenceAspect(AspectContainer *container = nullptr)
        : StringAspect(container)
    {
        setDisplayStyle(StringAspect::LineEditDisplay);
        setLabelText(Tr::tr("Key sequence:"));
        setToolTip(
            Utils::HostOsInfo::isMacHost()
                ? QLatin1String("<html><body>")
                      + Tr::tr("Use \"Cmd\", \"Opt\", \"Ctrl\", and \"Shift\" for modifier keys. "
                               "Use \"Escape\", \"Backspace\", \"Delete\", \"Insert\", \"Home\", "
                               "and so on, for special keys. "
                               "Combine individual keys with \"+\", "
                               "and combine multiple shortcuts to a shortcut sequence with \",\". "
                               "For example, if the user must hold the Ctrl and Shift modifier keys "
                               "while pressing Escape, and then release and press A, "
                               "enter \"Ctrl+Shift+Escape,A\".")
                      + QLatin1String("</body></html>")
                : QLatin1String("<html><body>")
                      + Tr::tr("Use \"Ctrl\", \"Alt\", \"Meta\", and \"Shift\" for modifier keys. "
                               "Use \"Escape\", \"Backspace\", \"Delete\", \"Insert\", \"Home\", "
                               "and so on, for special keys. "
                               "Combine individual keys with \"+\", "
                               "and combine multiple shortcuts to a shortcut sequence with \",\". "
                               "For example, if the user must hold the Ctrl and Shift modifier keys "
                               "while pressing Escape, and then release and press A, "
                               "enter \"Ctrl+Shift+Escape,A\".")
                      + QLatin1String("</body></html>"));
    }

    ~KeySequenceAspect() override { setRecording(false); }

    AspectPresentation presentation() const override
    {
        AspectPresentation p = StringAspect::presentation();
        p.control = AspectControls::KeySequence;
        return p;
    }

    QKeySequence keySequence() const
    {
        return keySequenceFromEditString(volatileValue());
    }

    void setKeySequence(const QKeySequence &key)
    {
        setValue(keySequenceToEditString(key));
    }

    bool isRecording() const { return m_recording; }

    Q_INVOKABLE void setRecording(bool recording)
    {
        if (m_recording == recording)
            return;
        m_recording = recording;
        m_keys = {};
        m_keyCount = 0;
        if (m_recording) {
            // Otherwise the field the keys are meant for gets them first.
            if (QWidget *focused = QApplication::focusWidget())
                focused->clearFocus();
            qApp->installEventFilter(this);
        } else {
            qApp->removeEventFilter(this);
        }
        emit recordingChanged();
    }

signals:
    void recordingChanged();

private:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (!m_recording)
            return StringAspect::eventFilter(watched, event);

        switch (event->type()) {
        case QEvent::ShortcutOverride:
            event->accept();
            return true;
        case QEvent::KeyRelease:
        case QEvent::Shortcut:
        // Escape otherwise tries to close the dialog.
        case QEvent::Close:
            return true;
        case QEvent::MouseButtonPress:
            setRecording(false);
            return true;
        case QEvent::KeyPress:
            return recordKey(static_cast<QKeyEvent *>(event));
        default:
            break;
        }
        return StringAspect::eventFilter(watched, event);
    }

    // A sequence is at most four combinations, and a modifier on its own is
    // not one of them.
    bool recordKey(QKeyEvent *event)
    {
        const int key = event->key();
        if (m_keyCount > 3 || key == Qt::Key_Control || key == Qt::Key_Shift
            || key == Qt::Key_Meta || key == Qt::Key_Alt) {
            return false;
        }

        m_keys[m_keyCount++] = key | translateModifiers(event->modifiers(), event->text());
        event->accept();
        setKeySequence(QKeySequence(m_keys[0], m_keys[1], m_keys[2], m_keys[3]));
        if (m_keyCount > 3)
            setRecording(false);
        return true;
    }

    bool m_recording = false;
    std::array<int, 4> m_keys{{0, 0, 0, 0}};
    int m_keyCount = 0;
};

// A section of the command tree - "Core", "Edit" - named after what comes
// before the first dot in a command id.
class SectionItem final : public TreeItem
{
public:
    explicit SectionItem(const QString &name) : m_name(name) {}

    QVariant data(int column, int role) const override
    {
        // The name once, not once per column: a Qt Quick tree draws each cell
        // where it is rather than spanning the heading across the row.
        if (role == Qt::DisplayRole && column == 0)
            return m_name;
        if (role == Qt::FontRole && column == 0) {
            QFont font;
            font.setBold(true);
            return font;
        }
        return {};
    }

private:
    QString m_name;
};

// One command, with whatever it is currently mapped to. What the row says
// about itself - modified, colliding - is here rather than set on it from
// outside, so that a Qt Quick view and a QTreeView show the same thing.
class CommandItem final : public TreeItem
{
public:
    CommandItem(ShortcutItem *shortcut, const QString &subId)
        : m_shortcut(shortcut), m_subId(subId)
    {}

    ShortcutItem *shortcut() const { return m_shortcut; }

    bool isModified() const
    {
        return cleanKeys(m_shortcut->m_keys) != m_shortcut->m_cmd->defaultKeySequences();
    }

    void setColliding(bool colliding)
    {
        if (m_colliding == colliding)
            return;
        m_colliding = colliding;
        update();
    }

    QVariant data(int column, int role) const override
    {
        switch (role) {
        case Qt::DisplayRole:
            switch (column) {
            case 0: return m_subId;
            case 1: return m_shortcut->m_cmd->description();
            case 2: return keySequencesToNativeString(m_shortcut->m_keys);
            }
            return {};
        case Qt::FontRole: {
            QFont font;
            font.setBold(isModified());
            return font;
        }
        case Qt::ForegroundRole:
            if (column == 2 && m_colliding)
                return Utils::creatorColor(Utils::Theme::TextColorError);
            return {};
        case Utils::AspectTable::FilterTextRole:
            // Found by what it is rather than by what it shows: the whole
            // command id, and the shortcut as it is written rather than as it
            // is drawn - which is what "Show conflicts" filters by.
            return QString(m_shortcut->m_cmd->id().toString() + kSeparator
                           + keySequencesToEditString(m_shortcut->m_keys));
        default:
            break;
        }
        return {};
    }

private:
    ShortcutItem *m_shortcut = nullptr;
    QString m_subId;
    bool m_colliding = false;
};

// The commands, as the page lists them.
class CommandTreeAspect final : public BaseAspect
{
    Q_OBJECT

public:
    using BaseAspect::BaseAspect;

    AspectPresentation presentation() const override
    {
        AspectPresentation p = BaseAspect::presentation();
        p.control = AspectControls::Tree;
        p.filterPlaceholderText = Tr::tr("Filter");
        return p;
    }

    QAbstractItemModel *tableModel() override { return &m_model; }
    TreeModel<> &model() { return m_model; }

    // Which command the shortcut box below is about. The view says so; the
    // page reads it. Null on a section heading, or when nothing is picked.
    Q_INVOKABLE void setCurrentIndex(const QModelIndex &index)
    {
        auto item = dynamic_cast<CommandItem *>(m_model.itemForIndex(index));
        if (item == m_current)
            return;
        m_current = item;
        emit currentChanged();
    }

    CommandItem *current() const { return m_current; }

    // What the filter field holds. "Show conflicts" writes the colliding
    // sequence into it, which is how the other commands using it are found.
    QString filterText() const { return m_filterText; }
    void setFilterText(const QString &text)
    {
        if (m_filterText == text)
            return;
        m_filterText = text;
        emit filterTextChanged(text);
    }
    Q_INVOKABLE void filterTypedIn(const QString &text) { m_filterText = text; }

signals:
    void currentChanged();
    void filterTextChanged(const QString &text);

private:
    TreeModel<> m_model{this};
    CommandItem *m_current = nullptr;
    QString m_filterText;
};

// One of the key sequences a command is mapped to.
class ShortcutAspects final : public AspectContainer
{
public:
    ShortcutAspects()
    {
        key.setQmlName("Key");
        warning.setQmlName("Warning");
        warning.setIconType(InfoType::Error);
        warning.setTextFormat(AspectControls::TextFormat::RichText);
        warning.setVisible(false);
    }

    KeySequenceAspect key{this};
    TextDisplay warning{this};
};

class KeyboardAspects final : public AspectContainer
{
public:
    KeyboardAspects();
    ~KeyboardAspects() override { qDeleteAll(m_scitems); }

    void apply() override;
    void cancel() override;

private:
    void initialize();
    void clear();
    void showCurrentCommand();
    void readShortcutsIntoCommand();
    void resetCurrentToDefault();
    void resetAllToDefault();
    void importScheme();
    void exportScheme();
    void showConflicts();
    void updateWarnings();
    bool markCollisions(ShortcutItem *item, int index);
    void markAllCollisions();

    QList<ShortcutItem *> m_scitems;
    QHash<Command *, CommandItem *> m_items;
    bool m_showing = false;
    QTimer m_updateTimer;

public:
    CommandTreeAspect &commands() { return m_commands; }
    AspectList &shortcuts() { return m_shortcuts; }
    ShortcutItem *shortcutFor(const Id &id) const
    {
        return Utils::findOrDefault(m_scitems, [id](ShortcutItem *item) {
            return item->m_cmd->id() == id;
        });
    }
    CommandItem *itemFor(const Id &id) const
    {
        ShortcutItem *item = shortcutFor(id);
        return item ? m_items.value(item->m_cmd) : nullptr;
    }
    void resetAll() { resetAllToDefault(); }

private:
    CommandTreeAspect m_commands{this};
    ActionAspect m_import{this};
    ActionAspect m_export{this};
    ActionAspect m_resetAll{this};
    ActionAspect m_reset{this};
    AspectContainer m_shortcutBox{this};
    AspectList m_shortcuts{&m_shortcutBox};
};

KeyboardAspects::KeyboardAspects()
{
    setAutoApply(false);
    setQmlSource(QUrl("qrc:/qt/qml/QtCreator/Core/KeyboardSettingsPage.qml"));

    m_commands.setQmlName("Commands");

    m_import.setQmlName("Import");
    m_import.setActionText(Tr::tr("Import..."));
    m_import.setAction([this] { importScheme(); });

    m_export.setQmlName("Export");
    m_export.setActionText(Tr::tr("Export..."));
    m_export.setAction([this] { exportScheme(); });

    m_resetAll.setQmlName("ResetAll");
    m_resetAll.setActionText(Tr::tr("Reset All"));
    m_resetAll.setToolTip(Tr::tr("Reset all to default."));
    m_resetAll.setAction([this] { resetAllToDefault(); });

    m_reset.setQmlName("Reset");
    m_reset.setActionText(Tr::tr("Reset"));
    m_reset.setToolTip(Tr::tr("Reset to default."));
    m_reset.setAction([this] { resetCurrentToDefault(); });

    m_shortcutBox.setQmlName("Shortcut");
    m_shortcutBox.setEnabled(false);

    m_shortcuts.setQmlName("Keys");
    m_shortcuts.setDisplayStyle(AspectList::DisplayStyle::InlineList);
    m_shortcuts.setCreateItemFunction([] { return std::make_shared<ShortcutAspects>(); });

    // Behaviour, not layout.
    connect(&m_commands, &CommandTreeAspect::currentChanged,
            this, [this] { showCurrentCommand(); });
    connect(&m_shortcuts, &BaseAspect::volatileValueChanged, this, [this] {
        if (!m_showing)
            readShortcutsIntoCommand();
    });
    connect(&m_shortcuts, &AspectList::volatileItemListChanged, this, [this] {
        if (!m_showing)
            readShortcutsIntoCommand();
    });

    m_updateTimer.setSingleShot(true);
    m_updateTimer.setInterval(100);
    connect(ActionManager::instance(), &ActionManager::commandListChanged,
            &m_updateTimer, qOverload<>(&QTimer::start));
    connect(&m_updateTimer, &QTimer::timeout, this, [this] { initialize(); });

    initialize();
}

void KeyboardAspects::clear()
{
    m_commands.setCurrentIndex({});
    m_commands.model().clear();
    m_items.clear();
    qDeleteAll(m_scitems);
    m_scitems.clear();
}

void KeyboardAspects::initialize()
{
    clear();
    m_commands.model().setHeader({Tr::tr("Command"), Tr::tr("Label"), Tr::tr("Shortcut")});

    QMap<QString, SectionItem *> sections;
    const QList<Command *> commands = ActionManager::commands();
    for (Command *c : commands) {
        if (c->hasAttribute(Command::CA_NonConfigurable))
            continue;
        if (c->action() && c->action()->isSeparator())
            continue;

        auto s = new ShortcutItem;
        s->m_cmd = c;
        s->m_keys = c->keySequences();
        m_scitems << s;

        const QString identifier = c->id().toString();
        const int pos = identifier.indexOf(QLatin1Char('.'));
        const QString section = identifier.left(pos);
        if (!sections.contains(section)) {
            auto sectionItem = new SectionItem(section);
            m_commands.model().rootItem()->appendChild(sectionItem);
            sections.insert(section, sectionItem);
        }
        auto item = new CommandItem(s, identifier.mid(pos + 1));
        sections[section]->appendChild(item);
        m_items.insert(c, item);
    }
    markAllCollisions();
    showCurrentCommand();
}

void KeyboardAspects::showCurrentCommand()
{
    CommandItem *item = m_commands.current();
    m_shortcutBox.setEnabled(item != nullptr);

    // Filling the box in is not the user editing it.
    m_showing = true;
    const QScopeGuard done([this] { m_showing = false; });
    m_shortcuts.clear();
    if (item) {
        // Clean up before showing: an empty sequence is not one of them.
        item->shortcut()->m_keys = cleanKeys(item->shortcut()->m_keys);
        for (int i = 0, count = qMax(1, int(item->shortcut()->m_keys.size())); i < count; ++i) {
            auto entry = std::make_shared<ShortcutAspects>();
            entry->key.setKeySequence(item->shortcut()->m_keys.value(i));
            m_shortcuts.addItem(entry);
        }
    }
    m_shortcuts.apply();
    updateWarnings();
}

void KeyboardAspects::readShortcutsIntoCommand()
{
    CommandItem *item = m_commands.current();
    if (!item)
        return;

    QList<QKeySequence> keys;
    m_shortcuts.forEachItem([&keys](const std::shared_ptr<ShortcutAspects> &entry) {
        keys.append(entry->key.keySequence());
    });
    item->shortcut()->m_keys = keys;
    item->update();
    markAllCollisions();
    updateWarnings();
    markSettingsDirty();
}

// What is wrong with each sequence, and whether anything else uses it.
void KeyboardAspects::updateWarnings()
{
    CommandItem *item = m_commands.current();
    int index = 0;
    m_shortcuts.forEachItem([this, item, &index](const std::shared_ptr<ShortcutAspects> &entry) {
        const int at = index++;
        QString message;
        const QKeySequence key = entry->key.keySequence();
        const bool valid = checkValidity(key, &message);
        if (valid && item && markCollisions(item->shortcut(), at)) {
            message = Tr::tr(
                "Key sequence has potential conflicts. <a href=\"#conflicts\">Show.</a>");
        }
        entry->warning.setText(message);
        entry->warning.setVisible(!message.isEmpty());
    });
}

void KeyboardAspects::resetCurrentToDefault()
{
    CommandItem *item = m_commands.current();
    QTC_ASSERT(item, return);
    item->shortcut()->m_keys = item->shortcut()->m_cmd->defaultKeySequences();
    item->update();
    showCurrentCommand();
    markAllCollisions();
    markSettingsDirty();
}

void KeyboardAspects::resetAllToDefault()
{
    for (ShortcutItem *item : std::as_const(m_scitems))
        item->m_keys = item->m_cmd->defaultKeySequences();
    for (CommandItem *item : std::as_const(m_items))
        item->update();
    showCurrentCommand();
    markAllCollisions();
    markSettingsDirty();
}

void KeyboardAspects::importScheme()
{
    const FilePath fileName = FileUtils::getOpenFilePath(Tr::tr("Import Keyboard Mapping Scheme"),
                                                         schemesPath(),
                                                         Tr::tr("Keyboard Mapping Scheme (*.kms)"));
    if (fileName.isEmpty())
        return;

    CommandsFile cf(fileName);
    const QMap<QString, QList<QKeySequence>> mapping = cf.importCommands();
    for (ShortcutItem *item : std::as_const(m_scitems)) {
        const QString sid = item->m_cmd->id().toString();
        if (mapping.contains(sid)) {
            item->m_keys = mapping.value(sid);
            if (CommandItem *commandItem = m_items.value(item->m_cmd))
                commandItem->update();
        }
    }
    showCurrentCommand();
    markAllCollisions();
    markSettingsDirty();
}

void KeyboardAspects::exportScheme()
{
    const FilePath filePath = DocumentManager::getSaveFileNameWithExtension(
        Tr::tr("Export Keyboard Mapping Scheme"),
        schemesPath(),
        Tr::tr("Keyboard Mapping Scheme (*.kms)"));
    if (filePath.isEmpty())
        return;
    CommandsFile cf(filePath);
    cf.exportCommands(m_scitems);
}

void KeyboardAspects::showConflicts()
{
    if (CommandItem *item = m_commands.current())
        m_commands.setFilterText(keySequencesToEditString(item->shortcut()->m_keys));
}

bool KeyboardAspects::markCollisions(ShortcutItem *item, int index)
{
    bool hasCollision = false;
    const QKeySequence key = item->m_keys.value(index);
    if (!key.isEmpty()) {
        const Id globalId(Constants::C_GLOBAL);
        const Context itemContext = item->m_cmd->context();
        const bool itemHasGlobalContext = itemContext.contains(globalId);
        for (ShortcutItem *currentItem : std::as_const(m_scitems)) {
            if (item == currentItem)
                continue;
            if (!Utils::anyOf(currentItem->m_keys, Utils::equalTo(key)))
                continue;
            // Check if contexts might conflict.
            const Context currentContext = currentItem->m_cmd->context();
            bool currentIsConflicting = (itemHasGlobalContext && !currentContext.isEmpty());
            if (!currentIsConflicting) {
                for (const Id &id : currentContext) {
                    if ((id == globalId && !itemContext.isEmpty()) || itemContext.contains(id)) {
                        currentIsConflicting = true;
                        break;
                    }
                }
            }
            if (currentIsConflicting) {
                if (CommandItem *other = m_items.value(currentItem->m_cmd))
                    other->setColliding(true);
                hasCollision = true;
            }
        }
    }
    if (CommandItem *own = m_items.value(item->m_cmd))
        own->setColliding(hasCollision);
    return hasCollision;
}

void KeyboardAspects::markAllCollisions()
{
    for (CommandItem *item : std::as_const(m_items))
        item->setColliding(false);
    for (ShortcutItem *item : std::as_const(m_scitems))
        for (int i = 0; i < item->m_keys.size(); ++i)
            markCollisions(item, i);
}

void KeyboardAspects::apply()
{
    AspectContainer::apply();
    for (const ShortcutItem *item : std::as_const(m_scitems))
        item->m_cmd->setKeySequences(item->m_keys);
}

void KeyboardAspects::cancel()
{
    AspectContainer::cancel();
    // The page is built once, so it starts again from what the commands say
    // rather than from what was being edited.
    initialize();
}


class ShortcutSettings final : public IOptionsPage
{
public:
    ShortcutSettings()
    {
        setId(Constants::SETTINGS_ID_SHORTCUTS);
        setDisplayName(Tr::tr("Keyboard"));
        setCategory(Constants::SETTINGS_CATEGORY_CORE);
        setSettingsProvider([] {
            static GuardedObject<KeyboardAspects> theAspects;
            return theAspects.get();
        });
        // Building the page means building the whole command list, and the
        // commands are in a model that the search does not see anyway.
        setFixedKeywords({Tr::tr("Keyboard Shortcuts"),
                          Tr::tr("Shortcut"),
                          Tr::tr("Reset All"),
                          Tr::tr("Reset"),
                          Tr::tr("Import..."),
                          Tr::tr("Export...")});
    }
};

#ifdef WITH_TESTS
class ShortcutSettingsTest final : public QObject
{
    Q_OBJECT

private slots:
    void cleanup()
    {
        if (KeyboardAspects *p = page())
            static_cast<BaseAspect *>(p)->cancel();
    }

    void testTypingASequenceReachesTheCommand()
    {
        // The tree row, the shortcut box and the command all say the same
        // thing; the box is what the user writes into.
        KeyboardAspects *p = page();
        QVERIFY(p);
        CommandItem *item = p->itemFor(Constants::OPTIONS);
        if (!item)
            QSKIP("The Preferences command is not registered here");
        p->commands().setCurrentIndex(p->commands().model().indexForItem(item));
        QCOMPARE(p->shortcuts().size(), 1);

        auto entry = std::static_pointer_cast<ShortcutAspects>(p->shortcuts().volatileItems().first());
        entry->key.setValue("Ctrl+Shift+F12");
        QCOMPARE(item->shortcut()->m_keys.size(), 1);
        QCOMPARE(keySequenceToEditString(item->shortcut()->m_keys.first()),
                 QString("Ctrl+Shift+F12"));
        // And the row says so, in the form a keyboard shows.
        QCOMPARE(item->data(2, Qt::DisplayRole).toString(),
                 keySequencesToNativeString(item->shortcut()->m_keys));
        // Changed from the default, so the row is marked.
        QVERIFY(item->isModified());
    }

    void testACommandCanHaveMoreThanOneSequence()
    {
        KeyboardAspects *p = page();
        QVERIFY(p);
        CommandItem *item = p->itemFor(Constants::OPTIONS);
        if (!item)
            QSKIP("The Preferences command is not registered here");
        p->commands().setCurrentIndex(p->commands().model().indexForItem(item));

        auto first = std::static_pointer_cast<ShortcutAspects>(
            p->shortcuts().volatileItems().first());
        first->key.setValue("Ctrl+Shift+F12");
        p->shortcuts().createAndAddItem();
        QCOMPARE(p->shortcuts().size(), 2);
        auto second = std::static_pointer_cast<ShortcutAspects>(
            p->shortcuts().volatileItems().at(1));
        second->key.setValue("Ctrl+Shift+F11");
        QCOMPARE(item->shortcut()->m_keys.size(), 2);

        // Removing one leaves the other.
        p->shortcuts().removeItem(p->shortcuts().volatileItems().first());
        QCOMPARE(item->shortcut()->m_keys.size(), 1);
        QCOMPARE(keySequenceToEditString(item->shortcut()->m_keys.first()),
                 QString("Ctrl+Shift+F11"));
    }

    void testTwoCommandsOnTheSameSequenceCollide()
    {
        // The colliding rows are written in the error colour and the box says
        // so, which is the only warning there is that a shortcut will not do
        // what it looks like it does.
        KeyboardAspects *p = page();
        QVERIFY(p);
        CommandItem *one = p->itemFor(Constants::OPTIONS);
        CommandItem *other = p->itemFor(Constants::NEW);
        if (!one || !other)
            QSKIP("The commands this needs are not registered here");

        p->commands().setCurrentIndex(p->commands().model().indexForItem(one));
        std::static_pointer_cast<ShortcutAspects>(p->shortcuts().volatileItems().first())
            ->key.setValue("Ctrl+Shift+F12");
        p->commands().setCurrentIndex(p->commands().model().indexForItem(other));
        auto entry = std::static_pointer_cast<ShortcutAspects>(
            p->shortcuts().volatileItems().first());
        entry->key.setValue("Ctrl+Shift+F12");

        QVERIFY(one->data(2, Qt::ForegroundRole).isValid());
        QVERIFY(other->data(2, Qt::ForegroundRole).isValid());
        QVERIFY(entry->warning.isVisible());
        QVERIFY(entry->warning.text().contains("conflict"));

        // And a row that collides with nothing is not marked.
        entry->key.setValue("Ctrl+Shift+F11");
        QVERIFY(!other->data(2, Qt::ForegroundRole).isValid());
        QVERIFY(!entry->warning.isVisible());
    }

    void testACommandIsFoundByTheSequenceItIsMappedTo()
    {
        // Typing a shortcut into the filter field finds the commands using it,
        // which is what "Show conflicts" does. The rows do not show the
        // portable text, so filtering on what they show would find nothing.
        KeyboardAspects *p = page();
        QVERIFY(p);
        CommandItem *item = p->itemFor(Constants::OPTIONS);
        if (!item)
            QSKIP("The Preferences command is not registered here");
        p->commands().setCurrentIndex(p->commands().model().indexForItem(item));
        std::static_pointer_cast<ShortcutAspects>(p->shortcuts().volatileItems().first())
            ->key.setValue("Ctrl+Shift+F12");

        QSortFilterProxyModel filter;
        filter.setFilterCaseSensitivity(Qt::CaseInsensitive);
        filter.setFilterKeyColumn(-1);
        filter.setSourceModel(p->commands().tableModel());
        const QString shown = item->data(2, Qt::DisplayRole).toString();
        const QVariant found = item->data(0, Utils::AspectTable::FilterTextRole);
        QVERIFY(found.toString().contains("Ctrl+Shift+F12"));
        QVERIFY(!shown.contains("Ctrl+Shift+F12"));
    }

    void testResettingPutsTheDefaultsBack()
    {
        KeyboardAspects *p = page();
        QVERIFY(p);
        CommandItem *item = p->itemFor(Constants::OPTIONS);
        if (!item)
            QSKIP("The Preferences command is not registered here");
        const QList<QKeySequence> original = item->shortcut()->m_cmd->defaultKeySequences();
        p->commands().setCurrentIndex(p->commands().model().indexForItem(item));
        std::static_pointer_cast<ShortcutAspects>(p->shortcuts().volatileItems().first())
            ->key.setValue("Ctrl+Shift+F12");
        QVERIFY(item->isModified());

        p->resetAll();
        QCOMPARE(item->shortcut()->m_keys, original);
        QVERIFY(!item->isModified());
    }

private:
    static KeyboardAspects *page()
    {
        Core::IOptionsPage *found = Utils::findOrDefault(
            Core::IOptionsPage::allOptionsPages(), [](Core::IOptionsPage *p) {
                return p->id() == Constants::SETTINGS_ID_SHORTCUTS;
            });
        if (!found)
            return nullptr;
        const std::optional<AspectContainer *> aspects = found->aspects();
        return aspects ? static_cast<KeyboardAspects *>(*aspects) : nullptr;
    }
};

QObject *createShortcutSettingsTest()
{
    return new ShortcutSettingsTest;
}
#endif // WITH_TESTS

void setupShortcutSettings()
{
    static ShortcutSettings theShortcutSettings;
}

} // namespace Core::Internal

#include "shortcutsettings.moc"
