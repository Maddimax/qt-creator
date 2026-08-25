// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aspects.h"

#include "algorithm.h"
#include "async.h"
#include "checkableaspect.h"
#include "checkabledecider.h"
#include "environment.h"
#include "guard.h"
#include "macroexpander.h"
#include "pathvalidation.h"
#include "qtcassert.h"
#include "qtcsettings.h"
#include "store.h"
#include "stringutils.h"
#include "utilstr.h"

#include <QAction>
#include <QDebug>
#include <QFontInfo>
#include <QItemSelectionModel>
#include <QPointer>
#include <QStandardItemModel>
#include <QUndoStack>

using namespace Layouting;

namespace Utils {



BaseAspect::Changes::Changes()
{
    memset(this, 0, sizeof(*this));
}

class Internal::BaseAspectPrivate
{
public:
    explicit BaseAspectPrivate(AspectContainer *container) : m_container(container) {}

    MacroExpander *macroExpander()
    {
        if (!m_expander) {
            m_expander = std::make_unique<MacroExpander>();
            m_expander->setDisplayName("Variables");
            if (m_container) {
                MacroExpanderProvider p(m_container, m_container->macroExpander());
                m_expander->registerSubProvider(p);
            }
        }
        return m_expander.get();
    }

    void setContainer(AspectContainer *container)
    {
        m_container = container;
        if (m_expander) {
            MacroExpanderProvider p(m_container, m_container->macroExpander());
            m_expander->registerSubProvider(p);
        }
    }

    Id m_id;
    QString m_displayName;
    Key m_settingsKey; // Name of data in settings.
    QString m_tooltip;
    QString m_labelText;
    QPixmap m_labelPixmap;
    QString m_controlObjectName;
    QString m_qmlName;
    QIcon m_icon;
    QPointer<QAction> m_action; // Owned by us.
    AspectContainer *m_container = nullptr; // Not owned by us.

    bool m_visible = true;
    bool m_readOnly = false;
    bool m_autoApply = true;
    bool m_saveAlways = false; // if true, also empty keys will be written
    QPointer<BoolAspect> m_enabler;
    bool m_enabled = true;
    int m_spanX = 1;
    int m_spanY = 1;
    BaseAspect::ConfigWidgetCreator m_configWidgetCreator;

    BaseAspect::DataCreator m_dataCreator;
    BaseAspect::DataCloner m_dataCloner;
    QList<BaseAspect::DataExtractor> m_dataExtractors;

    QUndoStack *m_undoStack = nullptr;

private:
    std::unique_ptr<MacroExpander> m_expander;
};

/*!
    \class Utils::BaseAspect
    \inmodule QtCreator

    \brief The \c BaseAspect class provides a common base for classes implementing
    aspects.

    An \e aspect is a hunk of data like a property or collection of related
    properties of some object, together with a description of its behavior
    for common operations like visualizing or persisting.

    Simple aspects are, for example, a boolean property represented by a QCheckBox
    in the user interface, or a string property represented by a PathChooser,
    for selecting directories in the filesystem.

    While aspects implementations usually can visualize and persist
    their data, or use an ID, neither of these is mandatory.

    The derived classes can implement addToLayout() to create a UI.

    Implement \c guiToBuffer() and \c bufferToGui() to synchronize the UI with
    the internal data.
*/

/*!
    \enum Utils::BaseAspect::Announcement

    Whether to emit a signal when a value changes.

    \value DoEmit
           Emit a signal.
    \value BeQuiet
           Don't emit a signal.
*/

/*!
    Constructs a base aspect.

    If \a container is non-null, the aspect is made known to the container.
*/
BaseAspect::BaseAspect(AspectContainer *container)
    : d(new Internal::BaseAspectPrivate(container))
{
    if (container)
        container->registerAspect(this);
    addDataExtractor(this, &BaseAspect::variantValue, &Data::value);
}

/*!
    Destructs a BaseAspect.
*/
BaseAspect::~BaseAspect()
{
    delete d->m_action;
}

Id BaseAspect::id() const
{
    return d->m_id;
}

void BaseAspect::setId(Id id)
{
    d->m_id = id;
}

QVariant BaseAspect::volatileVariantValue() const
{
    return {};
}

void BaseAspect::setVolatileVariantValue(const QVariant &value, Announcement howToAnnounce)
{
    Q_UNUSED(value)
    Q_UNUSED(howToAnnounce)
    QTC_CHECK(false);
}

QVariant BaseAspect::variantValue() const
{
    return {};
}

/*!
    Sets \a value.

    If \a howToAnnounce is set to \c DoEmit, emits the \c valueChanged signal.

    Prefer the typed \c setValue() of the derived classes.
*/
void BaseAspect::setVariantValue(const QVariant &value, Announcement howToAnnounce)
{
    Q_UNUSED(value)
    Q_UNUSED(howToAnnounce)
    QTC_CHECK(false);
}

void BaseAspect::setDefaultVariantValue(const QVariant &value)
{
    Q_UNUSED(value)
    QTC_CHECK(false);
}

bool BaseAspect::isDefaultValue() const
{
    return defaultVariantValue() == variantValue();
}

QVariant BaseAspect::defaultVariantValue() const
{
    return {};
}

/*!
    \class Utils::TypedAspect
    \inheaderfile utils/aspects.h
    \inmodule QtCreator

    \brief The \c TypedAspect class is a helper class for implementing a simple
    aspect.

    A typed aspect contains a single piece of data that is of the type
    \c ValueType.
*/


/*!
    \fn template <typename ValueType> void Utils::TypedAspect<ValueType>::setDefaultValue(const ValueType &value)

    Sets a default \a value and the current value for this aspect.

    \note The current value will be set silently to the same value.
    It is reasonable to only set default values in the setup phase
    of the aspect.

    Default values will not be stored in settings.
*/

void BaseAspect::setDisplayName(const QString &displayName)
{
    d->m_displayName = displayName;
}

bool BaseAspect::isVisible() const
{
    return d->m_visible;
}

/*!
    Shows or hides the visual representation of this aspect depending
    on the value of \a visible.
    By default, it is visible.
 */
void BaseAspect::setVisible(bool visible)
{
    if (visible == d->m_visible)
        return;

    d->m_visible = visible;
    emit visibleChanged(visible);
}




/*!
    Sets \a labelText as text for the separate label in the visual
    representation of this aspect.
*/
void BaseAspect::setLabelText(const QString &labelText)
{
    d->m_labelText = labelText;
    emit labelTextChanged();
}

/*!
    Sets \a labelPixmap as pixmap for the separate label in the visual
    representation of this aspect.
*/
/*!
    Returns the name a QML page uses to reach this aspect.

    \sa setQmlName()
*/
QString BaseAspect::qmlName() const
{
    if (!d->m_qmlName.isEmpty())
        return d->m_qmlName;

    // Settings keys in the tree use both separators, and plenty use neither.
    const QString key = stringFromKey(d->m_settingsKey);
    const qsizetype lastSeparator = std::max(key.lastIndexOf('/'), key.lastIndexOf('.'));
    return key.mid(lastSeparator + 1);
}

void BaseAspect::setQmlName(const QString &qmlName)
{
    d->m_qmlName = qmlName;
}

void BaseAspect::setControlObjectName(const QString &objectName)
{
    d->m_controlObjectName = objectName;
    emit controlConfigurationChanged();
}

void BaseAspect::setLabelPixmap(const QPixmap &labelPixmap)
{
    d->m_labelPixmap = labelPixmap;
    emit labelPixmapChanged();
}

void BaseAspect::setIcon(const QIcon &icon)
{
    d->m_icon = icon;
    if (d->m_action)
        d->m_action->setIcon(icon);
}

QIcon BaseAspect::icon() const
{
    return d->m_icon;
}

/*!
    Returns the current text for the separate label in the visual
    representation of this aspect.
*/
QString BaseAspect::labelText() const
{
    return d->m_labelText;
}

QString BaseAspect::plainLabelText() const
{
    return stripAccelerator(d->m_labelText);
}

QString BaseAspect::toolTip() const
{
    return d->m_tooltip;
}

/*!
    Sets \a tooltip as tool tip for the visual representation of this aspect.
 */
void BaseAspect::setToolTip(const QString &tooltip)
{
    if (tooltip == d->m_tooltip)
        return;

    d->m_tooltip = tooltip;
    emit tooltipChanged(tooltip);
}

void BaseAspect::setUndoStack(QUndoStack *undoStack)
{
    d->m_undoStack = undoStack;
}

QUndoStack *BaseAspect::undoStack() const
{
    return d->m_undoStack;
}

bool BaseAspect::isEnabled() const
{
    if (d->m_enabler)
        return d->m_enabler->isEnabled() && d->m_enabler->volatileValue();
    return d->m_enabled;
}

void BaseAspect::setEnabled(bool enabled)
{
    if (enabled == d->m_enabled)
        return;

    d->m_enabled = enabled;
    emit enabledChanged();
}

/*!
    Makes the enabled state of this aspect depend on the checked state of \a checker.
*/
void BaseAspect::setEnabler(BoolAspect *checker)
{
    QTC_ASSERT(checker, return);

    d->m_enabler = checker;

    auto update = [this] { BaseAspect::setEnabled(isEnabled()); };

    connect(checker, &BoolAspect::volatileValueChanged, this, update);
    connect(checker, &BoolAspect::changed, this, update);
    connect(checker, &BaseAspect::enabledChanged, this, update);

    update();
}

bool BaseAspect::isReadOnly() const
{
    return d->m_readOnly;
}

void BaseAspect::setReadOnly(bool readOnly)
{
    if (readOnly == d->m_readOnly)
        return;

    d->m_readOnly = readOnly;
    emit readOnlyChanged(readOnly);
}

void BaseAspect::setSpan(int x, int y)
{
    d->m_spanX = x;
    d->m_spanY = y;
}

bool BaseAspect::isSaveAlways() const
{
    return d->m_saveAlways;
}

void BaseAspect::setSaveAlways(bool saveAlways)
{
    d->m_saveAlways = saveAlways;
}

bool BaseAspect::isAutoApply() const
{
    return d->m_autoApply;
}

/*!
    Sets auto-apply mode. When auto-apply mode is \a off, user interaction to this
    aspect's widget will not modify the \c value of the aspect until \c apply()
    is called programmatically.

    \sa setSettingsKey()
*/

void BaseAspect::setAutoApply(bool on)
{
    d->m_autoApply = on;
}

/*!
    \internal
*/
void BaseAspect::setConfigWidgetCreator(const ConfigWidgetCreator &configWidgetCreator)
{
    d->m_configWidgetCreator = configWidgetCreator;
}

/*!
    Returns the key to be used when accessing the settings.

    \sa setSettingsKey()
*/
Key BaseAspect::settingsKey() const
{
    return d->m_settingsKey;
}

/*!
    Sets the \a key to be used when accessing the settings.

    \sa settingsKey()
*/
void BaseAspect::setSettingsKey(const Key &key)
{
    d->m_settingsKey = key;
}

/*!
    Sets the \a key and \a group to be used when accessing the settings.

    \sa settingsKey()
*/
void BaseAspect::setSettingsKey(const Key &group, const Key &key)
{
    d->m_settingsKey = group + "/" + key;
}

/*!
    Immediately writes the value of this aspect into its specified
    settings, taking a potential container's settings group specification
    into account.

    \note This is expensive, so it should only be used with good reason.
*/
void BaseAspect::writeToSettingsImmediatly() const
{
    QStringList groups;
    if (d->m_container)
        groups = d->m_container->settingsGroups();
    const SettingsGroupNester nester(groups);
    writeSettings();
}

/*!
    Returns the string that should be used when this action appears in menus
    or other places that are typically used with Book style capitalization.

    If no display name is set, the label text will be used as fallback.
*/

QString BaseAspect::displayName() const
{
    return d->m_displayName.isEmpty() ? d->m_labelText : d->m_displayName;
}

/*!
    \internal
*/
BaseAspect::ConfigWidgetCreator BaseAspect::configWidgetCreator() const
{
    return d->m_configWidgetCreator;
}

QAction *BaseAspect::action()
{
    if (!d->m_action) {
        d->m_action = new QAction(labelText());
        d->m_action->setIcon(d->m_icon);
    }
    return d->m_action;
}

AspectContainer *BaseAspect::container() const
{
    return d->m_container;
}

/*!
    Adds the visual representation of this aspect to the layout with the
    specified \a parent using a layout builder.
*/
void BaseAspect::addToLayoutImpl(Layout &)
{
}

/*!
    Describes the control this aspect wants, so that a renderer does not have to
    know the aspect types. The base fills the fields every control has; each
    aspect adds what its own control needs.
*/
void BaseAspect::setVolatileVariantValueFromGui(const QVariant &value)
{
    setVolatileVariantValue(value);
}

AspectPresentation BaseAspect::presentation() const
{
    AspectPresentation p;
    p.control = AspectControls::Custom;
    p.labelText = labelText();
    p.labelPixmap = d->m_labelPixmap;
    p.objectName = d->m_controlObjectName;
    p.spanX = d->m_spanX;
    p.spanY = d->m_spanY;
    p.toolTip = toolTip();
    p.readOnly = isReadOnly();
    p.visible = isVisible();
    p.enabled = isEnabled();
    return p;
}

void addToLayout(Layouting::Layout *layout, const BaseAspect &aspect)
{
    aspect.addToLayout(*layout);
}

void addToLayout(Layouting::Layout *layout, const BaseAspect *aspect)
{
    aspect->addToLayout(*layout);
}

static AspectRenderer s_aspectRenderer;

void setAspectRenderer(const AspectRenderer &renderer)
{
    s_aspectRenderer = renderer;
}

static bool renderAspect(BaseAspect &aspect, Layout &parent)
{
    return s_aspectRenderer && s_aspectRenderer(aspect, parent);
}

/*!
    Updates this aspect's value from user-initiated changes in the widget.

    Emits changed() if the value changed.
*/
void BaseAspect::apply()
{
    // We assume m_volatileValue to reflect current gui state as invariant after
    // signalling settled down. It's an aspect (-subclass) implementation problem
    // if this doesn't hold. Fix it up and bark.
    QTC_CHECK(!guiToVolatileValue());

    if (!volatileValueToValue()) // Nothing to do.
        return;

    Changes changes;
    changes.valueFromVolatileValue = true;
    announceChanges(changes);
}

/*!
    Discard user changes in the widget and restore widget contents from
    aspect's value.

    This has only an effect if \c isAutoApply is false.
*/
void BaseAspect::cancel()
{
    Changes changes;
    changes.volatileValueFromValue = valueToVolatileValue();
    volatileValueToGui();
    announceChanges(changes);
}

bool BaseAspect::hasAction() const
{
    return d->m_action != nullptr;
}

void BaseAspect::announceChanges(Changes changes, Announcement howToAnnounce)
{
    if (howToAnnounce == BeQuiet)
        return;

    if (changes.volatileValueFromValue || changes.volatileValueFromOutside || changes.volatileValueFromGui)
        emit volatileValueChanged();

    if (changes.valueFromOutside || changes.valueFromVolatileValue) {
        emit changed();
        if (hasAction())
            emit action()->triggered(variantValue().toBool());
    }
}

bool BaseAspect::isDirty() const
{
    return false;
}


void BaseAspect::setContainer(AspectContainer *container)
{
    d->setContainer(container);
}


void BaseAspect::saveToMap(Store &data, const QVariant &value,
                           const QVariant &defaultValue, const Key &key) const
{
    if (key.isEmpty() && !d->m_saveAlways)
        return;
    if (value == defaultValue)
        data.remove(key);
    else
        data.insert(key, value);
}

bool BaseAspect::skipSave() const
{
    return settingsKey().isEmpty() && !d->m_saveAlways;
}

/*!
    Retrieves the internal value of this BaseAspect from the Store \a map.
*/
void BaseAspect::fromMap(const Store &map)
{
    if (skipSave())
        return;

    const QVariant val = map.value(settingsKey(), toSettingsValue(defaultVariantValue()));
    setVariantValue(fromSettingsValue(val), BeQuiet);
}

/*!
    Stores the internal value of this BaseAspect into the Store \a map.
*/
void BaseAspect::toMap(Store &map) const
{
    saveToMap(map, toSettingsValue(variantValue()), toSettingsValue(defaultVariantValue()), settingsKey());
}

void BaseAspect::volatileToMap(Store &map) const
{
    saveToMap(map,
              toSettingsValue(volatileVariantValue()),
              toSettingsValue(defaultVariantValue()),
              settingsKey());
}

/*!
    Retrieves the volatile value of this BaseAspect from the Store \a map.

    The default implementation does nothing. An aspect that keeps a volatile
    value of its own overrides this; one that does not is left untouched,
    rather than having its applied value written behind the back of the
    container being edited.
*/
void BaseAspect::volatileFromMap(const Store &map)
{
    Q_UNUSED(map)
}

void BaseAspect::addToLayout(Layouting::Layout &parent) const
{
    const_cast<BaseAspect *>(this)->addToLayoutImpl(parent);
}

void BaseAspect::readSettings()
{
    if (skipSave())
        return;
    const QVariant val = Utils::userSettings().value(settingsKey());
    setVariantValue(val.isValid() ? fromSettingsValue(val) : defaultVariantValue(), BeQuiet);
}

void BaseAspect::writeSettings() const
{
    if (skipSave())
        return;
    Utils::userSettings().setValueWithDefault(settingsKey(),
                                              toSettingsValue(variantValue()),
                                              toSettingsValue(defaultVariantValue()));
}

QVariant BaseAspect::toSettingsValue(const QVariant &valueToSave) const
{
    return valueToSave;
}

QVariant BaseAspect::fromSettingsValue(const QVariant &savedValue) const
{
    return savedValue;
}

void BaseAspect::setMacroExpander(MacroExpander *expander)
{
    d->macroExpander()->clearSubProviders();
    if (expander)
        d->macroExpander()->registerSubProvider({this, [expander] { return expander; }});
}

/*!
    Returns a summary of this aspect's value for the controls that show one
    rather than an editor.
*/
QString BaseAspect::displayText() const
{
    return {};
}

/*!
    Reports that \a link in the aspect's text was activated. Does nothing
    unless an aspect gives the links a meaning.
*/
void BaseAspect::activateLink(const QString &link)
{
    Q_UNUSED(link)
}

/*!
    Asks the aspect to produce its displayText(). Aspects that keep their value
    to hand have nothing to do here.
*/
void BaseAspect::requestDisplayText()
{}

/*!
    Returns the rows this aspect holds, for one whose control is
    \c AspectControls::Table. Anything else has none.
*/
QAbstractItemModel *BaseAspect::tableModel()
{
    return nullptr;
}

/*!
    Returns what is wrong with \a candidate, or an empty string when nothing is.

    An aspect that has been given a validation function answers it here, so
    that a renderer holding nothing but a BaseAspect can ask. Aspects with no
    validation of their own accept everything.
*/
// What a validation function that can be run without a control says about
// \a candidate. The other two shapes cannot answer here: one is handed a
// FancyLineEdit, and one answers a QFuture. Both are the widget renderer's.
// What a validation function says about \a candidate, where it can say it here
// and now. The shape taking a FancyLineEdit is the widget renderer's and has
// nothing to be handed; the asynchronous one goes through AsyncValidation.
static QString validationMessageFor(const std::optional<ValidationFunction> &validator,
                                    const QVariant &candidate)
{
    if (!validator)
        return {};
    if (auto simple = std::get_if<SimpleSynchronousValidationFunction>(&*validator)) {
        const Result<> result = (*simple)(candidate.toString());
        return result ? QString() : result.error();
    }
    return {};
}

// An answer that has to be gone and got: whether the path a field holds is a
// debugger, say, is decided by running it. The aspect remembers the answer for
// one candidate - the one the field is showing - and says so when it arrives.
//
// Nothing is reported while a check is in flight. Showing the last answer
// would be showing it about text the user has already changed.
QString AsyncValidation::messageFor(const std::optional<ValidationFunction> &validator,
                                    const QVariant &candidate,
                                    BaseAspect *aspect)
{
    auto async = validator ? std::get_if<AsyncValidationFunction>(&*validator) : nullptr;
    if (!async)
        return {};

    const QString text = candidate.toString();
    if (text == m_candidate)
        return m_message;

    m_candidate = text;
    m_message.clear();
    (*async)(text).then(aspect, [this, aspect, text](const AsyncValidationResult &result) {
        // A later candidate has been asked about since; this answer is about
        // text that is no longer there.
        if (text != m_candidate)
            return;
        m_message = result ? QString() : result.error();
        emit aspect->validationMessageChanged();
    });
    return {};
}

QString BaseAspect::validationMessage(const QVariant &candidate) const
{
    Q_UNUSED(candidate)
    return {};
}

/*!
    Puts this aspect's value back to its default, as a control's reset button
    does. Goes through the volatile value, so that a page's Cancel still undoes
    it.
*/
void BaseAspect::resetToDefault()
{
    setVolatileVariantValueFromGui(defaultVariantValue());
}

/*!
    Performs this aspect's one action, for a TextWithAction control. Does
    nothing unless an aspect gives it a meaning.
*/
void BaseAspect::triggerAction()
{}

MacroExpander *BaseAspect::macroExpander() const
{
    return d->macroExpander();
}

void BaseAspect::addOnChanged(QObject *guard, const Callback &callback)
{
    connect(this, &BaseAspect::changed, guard, callback);
}

void BaseAspect::addOnVolatileValueChanged(QObject *guard, const Callback &callback)
{
    connect(this, &BaseAspect::volatileValueChanged, guard, callback);
}

void BaseAspect::addOnCheckedChanged(QObject *guard, const Callback &callback)
{
    connect(this, &BaseAspect::checkedChanged, guard, callback);
}

void BaseAspect::addOnEnabledChanged(QObject *guard, const Callback &callback)
{
    connect(this, &BaseAspect::enabledChanged, guard, callback);
}

void BaseAspect::addOnLabelTextChanged(QObject *guard, const Callback &callback)
{
    connect(this, &BaseAspect::labelTextChanged, guard, callback);
}

void BaseAspect::addOnLabelPixmapChanged(QObject *guard, const Callback &callback)
{
    connect(this, &BaseAspect::labelPixmapChanged, guard, callback);
}


namespace Internal {

class BoolAspectPrivate
{
public:
    BoolAspect::LabelPlacement m_labelPlacement = BoolAspect::LabelPlacement::AtCheckBox;
    BoolAspect::DisplayStyle m_displayStyle = BoolAspect::DisplayStyle::CheckBox;
    UndoableValue<bool> m_undoable;
};

class ToggleAspectPrivate
{
public:
    struct Data
    {
        QIcon icon;
        QString tooltip;
        QString text;
    } on, off;
};

class ColorAspectPrivate
{
public:
    bool m_alphaAllowed = true;
    bool m_withResetButton = true;
    QSize m_minimumSize{64, 0};
};

class FontFamilyAspectPrivate
{
public:
    UndoableValue<QString> m_undoable;
    FontFamilyAspect::FontFilters m_fontFilters = FontFamilyAspect::AllFonts;
};

class SelectionAspectPrivate
{
public:
    SelectionAspect::DisplayStyle m_displayStyle = SelectionAspect::DisplayStyle::RadioButtons;
    QList<SelectionAspect::Option> m_options;
    UndoableValue<int> m_undoable;
    bool m_useDataAsSavedValue = false;
};

class MultiSelectionAspectPrivate
{
public:
    explicit MultiSelectionAspectPrivate(MultiSelectionAspect *q) : q(q) {}

    bool setValueSelectedHelper(const QString &value, bool on);

    MultiSelectionAspect *q;
    QStringList m_allValues;
    MultiSelectionAspect::DisplayStyle m_displayStyle
        = MultiSelectionAspect::DisplayStyle::ListView;
    UndoableValue<QStringList> m_undoable;
};

class StringAspectPrivate
{
public:
    StringAspect::DisplayStyle m_displayStyle = StringAspect::LabelDisplay;
    std::function<QString(const QString &)> m_displayFilter;

    Qt::TextElideMode m_elideMode = Qt::ElideNone;
    QString m_placeHolderText;
    Key m_historyCompleterKey;
    StringAspect::ValueAcceptor m_valueAcceptor;
    std::optional<ValidationFunction> m_validator;
    std::function<QValidator *(QObject *parent)> m_validatorFactory;

    CheckableAspectImplementation m_checkerImpl;

    bool m_undoRedoEnabled = true;
    bool m_acceptRichText = false;
    bool m_showToolTipOnLabel = false;
    bool m_useResetButton = false;
    bool m_autoApplyOnEditingFinished = false;
    bool m_validatePlaceHolder = false;

    FilePath m_rightSideIconPath;
    int m_minimumHeight = 0;
    QStringList m_completions;

    UndoableValue<QString> undoable;
};

class IntegerAspectPrivate
{
public:
    std::optional<qint64> m_minimumValue;
    std::optional<qint64> m_maximumValue;
    int m_displayIntegerBase = 10;
    qint64 m_displayScaleFactor = 1;
    QString m_prefix;
    QString m_suffix;
    QString m_specialValueText;
    int m_singleStep = 1;
    // Holds the stored value; the display scale factor is applied only at the spin box.
    UndoableValue<qint64> m_undoable;
};

class DoubleAspectPrivate
{
public:
    std::optional<double> m_minimumValue;
    std::optional<double> m_maximumValue;
    QString m_prefix;
    QString m_suffix;
    QString m_specialValueText;
    double m_singleStep = 1;
    UndoableValue<double> m_undoable;
};

class StringListAspectPrivate
{
public:
    UndoableValue<QStringList> m_undoable;
    bool m_allowAdding{true};
    bool m_allowRemoving{true};
    bool m_allowEditing{true};
    StringListAspect::DisplayStyle m_displayStyle{StringListAspect::DisplayStyle::ListView};
};

class FilePathListAspectPrivate
{
public:
    UndoableValue<QStringList> undoable;
    QString placeHolderText;
};

class TextDisplayPrivate
{
public:
    QString m_message;
    InfoType m_type = InfoType::None;
    bool m_wordWrap = true;
    AspectControls::TextFormat m_textFormat = AspectControls::TextFormat::AutoText;
};

class ActionAspectPrivate
{
public:
    QString m_actionText;
    std::function<void()> m_action;
    std::function<void()> m_onShown;
    QList<AspectPresentation::Choice> m_choices;
    std::function<void(const QVariant &)> m_onChoice;
};

} // Internal

/*!
    \enum Utils::StringAspect::DisplayStyle
    \inmodule QtCreator

    The DisplayStyle enum describes the main visual characteristics of a
    string aspect.

      \value LabelDisplay
             Based on QLabel, used for text that cannot be changed by the
             user in this place, for example names of executables that are
             defined in the build system.

      \value LineEditDisplay
             Based on QLineEdit, used for user-editable strings that usually
             fit on a line.

      \value TextEditDisplay
             Based on QTextEdit, used for user-editable strings that often
             do not fit on a line.

      \value PasswordLineEditDisplay
             Based on QLineEdit, used for password strings

    \sa Utils::PathChooser
*/

/*!
    \class Utils::StringAspect
    \inmodule QtCreator

    \brief A string aspect is a string-like property of some object, together with
    a description of its behavior for common operations like visualizing or
    persisting.

    String aspects can represent for example a parameter for an external commands,
    paths in a file system, or simply strings.

    The string can be displayed using a QLabel, QLineEdit, QTextEdit or
    Utils::PathChooser.

    The visual representation often contains a label in front of the display
    of the actual value.
*/

/*!
    Constructs the string aspect \a container.
 */

StringAspect::StringAspect(AspectContainer *container)
    : TypedAspect(container), d(new Internal::StringAspectPrivate)
{
    connect(&d->undoable.m_signal, &UndoSignaller::changed, this,
            [this] { handleGuiChanged(); });
    setSpan(2, 1); // Default: Label + something
}

/*!
    \internal
*/
StringAspect::~StringAspect() = default;

/*!
    \internal
*/
void StringAspect::setValueAcceptor(StringAspect::ValueAcceptor &&acceptor)
{
    d->m_valueAcceptor = std::move(acceptor);
}

/*!
    \reimp
*/
void StringAspect::fromMap(const Store &map)
{
    if (!skipSave())
        setValue(map.value(settingsKey(), defaultValue()).toString(), BeQuiet);
    d->m_checkerImpl.fromMap(map);
}

/*!
    \reimp
*/
void StringAspect::toMap(Store &map) const
{
    saveToMap(map, value(), defaultValue(), settingsKey());
    d->m_checkerImpl.toMap(map);
}

void StringAspect::volatileToMap(Store &map) const
{
    saveToMap(map, volatileValue(), defaultValue(), settingsKey());
    d->m_checkerImpl.volatileToMap(map);
}

void StringAspect::volatileFromMap(const Store &map)
{
    d->m_checkerImpl.volatileFromMap(map);
    if (!skipSave())
        setVolatileValue(map.value(settingsKey(), defaultValue()).toString());
}

/*!
    \internal
*/
void StringAspect::setShowToolTipOnLabel(bool show)
{
    d->m_showToolTipOnLabel = show;
    volatileValueToGui();
}

/*!
    Sets a \a displayFilter for fine-tuning the visual appearance
    of the value of this string aspect.
*/
QString StringAspect::displayText() const
{
    const std::function<QString(const QString &)> filter = displayFilter();
    return filter ? filter(volatileValue()) : volatileValue();
}

void StringAspect::setDisplayFilter(const std::function<QString(const QString &)> &displayFilter)
{
    d->m_displayFilter = displayFilter;
}

/*!
    Selects the main display characteristics of the aspect according to
    \a displayStyle.

    \note Not all StringAspect features are available with all display styles.

    \sa Utils::StringAspect::DisplayStyle
*/
void StringAspect::setDisplayStyle(DisplayStyle displayStyle)
{
    d->m_displayStyle = displayStyle;
}

/*!
    Sets \a placeHolderText as place holder for line and text displays.
*/
void StringAspect::setPlaceHolderText(const QString &placeHolderText)
{
    if (d->m_placeHolderText == placeHolderText)
        return;

    d->m_placeHolderText = placeHolderText;
    emit placeholderTextChanged(placeHolderText);
}

/*!
    Sets \a elideMode as label elide mode.
*/
void StringAspect::setElideMode(Qt::TextElideMode elideMode)
{
    if (d->m_elideMode == elideMode)
        return;
    d->m_elideMode = elideMode;
    emit elideModeChanged(elideMode);
}

/*!
    Sets \a historyCompleterKey as key for the history completer settings for
    line edits and path chooser displays.

    \sa Utils::PathChooser::setExpectedKind()
*/
void StringAspect::setHistoryCompleter(const Key &historyCompleterKey)
{
    d->m_historyCompleterKey = historyCompleterKey;
    emit historyCompleterKeyChanged(historyCompleterKey);
}

void StringAspect::setAcceptRichText(bool acceptRichText)
{
    d->m_acceptRichText = acceptRichText;
    emit acceptRichTextChanged(acceptRichText);
}

void StringAspect::setUseResetButton()
{
    d->m_useResetButton = true;
}

void StringAspect::setValidationFunction(const ValidationFunction &validator)
{
    d->m_validator = validator;
    emit validationFunctionChanged(validator);
}

void StringAspect::setValidatorFactory(
    const std::function<QValidator *(QObject *parent)> &validatorFactory)
{
    d->m_validatorFactory = validatorFactory;
}

void StringAspect::setAutoApplyOnEditingFinished(bool applyOnEditingFinished)
{
    d->m_autoApplyOnEditingFinished = applyOnEditingFinished;
}

void StringAspect::setVolatileVariantValueFromGui(const QVariant &value)
{
    if (!value.canConvert<QString>())
        return;
    d->undoable.set(undoStack(), value.toString());
    handleGuiChanged();
}

AspectPresentation StringAspect::presentation() const
{
    AspectPresentation p = TypedAspect::presentation();
    switch (d->m_displayStyle) {
    case LabelDisplay:            p.control = AspectControls::Label; break;
    case LineEditDisplay:         p.control = AspectControls::LineEdit; break;
    case TextEditDisplay:         p.control = AspectControls::TextEdit; break;
    case PasswordLineEditDisplay: p.control = AspectControls::PasswordLineEdit; break;
    }
    p.placeholderText = d->m_placeHolderText;
    p.withResetButton = d->m_useResetButton;
    p.completions = d->m_completions;
    return p;
}

void StringAspect::addToLayoutImpl(Layout &parent)
{
    // The widget renderer owns this control's construction. Reaching the
    // check means no renderer was installed; see installAspectWidgetRenderer().
    QTC_CHECK(renderAspect(*this, parent));
}

QString StringAspect::expandedValue() const
{
    return operator()();
}

QString StringAspect::operator()() const
{
    if (!m_value.isEmpty()) {
        if (auto expander = macroExpander())
            return expander->expand(m_value);
    }
    return m_value;
}

bool StringAspect::guiToVolatileValue()
{
    return updateStorage(m_volatileValue, d->undoable.get());
}

bool StringAspect::volatileValueToValue()
{
    if (d->m_valueAcceptor) {
        if (const std::optional<QString> tmp = d->m_valueAcceptor(m_value, m_volatileValue))
           return updateStorage(m_value, *tmp);
        return false;
    }
    return updateStorage(m_value, m_volatileValue);
}

bool StringAspect::valueToVolatileValue()
{
    const QString val = d->m_displayFilter ? d->m_displayFilter(m_value) : m_value;
    return updateStorage(m_volatileValue, val);
}

void StringAspect::volatileValueToGui()
{
    d->undoable.setWithoutUndo(m_volatileValue);
}

/*!
    Adds a check box with a \a checkerLabel according to \a checkBoxPlacement
    to the line edit.

    The state of the check box is made persistent when using a non-emtpy
    \a checkerKey.
*/
void StringAspect::makeCheckable(CheckBoxPlacement checkBoxPlacement,
                                 const QString &checkerLabel, const Key &checkerKey)
{
    d->m_checkerImpl.makeCheckable(checkBoxPlacement, checkerLabel, checkerKey, this);
}

bool StringAspect::isChecked() const
{
    return d->m_checkerImpl.isChecked();
}

void StringAspect::setChecked(bool checked)
{
    return d->m_checkerImpl.setChecked(checked);
}

void StringAspect::addOnRightSideIconClicked(QObject *guard,
                                             const std::function<void ()> &callback)
{
    connect(this, &StringAspect::rightSideIconClicked, guard, callback);
}

void StringAspect::setMinimumHeight(int height)
{
    d->m_minimumHeight = height;
}

void StringAspect::setCompletions(const QStringList &completions)
{
    d->m_completions = completions;
    emit controlConfigurationChanged();
}

void StringAspect::setRightSideIconPath(const FilePath &path)
{
    d->m_rightSideIconPath = path;
}

bool StringAspect::isCheckable() const
{
    return d->m_checkerImpl.isCheckable();
}

Internal::CheckableAspectImplementation &StringAspect::checker()
{
    return d->m_checkerImpl;
}

UndoableValue<QString> &StringAspect::undoableValue()
{
    return d->undoable;
}

std::function<QString(const QString &)> StringAspect::displayFilter() const
{
    return d->m_displayFilter;
}

Qt::TextElideMode StringAspect::elideMode() const
{
    return d->m_elideMode;
}

bool StringAspect::showToolTipOnLabel() const
{
    return d->m_showToolTipOnLabel;
}

int StringAspect::minimumHeight() const
{
    return d->m_minimumHeight;
}

QStringList StringAspect::completions() const
{
    return d->m_completions;
}

FilePath StringAspect::rightSideIconPath() const
{
    return d->m_rightSideIconPath;
}

Key StringAspect::historyCompleterKey() const
{
    return d->m_historyCompleterKey;
}

std::optional<ValidationFunction> StringAspect::validationFunction() const
{
    return d->m_validator;
}

QString StringAspect::validationMessage(const QVariant &candidate) const
{
    return validationMessageFor(d->m_validator, candidate);
}

std::function<QValidator *(QObject *)> StringAspect::validatorFactory() const
{
    return d->m_validatorFactory;
}

bool StringAspect::validatePlaceHolder() const
{
    return d->m_validatePlaceHolder;
}

bool StringAspect::autoApplyOnEditingFinished() const
{
    return d->m_autoApplyOnEditingFinished;
}

bool StringAspect::acceptRichText() const
{
    return d->m_acceptRichText;
}


/*!
    \class Utils::FilePathAspect
    \inmodule QtCreator

    \brief A file path aspect is shallow wrapper around a Utils::StringAspect that
    represents a file in the file system.

    It is displayed by default using Utils::PathChooser.

    The visual representation often contains a label in front of the display
    of the actual value.

    \sa Utils::StringAspect
*/

class Internal::FilePathAspectPrivate
{
public:
    std::function<QString(const QString &)> m_displayFilter;

    QString m_placeHolderText;
    QString m_prompDialogFilter;
    QString m_prompDialogTitle;
    QStringList m_commandVersionArguments;
    Key m_historyCompleterKey;
    PathChooserKind m_expectedKind = PathChooserKind::File;
    Environment m_environment;
    Lazy<FilePath> m_baseDirectory;
    FilePath m_initialBrowsePathBackup;
    StringAspect::ValueAcceptor m_valueAcceptor;
    std::optional<ValidationFunction> m_validator;
    AsyncValidation m_asyncValidation;
    std::optional<FilePath> m_effectiveBinary;
    std::function<void()> m_openTerminal;

    CheckableAspectImplementation m_checkerImpl;

    bool m_showToolTipOnLabel = false;
    bool m_fileDialogOnly = false;
    bool m_autoApplyOnEditingFinished = false;
    bool m_allowPathFromDevice = true;
    bool m_validatePlaceHolder = false;
    bool m_valid = false;
    FilePaths m_valueAlternatives;
    QList<FilePathAspect::Button> m_buttons;

    Guard m_editFinishedGuard;

    UndoableValue<QString> m_undoable;
};

FilePathAspect::FilePathAspect(AspectContainer *container)
    : TypedAspect(container), d(new Internal::FilePathAspectPrivate)
{
    connect(&d->m_undoable.m_signal, &UndoSignaller::changed, this,
            [this] { handleGuiChanged(); });
    setSpan(2, 1); // Default: Label + something

    addDataExtractor(this, &FilePathAspect::value, &Data::value);
    addDataExtractor(this, &FilePathAspect::operator(), &Data::filePath);

    connect(this, &BaseAspect::changed, this, [this] { d->m_effectiveBinary.reset(); });
}

FilePathAspect::~FilePathAspect() = default;

/*!
    Returns the value of this aspect as \c Utils::FilePath.

    \note This simply uses \c FilePath::fromUserInput() for the
    conversion. It does not use any check that the value is actually
    a valid file path.
*/

FilePath FilePathAspect::operator()() const
{
    return expandedValue();
}

FilePath FilePathAspect::expandedValue() const
{
    const QString value = TypedAspect::value();
    if (!value.isEmpty()) {
        if (auto expander = macroExpander())
            return FilePath::fromUserInput(expander->expand(value));
    }
    return FilePath::fromUserInput(value);
}

FilePath FilePathAspect::resolvedVolatileValue() const
{
    return Utils::expandPath(FilePath::fromUserInput(TypedAspect::volatileValue()),
                             macroExpander(),
                             d->m_baseDirectory.value(),
                             d->m_environment,
                             d->m_expectedKind);
}

FilePath FilePathAspect::expandedVolatileValue() const
{
    const QString value = TypedAspect::volatileValue();
    if (!value.isEmpty()) {
        if (auto expander = macroExpander())
            return FilePath::fromUserInput(expander->expand(value));
    }
    return FilePath::fromUserInput(value);
}

/*!
    Returns the full path of the set command. Only makes a difference if
    expected kind is \c Command or \c ExistingCommand and the current
    file path is an executable provided without its path.
    Performs a lookup in PATH if necessary.
 */
FilePath FilePathAspect::effectiveBinary() const
{
    if (d->m_effectiveBinary)
        return *d->m_effectiveBinary;

    d->m_effectiveBinary.emplace(
        Utils::expandPath(expandedValue(), nullptr, {}, {}, d->m_expectedKind));
    return *d->m_effectiveBinary;
}

QString FilePathAspect::value() const
{
    return TypedAspect::value();
}

/*!
    Sets the value of this file path aspect to \a filePath.

    If \a howToAnnounce is set to \c DoEmit, emits the \c valueChanged signal.

    \note This does not use any check that the value is actually
    a file path.
*/

void FilePathAspect::setValue(const FilePath &filePath, Announcement howToAnnounce)
{
    TypedAspect::setValue(filePath.toUserOutput(), howToAnnounce);
}

void FilePathAspect::setValue(const QString &filePath, Announcement howToAnnounce)
{
    TypedAspect::setValue(filePath, howToAnnounce);
}

void FilePathAspect::setDefaultValue(const QString &filePath)
{
    TypedAspect::setDefaultValue(filePath);
}

void FilePathAspect::setDefaultPathValue(const FilePath &filePath)
{
    TypedAspect::setDefaultValue(filePath.toUserOutput());
}

/*!
    Adds a check box with a \a checkerLabel according to \a checkBoxPlacement
    to the line edit.

    The state of the check box is made persistent when using a non-emtpy
    \a checkerKey.
*/
void FilePathAspect::makeCheckable(CheckBoxPlacement checkBoxPlacement,
                                   const QString &checkerLabel,
                                   const Key &checkerKey)
{
    d->m_checkerImpl.makeCheckable(checkBoxPlacement, checkerLabel, checkerKey, this);
}

bool FilePathAspect::isChecked() const
{
    return d->m_checkerImpl.isChecked();
}

void FilePathAspect::setChecked(bool checked)
{
    return d->m_checkerImpl.setChecked(checked);
}

void FilePathAspect::setValueAcceptor(ValueAcceptor &&acceptor)
{
    d->m_valueAcceptor = std::move(acceptor);
}

bool FilePathAspect::isCheckable() const
{
    return d->m_checkerImpl.isCheckable();
}

bool FilePathAspect::guiToVolatileValue()
{
    return updateStorage(m_volatileValue, d->m_undoable.get());
}

bool FilePathAspect::volatileValueToValue()
{
    if (d->m_valueAcceptor) {
        if (const std::optional<QString> tmp = d->m_valueAcceptor(m_value, m_volatileValue))
           return updateStorage(m_value, *tmp);
        return false;
    }
    return updateStorage(m_value, m_volatileValue);
}

bool FilePathAspect::valueToVolatileValue()
{
    const QString val = d->m_displayFilter ? d->m_displayFilter(m_value) : m_value;
    return updateStorage(m_volatileValue, val);
}

void FilePathAspect::volatileValueToGui()
{
    d->m_undoable.setWithoutUndo(m_volatileValue);
    validateInput();
}

bool FilePathAspect::isValid() const
{
    return d->m_valid;
}

void FilePathAspect::setValid(bool valid)
{
    if (d->m_valid == valid)
        return;
    d->m_valid = valid;
    emit validChanged(valid);
}

AsyncValidationFunction FilePathAspect::defaultValidationFunction() const
{
    // The control this ends up in is destroyed with the aspect, so capturing
    // the aspect is as safe as PathChooser capturing itself was.
    return [this](const QString &text) -> AsyncValidationFuture {
        if (text.isEmpty()) {
            return QtFuture::makeReadyFuture(
                AsyncValidationResult(ResultError(Tr::tr("The path must not be empty."))));
        }

        const FilePath expanded = Utils::expandPath(FilePath::fromUserInput(text),
                                                    macroExpander(),
                                                    d->m_baseDirectory.value(),
                                                    d->m_environment,
                                                    d->m_expectedKind);
        if (expanded.isEmpty()) {
            return QtFuture::makeReadyFuture(AsyncValidationResult(
                ResultError(Tr::tr("The path \"%1\" expanded to an empty string.")
                                .arg(expanded.toUserOutput()))));
        }

        return Utils::asyncRun([expanded, kind = d->m_expectedKind] {
            return Utils::validatePath(expanded, kind);
        });
    };
}

void FilePathAspect::addButton(const QString &text, QObject *context,
                               const std::function<void()> &callback)
{
    d->m_buttons.append({text, context, callback});
    emit controlConfigurationChanged();
}

QList<FilePathAspect::Button> FilePathAspect::buttons() const
{
    return d->m_buttons;
}

void FilePathAspect::setVolatileVariantValueFromGui(const QVariant &value)
{
    if (!value.canConvert<QString>())
        return;
    d->m_undoable.set(undoStack(), value.toString());
    handleGuiChanged();
}

AspectPresentation FilePathAspect::presentation() const
{
    AspectPresentation p = TypedAspect::presentation();
    p.control = AspectControls::PathChooser;
    p.placeholderText = d->m_placeHolderText;
    // The two enums are declared in the same order, and the descriptor's exists
    // so that it does not have to include pathvalidation.h.
    p.pathKind = static_cast<AspectControls::PathKind>(expectedKind());
    p.promptDialogTitle = promptDialogTitle();
    p.promptDialogFilter = promptDialogFilter();
    return p;
}

void FilePathAspect::addToLayoutImpl(Layouting::Layout &parent)
{
    // The widget renderer owns this control's construction. Reaching the
    // check means no renderer was installed; see installAspectWidgetRenderer().
    QTC_CHECK(renderAspect(*this, parent));
}

/*!
    \reimp
*/
void FilePathAspect::fromMap(const Store &map)
{
    if (!skipSave())
        setValue(map.value(settingsKey(), defaultValue()).toString(), BeQuiet);
    d->m_checkerImpl.fromMap(map);
}

/*!
    \reimp
*/
void FilePathAspect::toMap(Store &map) const
{
    saveToMap(map, value(), defaultValue(), settingsKey());
    d->m_checkerImpl.toMap(map);
}

void FilePathAspect::volatileToMap(Store &map) const
{
    saveToMap(map, volatileValue(), defaultValue(), settingsKey());
    d->m_checkerImpl.volatileToMap(map);
}

void FilePathAspect::volatileFromMap(const Store &map)
{
    d->m_checkerImpl.volatileFromMap(map);
    if (!skipSave())
        setVolatileValue(map.value(settingsKey(), defaultValue()).toString());
}

void FilePathAspect::setFocusToInputField()
{
    emit controlFocusRequested();
}

void FilePathAspect::setPromptDialogFilter(const QString &filter)
{
    d->m_prompDialogFilter = filter;
    emit controlConfigurationChanged();
}

void FilePathAspect::setPromptDialogTitle(const QString &title)
{
    d->m_prompDialogTitle = title;
    emit controlConfigurationChanged();
}

void FilePathAspect::setCommandVersionArguments(const QStringList &arguments)
{
    d->m_commandVersionArguments = arguments;
    emit controlConfigurationChanged();
}

void FilePathAspect::setAllowPathFromDevice(bool allowPathFromDevice)
{
    d->m_allowPathFromDevice = allowPathFromDevice;
    emit controlConfigurationChanged();
}

void FilePathAspect::setValidatePlaceHolder(bool validatePlaceHolder)
{
    d->m_validatePlaceHolder = validatePlaceHolder;
    emit controlConfigurationChanged();
}

void FilePathAspect::setShowToolTipOnLabel(bool show)
{
    d->m_showToolTipOnLabel = show;
    volatileValueToGui();
}

void FilePathAspect::setAutoApplyOnEditingFinished(bool applyOnEditingFinished)
{
    d->m_autoApplyOnEditingFinished = applyOnEditingFinished;
}

void FilePathAspect::setValueAlternatives(const FilePaths &candidates)
{
    d->m_valueAlternatives = candidates;
    emit controlConfigurationChanged();
}

/*!
  Sets \a expectedKind as expected kind for path chooser displays.

  \sa Utils::PathChooser::setExpectedKind()
*/
void FilePathAspect::setExpectedKind(const PathChooserKind &expectedKind)
{
    if (d->m_expectedKind != expectedKind) {
        d->m_expectedKind = expectedKind;
        d->m_effectiveBinary.reset();
        emit controlConfigurationChanged();
    }
}

void FilePathAspect::setEnvironment(const Environment &env)
{
    d->m_environment = env;
    emit controlConfigurationChanged();
}

void FilePathAspect::setBaseDirectory(const Lazy<FilePath> &baseDirectory)
{
    d->m_baseDirectory = baseDirectory;
    emit controlConfigurationChanged();
}

void FilePathAspect::setInitialBrowsePathBackup(const FilePath &initialBrowsePathBackup)
{
    d->m_initialBrowsePathBackup = initialBrowsePathBackup;
    emit controlConfigurationChanged();
}

void FilePathAspect::setPlaceHolderText(const QString &placeHolderText)
{
    d->m_placeHolderText = placeHolderText;
}

void FilePathAspect::setValidationFunction(const ValidationFunction &validator)
{
    d->m_validator = validator;
    emit controlConfigurationChanged();
}

void FilePathAspect::setDisplayFilter(const std::function<QString (const QString &)> &displayFilter)
{
    d->m_displayFilter = displayFilter;
}

void FilePathAspect::setHistoryCompleter(const Key &historyCompleterKey)
{
    d->m_historyCompleterKey = historyCompleterKey;
    emit controlConfigurationChanged();
}

void FilePathAspect::validateInput()
{
    emit controlValidationRequested();
}

void FilePathAspect::setOpenTerminalHandler(const std::function<void ()> &openTerminal)
{
    d->m_openTerminal = openTerminal;
    emit controlConfigurationChanged();
}

Internal::CheckableAspectImplementation &FilePathAspect::checker()
{
    return d->m_checkerImpl;
}

UndoableValue<QString> &FilePathAspect::undoableValue()
{
    return d->m_undoable;
}

std::function<QString(const QString &)> FilePathAspect::displayFilter() const
{
    return d->m_displayFilter;
}

PathChooserKind FilePathAspect::expectedKind() const
{
    return d->m_expectedKind;
}

Key FilePathAspect::historyCompleterKey() const
{
    return d->m_historyCompleterKey;
}

std::optional<ValidationFunction> FilePathAspect::validationFunction() const
{
    return d->m_validator;
}

QString FilePathAspect::validationMessage(const QVariant &candidate) const
{
    if (const QString message = validationMessageFor(d->m_validator, candidate); !message.isEmpty())
        return message;
    return d->m_asyncValidation.messageFor(d->m_validator, candidate,
                                           const_cast<FilePathAspect *>(this));
}

Environment FilePathAspect::environment() const
{
    return d->m_environment;
}

Lazy<FilePath> FilePathAspect::baseDirectory() const
{
    return d->m_baseDirectory;
}

FilePath FilePathAspect::initialBrowsePathBackup() const
{
    return d->m_initialBrowsePathBackup;
}

std::function<void()> FilePathAspect::openTerminalHandler() const
{
    return d->m_openTerminal;
}

QString FilePathAspect::promptDialogFilter() const
{
    return d->m_prompDialogFilter;
}

QString FilePathAspect::promptDialogTitle() const
{
    return d->m_prompDialogTitle;
}

QStringList FilePathAspect::commandVersionArguments() const
{
    return d->m_commandVersionArguments;
}

bool FilePathAspect::allowPathFromDevice() const
{
    return d->m_allowPathFromDevice;
}

bool FilePathAspect::validatePlaceHolder() const
{
    return d->m_validatePlaceHolder;
}

FilePaths FilePathAspect::valueAlternatives() const
{
    return d->m_valueAlternatives;
}

bool FilePathAspect::autoApplyOnEditingFinished() const
{
    return d->m_autoApplyOnEditingFinished;
}

Guard &FilePathAspect::editFinishedGuard()
{
    return d->m_editFinishedGuard;
}

/*!
    \class Utils::ColorAspect
    \inmodule QtCreator

    \brief A color aspect is a color property of some object, together with
    a description of its behavior for common operations like visualizing or
    persisting.

    The color aspect is displayed using a QtColorButton.
*/

ColorAspect::ColorAspect(AspectContainer *container)
    : TypedAspect(container), d(new Internal::ColorAspectPrivate)
{
    setDefaultValue(QColor::fromRgb(0, 0, 0));
    setSpan(1, 1);
}

ColorAspect::~ColorAspect() = default;

AspectPresentation ColorAspect::presentation() const
{
    AspectPresentation p = TypedAspect::presentation();
    p.control = AspectControls::ColorPicker;
    p.alphaAllowed = d->m_alphaAllowed;
    p.withResetButton = d->m_withResetButton;
    p.minimumSize = d->m_minimumSize;
    return p;
}

void ColorAspect::addToLayoutImpl(Layouting::Layout &parent)
{
    // The widget renderer owns this control's construction. Reaching the
    // check means no renderer was installed; see installAspectWidgetRenderer().
    QTC_CHECK(renderAspect(*this, parent));
}

void ColorAspect::setAlphaAllowed(bool allowed)
{
    d->m_alphaAllowed = allowed;
}

void ColorAspect::setWithResetButton(bool withResetButton)
{
    d->m_withResetButton = withResetButton;
}

void ColorAspect::setMinimumSize(const QSize &size)
{
    d->m_minimumSize = size;
}

/*!
    \class Utils::FontAspect
    \inmodule QtCreator

    \brief A font aspect is a font property of some object, together with
    a description of its behavior for common operations like visualizing or
    persisting.

    The font aspect is displayed using a QFontComboBox.
*/

FontFamilyAspect::FontFamilyAspect(AspectContainer *container)
    : TypedAspect(container), d(new Internal::FontFamilyAspectPrivate)
{
    connect(&d->m_undoable.m_signal, &UndoSignaller::changed, this,
            [this] { handleGuiChanged(); });
    setSpan(2, 1); // Default: Label + Combobox
}

FontFamilyAspect::~FontFamilyAspect() = default;

void FontFamilyAspect::setVolatileVariantValueFromGui(const QVariant &value)
{
    if (!value.canConvert<QString>())
        return;
    d->m_undoable.set(undoStack(), value.toString());
    handleGuiChanged();
}

AspectPresentation FontFamilyAspect::presentation() const
{
    AspectPresentation p = TypedAspect::presentation();
    p.control = AspectControls::FontFamilyPicker;
    p.fontFilters = AspectControls::FontFilters::fromInt(d->m_fontFilters.toInt());
    return p;
}

void FontFamilyAspect::addToLayoutImpl(Layouting::Layout &parent)
{
    // The widget renderer owns this control's construction. Reaching the
    // check means no renderer was installed; see installAspectWidgetRenderer().
    QTC_CHECK(renderAspect(*this, parent));
}

void FontFamilyAspect::setFontFilters(FontFilters fontFilters)
{
    d->m_fontFilters = fontFilters;
}

void FontFamilyAspect::volatileValueToGui()
{
    d->m_undoable.setWithoutUndo(m_volatileValue);
}

bool FontFamilyAspect::guiToVolatileValue()
{
    return updateStorage(m_volatileValue, d->m_undoable.get());
}

bool FontFamilyAspect::valueToVolatileValue()
{
    return updateStorage(m_volatileValue, m_value);
}

bool FontFamilyAspect::volatileValueToValue()
{
    const QString fontFamily = QFontInfo(QFont(m_volatileValue)).family();
    return updateStorage(m_value, fontFamily);
}

bool FontFamilyAspect::isDirty() const
{
    if (TypedAspect::isDirty())
        return true;
    const QString resolved = QFontInfo(QFont(m_value)).family();
    const QString resolvedVolatile = QFontInfo(QFont(m_volatileValue)).family();
    return resolved != resolvedVolatile;
}

void FontFamilyAspect::setDefaultValue(const QString &font)
{
    const QString fontFamily = QFontInfo(QFont(font)).family();
    TypedAspect::setDefaultValue(fontFamily);
}

// !internal

static void updateToggleAction(ToggleAspect &aspect,
                               const std::unique_ptr<Internal::ToggleAspectPrivate> &d)
{
    if (!aspect.hasAction())
        return;

    QAction *action = aspect.action();

    Internal::ToggleAspectPrivate::Data data = aspect.value() ? d->on : d->off;
    if (data.icon.isNull())
        data.icon = aspect() ? aspect.icon() : d->on.icon;
    if (data.text.isEmpty())
        data.text = aspect() ? aspect.toolTip() : d->on.text;
    if (data.tooltip.isEmpty())
        data.tooltip = aspect() ? aspect.toolTip() : d->on.tooltip;

    action->setIcon(data.icon);
    action->setText(data.text);
    action->setToolTip(data.tooltip);
}

/*!
    \class Utils::ToggleAspect
    \inmodule QtCreator

    \brief A toggle aspect is a boolean property of some object, together with
    a description of its behavior for common operations like visualizing or
    persisting. It also contains independent tooltips, icons and text for the action()
    according to the on / off state of the aspect.

    The aspect is displayed using a QCheckBox.

    The visual representation often contains a label in front or after
    the display of the actual checkmark.
*/

ToggleAspect::ToggleAspect(AspectContainer *container)
    : BoolAspect(container)
    , d(std::make_unique<Internal::ToggleAspectPrivate>())
{}

ToggleAspect::~ToggleAspect() {}

void ToggleAspect::setOffIcon(const QIcon &icon)
{
    d->off.icon = icon;
    updateToggleAction(*this, d);
}

void ToggleAspect::setOffTooltip(const QString &tooltip)
{
    d->off.tooltip = tooltip;
    updateToggleAction(*this, d);
}

void ToggleAspect::setOnTooltip(const QString &tooltip)
{
    d->on.tooltip = tooltip;
    updateToggleAction(*this, d);
}

void ToggleAspect::setOnIcon(const QIcon &icon)
{
    d->on.icon = icon;
    updateToggleAction(*this, d);
}

QString ToggleAspect::onTooltip() const
{
    return d->on.tooltip;
}

QIcon ToggleAspect::onIcon() const
{
    return d->on.icon;
}

QString ToggleAspect::offTooltip() const
{
    return d->off.tooltip;
}

QIcon ToggleAspect::offIcon() const
{
    return d->off.icon;
}

void ToggleAspect::setOnText(const QString &text)
{
    d->on.text = text;
}

QString ToggleAspect::onText() const
{
    return d->on.text;
}

void ToggleAspect::setOffText(const QString &text)
{
    d->off.text = text;
}
QString ToggleAspect::offText() const
{
    return d->off.text;
}

void ToggleAspect::announceChanges(Changes changes, Announcement howToAnnounce)
{
    if (changes.valueFromVolatileValue || changes.valueFromOutside)
        updateToggleAction(*this, d);
    BoolAspect::announceChanges(changes, howToAnnounce);
}

QAction *ToggleAspect::action()
{
    if (hasAction())
        return BoolAspect::action();

    QAction *a = BoolAspect::action();
    updateToggleAction(*this, d);

    return a;
}

/*!
    \class Utils::BoolAspect
    \inmodule QtCreator

    \brief A boolean aspect is a boolean property of some object, together with
    a description of its behavior for common operations like visualizing or
    persisting.

    The boolean aspect is displayed using a QCheckBox.

    The visual representation often contains a label in front or after
    the display of the actual checkmark.
*/


BoolAspect::BoolAspect(AspectContainer *container)
    : TypedAspect(container), d(new Internal::BoolAspectPrivate)
{
    connect(&d->m_undoable.m_signal, &UndoSignaller::changed, this,
            [this] { handleGuiChanged(); });
    setDefaultValue(false);
    setSpan(2, 1);
}

/*!
    \internal
*/
BoolAspect::~BoolAspect() = default;



/*!
    \reimp
*/
void BoolAspect::setVolatileVariantValueFromGui(const QVariant &value)
{
    if (!value.canConvert<bool>())
        return;
    d->m_undoable.set(undoStack(), value.toBool());
    handleGuiChanged();
}

AspectPresentation BoolAspect::presentation() const
{
    AspectPresentation p = TypedAspect::presentation();
    p.control = d->m_displayStyle == DisplayStyle::CheckBox ? AspectControls::CheckBox
                                                            : AspectControls::RadioButton;
    switch (d->m_labelPlacement) {
    case LabelPlacement::AtCheckBox:
        p.labelPlacement = AspectControls::LabelPlacement::AtControl;
        break;
    case LabelPlacement::Compact:
        p.labelPlacement = AspectControls::LabelPlacement::Compact;
        break;
    case LabelPlacement::InExtraLabel:
        p.labelPlacement = AspectControls::LabelPlacement::InExtraLabel;
        break;
    case LabelPlacement::ShowTip:
        p.labelPlacement = AspectControls::LabelPlacement::ShowTip;
        break;
    }
    return p;
}

void BoolAspect::addToLayoutImpl(Layouting::Layout &parent)
{
    // The widget renderer owns this control's construction. Reaching the
    // check means no renderer was installed; see installAspectWidgetRenderer().
    QTC_CHECK(renderAspect(*this, parent));
}


QAction *BoolAspect::action()
{
    if (hasAction())
        return TypedAspect::action();
    auto act = TypedAspect::action(); // Creates it.
    act->setCheckable(true);
    act->setChecked(m_value);
    act->setToolTip(toolTip());
    connect(act, &QAction::triggered, this, [this](bool newValue) {
        setValue(newValue);
    });
    connect(this, &BoolAspect::changed, act, [act, this] { act->setChecked(m_value); });

    return act;
}

bool BoolAspect::guiToVolatileValue()
{
    return updateStorage(m_volatileValue, d->m_undoable.get());
}

void BoolAspect::volatileValueToGui()
{
    d->m_undoable.setWithoutUndo(m_volatileValue);
}

void BoolAspect::setLabel(const QString &labelText, LabelPlacement labelPlacement)
{
    TypedAspect::setLabelText(labelText);
    d->m_labelPlacement = labelPlacement;
}

void BoolAspect::setLabelPlacement(BoolAspect::LabelPlacement labelPlacement)
{
    d->m_labelPlacement = labelPlacement;
}

void BoolAspect::setDisplayStyle(DisplayStyle displayStyle)
{
    d->m_displayStyle = displayStyle;
}

CheckableDecider BoolAspect::askAgainCheckableDecider()
{
    return CheckableDecider(
        [this] { return value(); },
        [this] { setValue(false); }
    );
}

CheckableDecider BoolAspect::doNotAskAgainCheckableDecider()
{
    return CheckableDecider(
        [this] { return !value(); },
        [this] { setValue(true); }
    );
}

/*!
 \internal
*/
QVariant InvertedSavedBoolAspect::fromSettingsValue(const QVariant &savedValue) const
{
    return !savedValue.toBool();
}

/*!
 \internal
*/
QVariant InvertedSavedBoolAspect::toSettingsValue(const QVariant &valueToSave) const
{
    return !valueToSave.toBool();
}

/*!
    \class Utils::SelectionAspect
    \inmodule QtCreator

    \brief A selection aspect represents a specific choice out of
    several.

    The selection aspect is displayed using a QComboBox or
    QRadioButtons in a QButtonGroup.
*/

SelectionAspect::SelectionAspect(AspectContainer *container)
    : TypedAspect(container), d(new Internal::SelectionAspectPrivate)
{
    connect(&d->m_undoable.m_signal, &UndoSignaller::changed, this,
            [this] { handleGuiChanged(); });
    setSpan(2, 1);
    d->m_undoable.setSilently(value());
}

/*!
    \internal
*/
SelectionAspect::~SelectionAspect() = default;

/*!
    \reimp
*/
void SelectionAspect::setVolatileVariantValueFromGui(const QVariant &value)
{
    if (!value.canConvert<int>())
        return;
    d->m_undoable.set(undoStack(), value.toInt());
    handleGuiChanged();
}

AspectPresentation SelectionAspect::presentation() const
{
    AspectPresentation p = TypedAspect::presentation();
    p.control = d->m_displayStyle == DisplayStyle::ComboBox ? AspectControls::ComboBox
                                                            : AspectControls::RadioButtonGroup;
    for (const Option &option : std::as_const(d->m_options))
        p.choices.append({option.displayName, option.tooltip, option.enabled, option.itemData});
    return p;
}

void SelectionAspect::addToLayoutImpl(Layouting::Layout &parent)
{
    // The widget renderer owns this control's construction. Reaching the
    // check means no renderer was installed; see installAspectWidgetRenderer().
    QTC_CHECK(renderAspect(*this, parent));
}

bool SelectionAspect::guiToVolatileValue()
{
    return updateStorage(m_volatileValue, d->m_undoable.get());
}

void SelectionAspect::volatileValueToGui()
{
    return d->m_undoable.setWithoutUndo(m_volatileValue);
}

void SelectionAspect::setDisplayStyle(SelectionAspect::DisplayStyle style)
{
    d->m_displayStyle = style;
}

SelectionAspect::DisplayStyle SelectionAspect::displayStyle() const
{
    return d->m_displayStyle;
}

QVariant SelectionAspect::toSettingsValue(const QVariant &valueToSave) const
{
    if (!d->m_useDataAsSavedValue)
        return valueToSave;

    const QVariant iv = itemValueForIndex(valueToSave.toInt());
    return iv.isValid() ? iv : valueToSave;
}

QVariant SelectionAspect::fromSettingsValue(const QVariant &savedValue) const
{
    if (!d->m_useDataAsSavedValue)
        return savedValue;

    const int index = indexForItemValue(savedValue);
    return index >= 0 ? index : savedValue;
}

void SelectionAspect::setUseDataAsSavedValue()
{
    d->m_useDataAsSavedValue = true;
}

void SelectionAspect::setStringValue(const QString &val)
{
    const int index = indexForDisplay(val);
    QTC_ASSERT(index >= 0, return);
    setValue(index);
}

void SelectionAspect::setDefaultValue(int val)
{
    TypedAspect::setDefaultValue(val);
}

// Note: This needs to be set after all options are added.
void SelectionAspect::setDefaultValue(const QString &val)
{
    TypedAspect::setDefaultValue(indexForDisplay(val));
}

QString SelectionAspect::stringValue() const
{
    const int idx = value();
    return idx >= 0 && idx < d->m_options.size() ? d->m_options.at(idx).displayName : QString();
}

QVariant SelectionAspect::itemValue() const
{
    const int idx = value();
    return idx >= 0 && idx < d->m_options.size() ? d->m_options.at(idx).itemData : QVariant();
}

void SelectionAspect::addOption(const QString &displayName, const QString &toolTip)
{
    addOption(Option(displayName, toolTip, {}));
}

void SelectionAspect::addOption(const Option &option)
{
    d->m_options.append(option);
    emit controlConfigurationChanged();
}

void SelectionAspect::clearOptions()
{
    d->m_options.clear();
    emit controlConfigurationChanged();
}

int SelectionAspect::optionCount() const
{
    return d->m_options.size();
}

int SelectionAspect::indexForDisplay(const QString &displayName) const
{
    for (int i = 0, n = d->m_options.size(); i < n; ++i) {
        if (d->m_options.at(i).displayName == displayName)
            return i;
    }
    return -1;
}

QString SelectionAspect::displayForIndex(int index) const
{
    QTC_ASSERT(index >= 0 && index < d->m_options.size(), return {});
    return d->m_options.at(index).displayName;
}

std::optional<SelectionAspect::Option> SelectionAspect::optionForIndex(int index) const
{
    QTC_ASSERT(index >= 0 && index < d->m_options.size(), return {});
    return d->m_options.at(index);
}

void SelectionAspect::setOptionForIndex(int index, const Option &option)
{
    QTC_ASSERT(index >= 0 && index < d->m_options.size(), return);
    d->m_options[index] = option;
}

int SelectionAspect::indexForItemValue(const QVariant &value) const
{
    for (int i = 0, n = d->m_options.size(); i < n; ++i) {
        if (d->m_options.at(i).itemData == value)
            return i;
    }
    return -1;
}

QVariant SelectionAspect::itemValueForIndex(int index) const
{
    QTC_ASSERT(index >= 0 && index < d->m_options.size(), return {});
    return d->m_options.at(index).itemData;
}

/*!
    \class Utils::MultiSelectionAspect
    \inmodule QtCreator

    \brief A multi-selection aspect represents one or more choices out of
    several.

    The multi-selection aspect is displayed using a QListWidget with
    checkable items.
*/

MultiSelectionAspect::MultiSelectionAspect(AspectContainer *container)
    : TypedAspect(container), d(new Internal::MultiSelectionAspectPrivate(this))
{
    connect(&d->m_undoable.m_signal, &UndoSignaller::changed, this,
            [this] { handleGuiChanged(); });
    setDefaultValue(QStringList());
    setSpan(2, 1);
}

/*!
    \internal
*/
MultiSelectionAspect::~MultiSelectionAspect() = default;

void MultiSelectionAspect::setVolatileVariantValueFromGui(const QVariant &value)
{
    if (!value.canConvert<QStringList>())
        return;
    d->m_undoable.set(undoStack(), value.toStringList());
    handleGuiChanged();
}

/*!
    \reimp
*/
AspectPresentation MultiSelectionAspect::presentation() const
{
    AspectPresentation p = TypedAspect::presentation();
    p.control = AspectControls::MultiSelection;
    for (const QString &value : std::as_const(d->m_allValues))
        p.choices.append({value, {}, true, value});
    return p;
}

void MultiSelectionAspect::addToLayoutImpl(Layout &builder)
{
    // The widget renderer owns this control's construction. Reaching the
    // check means no renderer was installed; see installAspectWidgetRenderer().
    QTC_CHECK(renderAspect(*this, builder));
}

bool Internal::MultiSelectionAspectPrivate::setValueSelectedHelper(const QString &val, bool on)
{
    QStringList list = q->value();
    if (on && !list.contains(val)) {
        list.append(val);
        q->setValue(list);
        return true;
    }
    if (!on && list.contains(val)) {
        list.removeOne(val);
        q->setValue(list);
        return true;
    }
    return false;
}

QStringList MultiSelectionAspect::allValues() const
{
    return d->m_allValues;
}

void MultiSelectionAspect::setAllValues(const QStringList &val)
{
    d->m_allValues = val;
}

void MultiSelectionAspect::setDisplayStyle(MultiSelectionAspect::DisplayStyle style)
{
    d->m_displayStyle = style;
}

void MultiSelectionAspect::volatileValueToGui()
{
    d->m_undoable.setWithoutUndo(m_volatileValue);
}

bool MultiSelectionAspect::guiToVolatileValue()
{
    return updateStorage(m_volatileValue, d->m_undoable.get());
}


/*!
    \class Utils::IntegerAspect
    \inmodule QtCreator

    \brief An integer aspect is a integral property of some object, together with
    a description of its behavior for common operations like visualizing or
    persisting.

    The integer aspect is displayed using a \c QSpinBox.

    The visual representation often contains a label in front
    the display of the spin box.
*/

// IntegerAspect

IntegerAspect::IntegerAspect(AspectContainer *container)
    : TypedAspect(container), d(new Internal::IntegerAspectPrivate)
{
    connect(&d->m_undoable.m_signal, &UndoSignaller::changed, this,
            [this] { handleGuiChanged(); });
    setSpan(2, 1);
}

/*!
    \internal
*/
IntegerAspect::~IntegerAspect() = default;

void IntegerAspect::setVolatileVariantValueFromGui(const QVariant &value)
{
    if (!value.canConvert<qint64>())
        return;
    d->m_undoable.set(undoStack(), value.value<qint64>());
    handleGuiChanged();
}

/*!
    \reimp
*/
AspectPresentation IntegerAspect::presentation() const
{
    AspectPresentation p = TypedAspect::presentation();
    p.control = AspectControls::SpinBox;
    if (const std::optional<qint64> m = minimumValue())
        p.minimum = *m;
    if (const std::optional<qint64> m = maximumValue())
        p.maximum = *m;
    p.singleStep = singleStep();
    p.prefix = prefix();
    p.suffix = suffix();
    p.specialValueText = specialValueText();
    p.displayIntegerBase = displayIntegerBase();
    p.displayScaleFactor = displayScaleFactor();
    return p;
}

void IntegerAspect::addToLayoutImpl(Layouting::Layout &parent)
{
    // The widget renderer owns this control's construction. Reaching the
    // check means no renderer was installed; see installAspectWidgetRenderer().
    QTC_CHECK(renderAspect(*this, parent));
}

bool IntegerAspect::guiToVolatileValue()
{
    return updateStorage(m_volatileValue, d->m_undoable.get());
}

void IntegerAspect::volatileValueToGui()
{
    d->m_undoable.setWithoutUndo(m_volatileValue);
}

QVariant IntegerAspect::fromSettingsValue(const QVariant &savedValue) const
{
    qint64 v = savedValue.value<qint64>();
    if (d->m_minimumValue && v < *d->m_minimumValue)
        v = *d->m_minimumValue;
    if (d->m_maximumValue && v > *d->m_maximumValue)
        v = *d->m_maximumValue;
    return v;
}

void IntegerAspect::setRange(qint64 min, qint64 max)
{
    d->m_minimumValue = min;
    d->m_maximumValue = max;
}

std::optional<qint64> IntegerAspect::minimumValue() const
{
    return d->m_minimumValue;
}

std::optional<qint64> IntegerAspect::maximumValue() const
{
    return d->m_maximumValue;
}

qint64 IntegerAspect::singleStep() const
{
    return d->m_singleStep;
}

QString IntegerAspect::prefix() const
{
    return d->m_prefix;
}

QString IntegerAspect::suffix() const
{
    return d->m_suffix;
}

QString IntegerAspect::specialValueText() const
{
    return d->m_specialValueText;
}

int IntegerAspect::displayIntegerBase() const
{
    return d->m_displayIntegerBase;
}

qint64 IntegerAspect::displayScaleFactor() const
{
    return d->m_displayScaleFactor;
}

void IntegerAspect::setLabel(const QString &label)
{
    setLabelText(label);
}

void IntegerAspect::setPrefix(const QString &prefix)
{
    d->m_prefix = prefix;
}

void IntegerAspect::setSuffix(const QString &suffix)
{
    d->m_suffix = suffix;
}

void IntegerAspect::setDisplayIntegerBase(int base)
{
    d->m_displayIntegerBase = base;
}

void IntegerAspect::setDisplayScaleFactor(qint64 factor)
{
    d->m_displayScaleFactor = factor;
}

void IntegerAspect::setSpecialValueText(const QString &specialText)
{
    d->m_specialValueText = specialText;
}

void IntegerAspect::setSingleStep(qint64 step)
{
    d->m_singleStep = step;
}


/*!
    \class Utils::DoubleAspect
    \inmodule QtCreator

    \brief An double aspect is a numerical property of some object, together with
    a description of its behavior for common operations like visualizing or
    persisting.

    The double aspect is displayed using a \c QDoubleSpinBox.

    The visual representation often contains a label in front
    the display of the spin box.
*/

DoubleAspect::DoubleAspect(AspectContainer *container)
    : TypedAspect(container), d(new Internal::DoubleAspectPrivate)
{
    connect(&d->m_undoable.m_signal, &UndoSignaller::changed, this,
            [this] { handleGuiChanged(); });
    setDefaultValue(double(0));
    setSpan(2, 1);
}

/*!
    \internal
*/
DoubleAspect::~DoubleAspect() = default;

void DoubleAspect::setVolatileVariantValueFromGui(const QVariant &value)
{
    if (!value.canConvert<double>())
        return;
    d->m_undoable.set(undoStack(), value.toDouble());
    handleGuiChanged();
}

/*!
    \reimp
*/
AspectPresentation DoubleAspect::presentation() const
{
    AspectPresentation p = TypedAspect::presentation();
    p.control = AspectControls::DoubleSpinBox;
    if (const std::optional<double> m = minimumValue())
        p.minimum = *m;
    if (const std::optional<double> m = maximumValue())
        p.maximum = *m;
    p.singleStep = singleStep();
    p.prefix = prefix();
    p.suffix = suffix();
    p.specialValueText = specialValueText();
    return p;
}

void DoubleAspect::addToLayoutImpl(Layout &builder)
{
    // The widget renderer owns this control's construction. Reaching the
    // check means no renderer was installed; see installAspectWidgetRenderer().
    QTC_CHECK(renderAspect(*this, builder));
}

bool DoubleAspect::guiToVolatileValue()
{
    return updateStorage(m_volatileValue, d->m_undoable.get());
}

void DoubleAspect::volatileValueToGui()
{
    d->m_undoable.setWithoutUndo(m_volatileValue);
}

void DoubleAspect::setRange(double min, double max)
{
    d->m_minimumValue = min;
    d->m_maximumValue = max;
}

std::optional<double> DoubleAspect::minimumValue() const
{
    return d->m_minimumValue;
}

std::optional<double> DoubleAspect::maximumValue() const
{
    return d->m_maximumValue;
}

double DoubleAspect::singleStep() const
{
    return d->m_singleStep;
}

QString DoubleAspect::prefix() const
{
    return d->m_prefix;
}

QString DoubleAspect::suffix() const
{
    return d->m_suffix;
}

QString DoubleAspect::specialValueText() const
{
    return d->m_specialValueText;
}

void DoubleAspect::setPrefix(const QString &prefix)
{
    d->m_prefix = prefix;
}

void DoubleAspect::setSuffix(const QString &suffix)
{
    d->m_suffix = suffix;
}

void DoubleAspect::setSpecialValueText(const QString &specialText)
{
    d->m_specialValueText = specialText;
}

void DoubleAspect::setSingleStep(double step)
{
    d->m_singleStep = step;
}


/*!
    \class Utils::TriStateAspect
    \inmodule QtCreator

    \brief A tristate aspect is a property of some object that can have
    three values: enabled, disabled, and unspecified.

    Its visual representation is a QComboBox with three items.
*/

TriStateAspect::TriStateAspect(AspectContainer *container,
                               const QString &enabledDisplay,
                               const QString &disabledDisplay,
                               const QString &defaultDisplay)
    : SelectionAspect(container)
{
    setDisplayStyle(DisplayStyle::ComboBox);
    setDefaultValue(TriState::Default);
    SelectionAspect::addOption({});
    SelectionAspect::addOption({});
    SelectionAspect::addOption({});
    setOptionText(TriState::EnabledValue, enabledDisplay);
    setOptionText(TriState::DisabledValue, disabledDisplay);
    setOptionText(TriState::DefaultValue, defaultDisplay);
}

void TriStateAspect::setUseCheckBox(bool useCheckBox)
{
    m_useCheckBox = useCheckBox;
}

AspectPresentation TriStateAspect::presentation() const
{
    AspectPresentation p = SelectionAspect::presentation();
    if (m_useCheckBox)
        p.control = AspectControls::TriStateCheckBox;
    return p;
}

static QString defaultTristateDisplay(TriState::Value tristate)
{
    switch (tristate) {
        case TriState::EnabledValue: return Tr::tr("Enable");
        case TriState::DisabledValue: return Tr::tr("Disable");
        case TriState::DefaultValue: return Tr::tr("Default");
    }
    QTC_CHECK(false);
    return {};
}

void TriStateAspect::setOptionText(const TriState::Value tristate, const QString &display)
{
    d->m_options[tristate].displayName = display.isEmpty()
        ? defaultTristateDisplay(tristate) : display;
}

TriState TriStateAspect::value() const
{
    return TriState::fromInt(SelectionAspect::value());
}

void TriStateAspect::setValue(TriState value)
{
    SelectionAspect::setValue(value.toInt());
}

TriState TriStateAspect::defaultValue() const
{
    return TriState::fromInt(SelectionAspect::defaultValue());
}

void TriStateAspect::setDefaultValue(TriState value)
{
    SelectionAspect::setDefaultValue(value.toInt());
}

const TriState TriState::Enabled{TriState::EnabledValue};
const TriState TriState::Disabled{TriState::DisabledValue};
const TriState TriState::Default{TriState::DefaultValue};

TriState TriState::fromVariant(const QVariant &variant)
{
    return fromInt(variant.toInt());
}

TriState TriState::fromInt(int v)
{
    QTC_ASSERT(v == EnabledValue || v == DisabledValue || v == DefaultValue, v = DefaultValue);
    return TriState(Value(v));
}


/*!
    \class Utils::StringListAspect
    \inmodule QtCreator

    \brief A string list aspect represents a property of some object
    that is a list of strings.
*/

StringListAspect::StringListAspect(AspectContainer *container)
    : TypedAspect(container), d(new Internal::StringListAspectPrivate)
{
    connect(&d->m_undoable.m_signal, &UndoSignaller::changed, this,
            [this] { handleGuiChanged(); });
    setDefaultValue(QStringList());
}

/*!
    \internal
*/
StringListAspect::~StringListAspect() = default;

bool StringListAspect::guiToVolatileValue()
{
    const QStringList newValue = d->m_undoable.get();
    if (newValue != m_volatileValue) {
        m_volatileValue = newValue;
        return true;
    }
    return false;
}

void StringListAspect::volatileValueToGui()
{
    d->m_undoable.setWithoutUndo(m_volatileValue);
}

void StringListAspect::setDisplayStyle(DisplayStyle displayStyle)
{
    d->m_displayStyle = displayStyle;
}

void StringListAspect::setVolatileVariantValueFromGui(const QVariant &value)
{
    if (!value.canConvert<QStringList>())
        return;
    d->m_undoable.set(undoStack(), value.toStringList());
    handleGuiChanged();
}

AspectPresentation StringListAspect::presentation() const
{
    AspectPresentation p = TypedAspect::presentation();
    p.control = d->m_displayStyle == DisplayStyle::CommaSeparatedLineEdit
                    ? AspectControls::CommaSeparatedLineEdit
                    : AspectControls::StringList;
    p.allowAdding = d->m_allowAdding;
    p.allowRemoving = d->m_allowRemoving;
    p.allowEditing = d->m_allowEditing;
    return p;
}

void StringListAspect::addToLayoutImpl(Layout &parent)
{
    // The widget renderer owns this control's construction. Reaching the
    // check means no renderer was installed; see installAspectWidgetRenderer().
    QTC_CHECK(renderAspect(*this, parent));
}

void StringListAspect::appendValue(const QString &s, bool allowDuplicates)
{
    QStringList val = value();
    if (allowDuplicates || !val.contains(s))
        val.append(s);
    setValue(val);
}

void StringListAspect::removeValue(const QString &s)
{
    QStringList val = value();
    val.removeAll(s);
    setValue(val);
}

void StringListAspect::appendValues(const QStringList &values, bool allowDuplicates)
{
    QStringList val = value();
    for (const QString &s : values) {
        if (allowDuplicates || !val.contains(s))
            val.append(s);
    }
    setValue(val);
}

void StringListAspect::removeValues(const QStringList &values)
{
    QStringList val = value();
    for (const QString &s : values)
        val.removeAll(s);
    setValue(val);
}

void StringListAspect::setUiAllowAdding(bool allowAdding)
{
    d->m_allowAdding = allowAdding;
}
void StringListAspect::setUiAllowRemoving(bool allowRemoving)
{
    d->m_allowRemoving = allowRemoving;
}
void StringListAspect::setUiAllowEditing(bool allowEditing)
{
    d->m_allowEditing = allowEditing;
}

bool StringListAspect::uiAllowAdding() const
{
    return d->m_allowAdding;
}
bool StringListAspect::uiAllowRemoving() const
{
    return d->m_allowRemoving;
}
bool StringListAspect::uiAllowEditing() const
{
    return d->m_allowEditing;
}

UndoableValue<QStringList> &StringListAspect::undoableValue()
{
    return d->m_undoable;
}

/*!
    \class Utils::FilePathListAspect
    \inmodule QtCreator

    \brief A filepath list aspect represents a property of some object
    that is a list of filepathList.
*/

FilePathListAspect::FilePathListAspect(AspectContainer *container)
    : TypedAspect(container)
    , d(new Internal::FilePathListAspectPrivate)
{
    connect(&d->undoable.m_signal, &UndoSignaller::changed, this,
            [this] { handleGuiChanged(); });
    setDefaultValue(QStringList());
}

FilePathListAspect::~FilePathListAspect() = default;

FilePaths FilePathListAspect::operator()() const
{
    return Utils::transform(m_value, [expander = macroExpander()](const QString &f) {
        if (expander)
            return FilePath::fromUserInput(expander->expand(f));
        return FilePath::fromUserInput(f);
    });
}

bool FilePathListAspect::guiToVolatileValue()
{
    const QStringList newValue = d->undoable.get();
    if (newValue != m_volatileValue) {
        m_volatileValue = newValue;
        return true;
    }
    return false;
}

void FilePathListAspect::volatileValueToGui()
{
    d->undoable.setWithoutUndo(m_volatileValue);
}

void FilePathListAspect::setVolatileVariantValueFromGui(const QVariant &value)
{
    if (!value.canConvert<QStringList>())
        return;
    d->undoable.set(undoStack(), value.toStringList());
    handleGuiChanged();
}

AspectPresentation FilePathListAspect::presentation() const
{
    AspectPresentation p = TypedAspect::presentation();
    p.control = AspectControls::FilePathList;
    p.placeholderText = d->placeHolderText;
    return p;
}

void FilePathListAspect::addToLayoutImpl(Layout &parent)
{
    // The widget renderer owns this control's construction. Reaching the
    // check means no renderer was installed; see installAspectWidgetRenderer().
    QTC_CHECK(renderAspect(*this, parent));
}

void FilePathListAspect::setPlaceHolderText(const QString &placeHolderText)
{
    if (placeHolderText == d->placeHolderText)
        return;

    d->placeHolderText = placeHolderText;
    emit placeHolderTextChanged(placeHolderText);
}

QString FilePathListAspect::placeHolderText() const
{
    return d->placeHolderText;
}

void FilePathListAspect::appendValue(const FilePath &path, bool allowDuplicates)
{
    const QString asString = path.toUserOutput();
    QStringList val = value();
    if (allowDuplicates || !val.contains(asString))
        val.append(asString);
    setValue(val);
}

void FilePathListAspect::removeValue(const FilePath &s)
{
    QStringList val = value();
    val.removeAll(s.toUserOutput());
    setValue(val);
}

void FilePathListAspect::appendValues(const FilePaths &paths, bool allowDuplicates)
{
    QStringList val = value();

    for (const FilePath &path : paths) {
        const QString asString = path.toUserOutput();
        if (allowDuplicates || !val.contains(asString))
            val.append(asString);
    }
    setValue(val);
}

void FilePathListAspect::removeValues(const FilePaths &paths)
{
    QStringList val = value();
    for (const FilePath &path : paths)
        val.removeAll(path.toUserOutput());
    setValue(val);
}

/*!
    \class Utils::IntegersAspect
    \internal
    \inmodule QtCreator

    \brief An integers aspect represents a property of some object
    that is a list of integers.
*/

IntegersAspect::IntegersAspect(AspectContainer *container)
    : TypedAspect(container)
{}

/*!
    \internal
*/
IntegersAspect::~IntegersAspect() = default;

/*!
    \reimp
*/
AspectPresentation IntegersAspect::presentation() const
{
    AspectPresentation p = TypedAspect::presentation();
    p.control = AspectControls::IntegerList;
    return p;
}

void IntegersAspect::addToLayoutImpl(Layouting::Layout &parent)
{
    // The widget renderer owns this control's construction. Reaching the
    // check means no renderer was installed; see installAspectWidgetRenderer().
    QTC_CHECK(renderAspect(*this, parent));
}


/*!
    \class Utils::TextDisplay
    \inmodule QtCreator

    \brief A text display is a phony aspect with the sole purpose of showing a
    short message, optionally with an icon, in places where otherwise more
    expensive Utils::StringAspect items would be used.

    A text display does not have a real value.
*/

/*!
    Constructs a text display with the parent \a container. The display shows
    \a message and an icon representing the type \a type.
 */
ContainerAspect::~ContainerAspect()
{
    if (m_owned) {
        delete m_container;
        m_container = nullptr;
    }
}

AspectPresentation ContainerAspect::presentation() const
{
    AspectPresentation p = BaseAspect::presentation();
    // Nothing of its own: the page draws what it hands over, and a generic
    // form cannot. Saying so keeps it out of the census's list of holes.
    p.control = AspectControls::Invisible;
    return p;
}

AspectContainer *ContainerAspect::container() const
{
    return m_container;
}

void ContainerAspect::setOwnedContainer(AspectContainer *container)
{
    handOver(container, true);
}

void ContainerAspect::setContainer(AspectContainer *container)
{
    handOver(container, false);
}

bool ContainerAspect::isDirty() const
{
    return m_container && m_container->isDirty();
}

void ContainerAspect::handOver(AspectContainer *container, bool owned)
{
    if (m_container == container) {
        m_owned = owned;
        return;
    }

    if (m_container)
        disconnect(m_container, nullptr, this, nullptr);
    if (m_owned)
        delete m_container;

    m_container = container;
    m_owned = owned;
    if (m_container) {
        // Only what the aspect owns is parented to it: a settings singleton
        // handed over here outlives every page that shows it.
        if (m_owned)
            m_container->setParent(this);
        // What the page shows is what it is dirty about, and the page's own
        // container is what the preferences dialog watches.
        connect(m_container, &BaseAspect::volatileValueChanged,
                this, &BaseAspect::volatileValueChanged);
    }
    emit containerChanged();
}

TextDisplay::TextDisplay(AspectContainer *container, const QString &message)
    : BaseAspect(container), d(new Internal::TextDisplayPrivate)
{
    d->m_message = message;
}

/*!
    \internal
*/
TextDisplay::~TextDisplay() = default;

/*!
    \reimp
*/
AspectPresentation TextDisplay::presentation() const
{
    AspectPresentation p = BaseAspect::presentation();
    p.control = AspectControls::Label;
    p.infoType = d->m_type;
    p.wordWrap = d->m_wordWrap;
    p.textFormat = d->m_textFormat;
    return p;
}

/*!
    \class Utils::ActionAspect
    \inmodule QtCreator

    \brief A button on a page, and the thing it does.

    Holds no value. A page action is an aspect like everything else on the page,
    so it is reachable by name from the page's QML and drawable by either
    renderer.
*/

ActionAspect::ActionAspect(AspectContainer *container)
    : BaseAspect(container), d(new Internal::ActionAspectPrivate)
{}

ActionAspect::~ActionAspect() = default;

void ActionAspect::setActionText(const QString &text)
{
    if (d->m_actionText == text)
        return;
    d->m_actionText = text;
    // The label is part of what a renderer drew, so say it has changed.
    emit controlConfigurationChanged();
}

void ActionAspect::setAction(const std::function<void()> &action)
{
    d->m_action = action;
}

void ActionAspect::setOnShown(const std::function<void()> &onShown)
{
    d->m_onShown = onShown;
}

void ActionAspect::setChoices(const QList<AspectPresentation::Choice> &choices)
{
    d->m_choices = choices;
    emit controlConfigurationChanged();
}

void ActionAspect::setOnChoice(const std::function<void(const QVariant &)> &onChoice)
{
    d->m_onChoice = onChoice;
}

void ActionAspect::triggerChoice(const QVariant &id)
{
    QTC_ASSERT(d->m_onChoice, return);
    d->m_onChoice(id);
}

void ActionAspect::triggerAction()
{
    QTC_ASSERT(d->m_action, return);
    d->m_action();
}

void ActionAspect::requestDisplayText()
{
    if (d->m_onShown)
        d->m_onShown();
}

AspectPresentation ActionAspect::presentation() const
{
    AspectPresentation p = BaseAspect::presentation();
    p.control = AspectControls::Button;
    p.actionText = d->m_actionText;
    p.choices = d->m_choices;
    return p;
}

void ActionAspect::addToLayoutImpl(Layout &parent)
{
    // As TextDisplay: the widget renderer owns this control's construction.
    QTC_CHECK(renderAspect(*this, parent));
}

void TextDisplay::addToLayoutImpl(Layout &parent)
{
    // The widget renderer owns this control's construction. Reaching the
    // check means no renderer was installed; see installAspectWidgetRenderer().
    QTC_CHECK(renderAspect(*this, parent));
}

/*!
    Sets \a t as the information label type for the visual representation
    of this aspect.
 */
void TextDisplay::setIconType(InfoType type)
{
    d->m_type = type;
    emit controlConfigurationChanged();
}

/*!
    Sets how the message is to be read - as plain text, as rich text, or as
    \a format. Markdown is never detected from the text itself, so a message
    written in it says so here.
*/
void TextDisplay::setTextFormat(AspectControls::TextFormat format)
{
    d->m_textFormat = format;
    emit controlConfigurationChanged();
}

void TextDisplay::setText(const QString &message)
{
    d->m_message = message;
    emit changed();
    emit displayTextChanged();
}

QString TextDisplay::displayText() const
{
    return d->m_message;
}

void TextDisplay::activateLink(const QString &link)
{
    emit linkActivated(link);
}

void TextDisplay::setWordWrap(bool on)
{
    d->m_wordWrap = on;
    emit controlConfigurationChanged();
}

QString TextDisplay::text() const
{
    return d->m_message;
}

/*!
    \class Utils::AspectContainer
    \inmodule QtCreator

    \brief The AspectContainer class wraps one or more aspects while providing
    the interface of a single aspect.

    Sub-aspects ownership can be declared using \a setOwnsSubAspects.
*/

class Internal::AspectContainerPrivate
{
public:
    QList<BaseAspect *> m_items; // Both owned and non-owned.
    QList<BaseAspect *> m_ownedItems; // Owned only.
    QStringList m_settingsGroup;
    std::shared_ptr<void> m_backendData;
    QUrl m_qmlSource;
};

#ifdef WITH_TESTS
static QList<AspectContainer *> &aspectContainerRegistry()
{
    // Intentionally leaked. Containers held in statics unregister from their
    // destructor, and the order of static destruction across libraries is
    // unspecified, so the registry must outlive every possible user.
    static QList<AspectContainer *> *registry = new QList<AspectContainer *>;
    return *registry;
}

const QList<AspectContainer *> &AspectContainer::registeredContainers()
{
    return aspectContainerRegistry();
}
#endif

AspectContainer::AspectContainer(AspectContainer *parentContainer)
    : BaseAspect(parentContainer)
    , d(new Internal::AspectContainerPrivate)
{
#ifdef WITH_TESTS
    aspectContainerRegistry().append(this);
#endif
}

/*!
    \internal
*/
AspectContainer::~AspectContainer()
{
#ifdef WITH_TESTS
    aspectContainerRegistry().removeOne(this);
#endif
    qDeleteAll(d->m_ownedItems);
}

AspectPresentation AspectContainer::presentation() const
{
    AspectPresentation p = BaseAspect::presentation();
    p.control = AspectControls::Container;
    return p;
}

void AspectContainer::addToLayoutImpl(Layouting::Layout &parent)
{
    // The widget renderer owns this control's construction. Reaching the
    // check means no renderer was installed; see installAspectWidgetRenderer().
    QTC_CHECK(renderAspect(*this, parent));
}

/*!
    \internal
*/
void AspectContainer::registerAspect(BaseAspect *aspect, bool takeOwnership)
{
    aspect->setContainer(this);
    aspect->setAutoApply(isAutoApply());
    aspect->setEnabled(aspect->isEnabled() && isEnabled());
    d->m_items.append(aspect);
    if (takeOwnership)
        d->m_ownedItems.append(aspect);

    connect(aspect, &BaseAspect::changed, this, &BaseAspect::changed);
    connect(aspect, &BaseAspect::changed, this, [this, aspect] { emit subAspectChanged(aspect); });
    connect(aspect, &BaseAspect::volatileValueChanged, this, &BaseAspect::volatileValueChanged);
}

void AspectContainer::registerAspects(const AspectContainer &aspects)
{
    for (BaseAspect *aspect : std::as_const(aspects.d->m_items))
        registerAspect(aspect);
}

/*!
    Retrieves a BaseAspect with a given \a id, or nullptr if no such aspect is contained.

    \sa BaseAspect
*/
BaseAspect *AspectContainer::aspect(Id id) const
{
    return findOrDefault(d->m_items, equal(&BaseAspect::id, id));
}

AspectContainer::const_iterator AspectContainer::begin() const
{
    return d->m_items.cbegin();
}

AspectContainer::const_iterator AspectContainer::end() const
{
    return d->m_items.cend();
}

void AspectContainer::setQmlSource(const QUrl &qmlSource)
{
    d->m_qmlSource = qmlSource;
}

QUrl AspectContainer::qmlSource() const
{
    return d->m_qmlSource;
}

void AspectContainer::setBackendData(const std::shared_ptr<void> &data)
{
    d->m_backendData = data;
}

std::shared_ptr<void> AspectContainer::backendData() const
{
    return d->m_backendData;
}

const QList<BaseAspect *> &AspectContainer::aspects() const
{
    return d->m_items;
}

void AspectContainer::fromMap(const Store &map)
{
    for (BaseAspect *aspect : std::as_const(d->m_items))
        aspect->fromMap(map);

    emit fromMapFinished();
}

void AspectContainer::toMap(Store &map) const
{
    for (BaseAspect *aspect : std::as_const(d->m_items))
        aspect->toMap(map);
}

void AspectContainer::volatileToMap(Store &map) const
{
    for (BaseAspect *aspect : std::as_const(d->m_items))
        aspect->volatileToMap(map);
}

void AspectContainer::volatileFromMap(const Store &map)
{
    for (BaseAspect *aspect : std::as_const(d->m_items))
        aspect->volatileFromMap(map);
}

void AspectContainer::readSettings()
{
    const SettingsGroupNester nester(d->m_settingsGroup);
    for (BaseAspect *aspect : std::as_const(d->m_items))
        aspect->readSettings();
}

void AspectContainer::writeSettings() const
{
    const SettingsGroupNester nester(d->m_settingsGroup);
    for (BaseAspect *aspect : std::as_const(d->m_items))
        aspect->writeSettings();
}

void AspectContainer::volatileValueToGui()
{
    for (BaseAspect *aspect : std::as_const(d->m_items))
        aspect->volatileValueToGui();
}

bool AspectContainer::guiToVolatileValue()
{
    bool result = true;
    for (BaseAspect *aspect : std::as_const(d->m_items)) {
        if (!aspect->guiToVolatileValue())
            result = false;
    }
    return result;
}

void AspectContainer::setSettingsGroup(const QString &groupKey)
{
    d->m_settingsGroup = QStringList{groupKey};
}

void AspectContainer::setSettingsGroups(const QString &groupKey, const QString &subGroupKey)
{
    d->m_settingsGroup = QStringList{groupKey, subGroupKey};
}

QStringList AspectContainer::settingsGroups() const
{
    return d->m_settingsGroup;
}

void AspectContainer::apply()
{
    const bool willChange = isDirty();

    for (BaseAspect *aspect : std::as_const(d->m_items))
        aspect->apply();

    emit applied();

    if (willChange)
        emit changed();
}

void AspectContainer::cancel()
{
    for (BaseAspect *aspect : std::as_const(d->m_items))
        aspect->cancel();
}

void AspectContainer::reset()
{
    for (BaseAspect *aspect : std::as_const(d->m_items))
        aspect->setVariantValue(aspect->defaultVariantValue());
}

void AspectContainer::setAutoApply(bool on)
{
    BaseAspect::setAutoApply(on);

    for (BaseAspect *aspect : std::as_const(d->m_items))
        aspect->setAutoApply(on);
}

bool AspectContainer::isDirty() const
{
    for (BaseAspect *aspect : std::as_const(d->m_items)) {
        if (aspect->isDirty())
            return true;
    }
    return false;
}

void AspectContainer::setUndoStack(QUndoStack *undoStack)
{
    BaseAspect::setUndoStack(undoStack);

    for (BaseAspect *aspect : std::as_const(d->m_items))
        aspect->setUndoStack(undoStack);
}

void AspectContainer::setEnabled(bool enabled)
{
    BaseAspect::setEnabled(enabled);

    for (BaseAspect *aspect : std::as_const(d->m_items))
        aspect->setEnabled(enabled);
}

bool AspectContainer::equals(const AspectContainer &other) const
{
    // FIXME: Expensive, but should not really be needed in a fully aspectified world.
    Store thisMap, thatMap;
    toMap(thisMap);
    other.toMap(thatMap);
    return thisMap == thatMap;
}

void AspectContainer::copyFrom(const AspectContainer &other)
{
    Store map;
    other.toMap(map);
    fromMap(map);
}

void AspectContainer::forEachAspect(const std::function<void(BaseAspect *)> &run) const
{
    for (BaseAspect *aspect : std::as_const(d->m_items)) {
        if (auto container = dynamic_cast<AspectContainer *>(aspect))
            container->forEachAspect(run);
        else
            run(aspect);
    }
}

BaseAspect::Data::Ptr BaseAspect::extractData() const
{
    QTC_ASSERT(d->m_dataCreator, return {});
    Data *data = d->m_dataCreator();
    data->m_classId = metaObject();
    data->m_id = id();
    data->m_cloner = d->m_dataCloner;
    for (const DataExtractor &extractor : std::as_const(d->m_dataExtractors))
        extractor(data);
    return Data::Ptr(data);
}

/*
    Mirrors the internal volatile value to the GUI element if they are already
    created.

    No-op otherwise.
*/
void BaseAspect::volatileValueToGui()
{
}

/*
    Mirrors the data stored in GUI element if they are already created to
    the internal volatile value.

    No-op otherwise.

    \return true when the buffered volatile value changed.
*/
bool BaseAspect::guiToVolatileValue()
{
    return false;
}

/*
    Mirrors buffered volatile value to the internal value.
    This function is used for \c apply().

    \return true when the internal value changed.
*/

bool BaseAspect::volatileValueToValue()
{
    return false;
}

bool BaseAspect::valueToVolatileValue()
{
    return false;
}

void BaseAspect::handleGuiChanged()
{
    if (guiToVolatileValue())
        emit volatileValueChanged();
    if (isAutoApply())
        apply();
}

void BaseAspect::addDataExtractorHelper(const DataExtractor &extractor) const
{
    d->m_dataExtractors.append(extractor);
}

void BaseAspect::setDataCreatorHelper(const DataCreator &creator) const
{
    d->m_dataCreator = creator;
}

void BaseAspect::setDataClonerHelper(const DataCloner &cloner) const
{
    d->m_dataCloner = cloner;
}

const BaseAspect::Data *AspectContainerData::aspect(Id instanceId) const
{
    for (const BaseAspect::Data::Ptr &data : m_data) {
        if (data.get()->id() == instanceId)
            return data.get();
    }
    return nullptr;
}

const BaseAspect::Data *AspectContainerData::aspect(BaseAspect::Data::ClassId classId) const
{
    for (const BaseAspect::Data::Ptr &data : m_data) {
        if (data.get()->classId() == classId)
            return data.get();
    }
    return nullptr;
}

void AspectContainerData::append(const BaseAspect::Data::Ptr &data)
{
    m_data.append(data);
}

// BaseAspect::Data

BaseAspect::Data::~Data() = default;

void BaseAspect::Data::Ptr::operator=(const Ptr &other)
{
    if (this == &other)
        return;
    delete m_data;
    m_data = other.m_data->clone();
}

// SettingsGroupNester

SettingsGroupNester::SettingsGroupNester(const QStringList &groups)
    : m_groupCount(groups.size())
{
    for (const QString &group : groups)
        Utils::userSettings().beginGroup(keyFromString(group));
}

SettingsGroupNester::~SettingsGroupNester()
{
    for (int i = 0; i != m_groupCount; ++i)
        Utils::userSettings().endGroup();
}

StringSelectionAspect::StringSelectionAspect(AspectContainer *container)
    : TypedAspect<QString>(container)
{}

QStandardItem *StringSelectionAspect::itemById(const QString &id)
{
    connect(&m_undoable.m_signal, &UndoSignaller::changed, this,
            [this] { handleGuiChanged(); });
    for (int i = 0; i < m_model->rowCount(); ++i) {
        auto cur = m_model->item(i);
        if (cur->data() == id)
            return cur;
    }

    return nullptr;
}

void StringSelectionAspect::volatileValueToGui()
{
    if (!m_model) {
        m_undoable.setSilently(m_volatileValue);
        return;
    }

    auto selected = itemById(m_volatileValue);
    if (selected) {
        m_undoable.setSilently(selected->data().toString());
        m_selectionModel->setCurrentIndex(selected->index(),
                                          QItemSelectionModel::SelectionFlag::ClearAndSelect);
        return;
    }

    if (m_model->rowCount() > 0) {
        m_undoable.setSilently(m_model->item(0)->data().toString());
        m_selectionModel->setCurrentIndex(m_model->item(0)->index(),
                                          QItemSelectionModel::SelectionFlag::ClearAndSelect);
    } else {
        m_undoable.setSilently(m_volatileValue);
        m_selectionModel->setCurrentIndex(QModelIndex(), QItemSelectionModel::SelectionFlag::Clear);
    }

    handleGuiChanged();
}

bool StringSelectionAspect::guiToVolatileValue()
{
    if (!m_model)
        return false;

    auto oldBuffer = m_volatileValue;

    m_volatileValue = m_undoable.get();

    return oldBuffer != m_volatileValue;
}

void StringSelectionAspect::setVolatileVariantValueFromGui(const QVariant &value)
{
    if (!value.canConvert<QString>())
        return;
    m_undoable.set(undoStack(), value.toString());
    handleGuiChanged();
}

void StringSelectionAspect::ensureFilled() const
{
    if (m_model)
        return;
    // No callback is nothing to fill, not a mistake: presentation() asks every
    // time a form is built, and the renderer is where an aspect that needs one
    // and lacks it gets complained about.
    if (!m_fillCallback)
        return;

    auto self = const_cast<StringSelectionAspect *>(this);
    m_model = new QStandardItemModel(self);
    m_selectionModel = new QItemSelectionModel(m_model);

    const auto fill = [self](const QList<QStandardItem *> &items) {
        emit self->modelChange(true);

        self->m_model->clear();
        for (QStandardItem *item : items)
            self->m_model->appendRow(item);

        self->volatileValueToGui();
        emit self->modelChange(false);
    };

    connect(self, &StringSelectionAspect::refillRequested, self, [self, fill] {
        self->m_fillCallback(fill);
    });

    m_fillCallback(fill);
}

AspectPresentation StringSelectionAspect::presentation() const
{
    AspectPresentation p = TypedAspect::presentation();
    p.control = AspectControls::ComboBox;
    // The value is the item's data, which is what volatileValueToGui() looks
    // items up by. An item without data is addressed by an empty id, matching
    // what the widget editor stores for it.
    p.valueIsChoiceId = true;
    ensureFilled();
    if (m_model) {
        for (int row = 0, rows = m_model->rowCount(); row < rows; ++row) {
            const QStandardItem *item = m_model->item(row);
            p.choices.append(
                {item->text(), item->toolTip(), item->isEnabled(), item->data()});
        }
    }
    p.sizeAdjustPolicy = m_sizeAdjustPolicy;
    p.minimumContentsLength = m_minimumContentsLength;
    return p;
}

void StringSelectionAspect::setSizeAdjustPolicy(AspectControls::SizeAdjustPolicy policy)
{
    m_sizeAdjustPolicy = policy;
    emit controlConfigurationChanged();
}

void StringSelectionAspect::setMinimumContentsLength(int characters)
{
    m_minimumContentsLength = characters;
    emit controlConfigurationChanged();
}

void StringSelectionAspect::addToLayoutImpl(Layouting::Layout &parent)
{
    // The widget renderer owns this control's construction. Reaching the
    // check means no renderer was installed; see installAspectWidgetRenderer().
    QTC_CHECK(renderAspect(*this, parent));
}


//
// FontAspect
//

FontAspect::FontAspect(AspectContainer *container)
    : AspectContainer(container)
{}

QFont FontAspect::operator()() const
{
    return value();
}

QFont FontAspect::value() const
{
    QFont font;
    font.setFamily(fontFamily.value());
    font.setPointSize(fontPointSize.value());
    return font;
}

QFont FontAspect::volatileValue() const
{
    QFont font;
    font.setFamily(fontFamily.volatileValue());
    font.setPointSize(fontPointSize.volatileValue());
    return font;
}

void FontAspect::setValue(const QFont &font)
{
    fontFamily.setValue(font.family());
    fontPointSize.setValue(font.pointSize());
}

void FontAspect::setVolatileValue(const QFont &font)
{
    fontFamily.setVolatileValue(font.family());
    fontPointSize.setVolatileValue(font.pointSize());
}

AspectPresentation FontAspect::presentation() const
{
    AspectPresentation p = AspectContainer::presentation();
    p.control = AspectControls::FontPicker;
    return p;
}

void FontAspect::addToLayoutImpl(Layouting::Layout &parent)
{
    // The widget renderer owns this control's construction. Reaching the
    // check means no renderer was installed; see installAspectWidgetRenderer().
    QTC_CHECK(renderAspect(*this, parent));
}

ByteArrayAspect::ByteArrayAspect(AspectContainer *container)
    : TypedAspect<QByteArray>(container)
{}

ByteArrayAspect::~ByteArrayAspect() = default;

} // namespace Utils
