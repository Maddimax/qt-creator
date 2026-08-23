// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "fancylineedit.h"
#include "filepath.h"
#include "aspectpresentation.h"
#include "id.h"
#include "validationfunction.h"

#include <QColor>
#include <QMap>
#include <QUndoCommand>
#include <QVariant>

#include <functional>
#include <memory>
#include <optional>

QT_BEGIN_NAMESPACE
class QAbstractButton;
class QAbstractSpinBox;
class QAction;
class QComboBox;
class QLabel;
class QSettings;
class QUndoStack;
class QStandardItem;
class QStandardItemModel;
class QItemSelectionModel;
class QValidator;
QT_END_NAMESPACE

namespace Layouting { class Layout; }

namespace Utils {

class AspectContainer;
class BoolAspect;
class CheckableDecider;
class Guard;
class Key;
class MacroExpander;
class PathChooser;

template <class T> class UndoableValue;

enum class PathChooserKind;

using Store = QMap<Key, QVariant>; // TODO: storefwd.h? utils_fwd.h?

namespace Internal {
class AspectContainerPrivate;
class AspectWidgetRenderer;
class BaseAspectPrivate;
class ToggleAspectPrivate;
class BoolAspectPrivate;
class ColorAspectPrivate;
class DoubleAspectPrivate;
class FilePathAspectPrivate;
class FilePathListAspectPrivate;
class FontFamilyAspectPrivate;
class IntegerAspectPrivate;
class MultiSelectionAspectPrivate;
class SelectionAspectPrivate;
class StringAspectPrivate;
class StringListAspectPrivate;
class TextDisplayPrivate;
class CheckableAspectImplementation;
} // Internal

class QTCREATOR_UTILS_EXPORT BaseAspect : public QObject
{
    Q_OBJECT

    // The volatile value is the one a settings page edits, so that Apply and
    // Cancel keep working.
    Q_PROPERTY(QVariant value READ volatileVariantValue WRITE setVolatileVariantValueFromGui
                   NOTIFY volatileValueChanged)
    Q_PROPERTY(QString labelText READ labelText WRITE setLabelText NOTIFY labelTextChanged)
    Q_PROPERTY(QString toolTip READ toolTip WRITE setToolTip NOTIFY tooltipChanged)
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool visible READ isVisible WRITE setVisible NOTIFY visibleChanged)
    Q_PROPERTY(bool readOnly READ isReadOnly WRITE setReadOnly NOTIFY readOnlyChanged)

public:
    BaseAspect(AspectContainer *container = nullptr);
    BaseAspect(const BaseAspect &) = delete;
    ~BaseAspect() override;

    Id id() const;
    void setId(Id id);

    enum Announcement { DoEmit, BeQuiet };

    virtual QVariant volatileVariantValue() const;
    virtual void setVolatileVariantValue(const QVariant &value, Announcement = DoEmit);
    // A write standing for the user editing a control, as opposed to the
    // program setting a value. Recorded on the undo stack by the aspects
    // that support undo; the widget delegates do the same from their signal
    // handlers.
    virtual void setVolatileVariantValueFromGui(const QVariant &value);
    virtual QVariant variantValue() const;
    virtual void setVariantValue(const QVariant &value, Announcement = DoEmit);

    virtual QVariant defaultVariantValue() const;
    virtual void setDefaultVariantValue(const QVariant &value);
    virtual bool isDefaultValue() const;

    Key settingsKey() const;
    void setSettingsKey(const Key &settingsKey);
    void setSettingsKey(const Key &group, const Key &key);

    QString displayName() const;
    void setDisplayName(const QString &displayName);

    QString toolTip() const;
    void setToolTip(const QString &tooltip);

    bool isVisible() const;
    void setVisible(bool visible);

    // Returns a controller that makes a target widget follow this aspect's
    // visibility (for Layouting's visibleOn()). See also groupChecker().
    std::function<void(QObject *)> visibleController();

    bool isAutoApply() const;
    virtual void setAutoApply(bool on);

    virtual void setUndoStack(QUndoStack *undoStack);
    QUndoStack *undoStack() const;

    bool isEnabled() const;
    virtual void setEnabled(bool enabled);
    void setEnabler(BoolAspect *checker);

    bool isReadOnly() const;
    void setReadOnly(bool enabled);

    void setSpan(int x, int y = 1);

    bool isSaveAlways() const;
    void setSaveAlways(bool saveAlways);

    QString labelText() const;
    void setLabelText(const QString &labelText);
    void setLabelPixmap(const QPixmap &labelPixmap);
    void setControlObjectName(const QString &objectName);
    void setIcon(const QIcon &labelIcon);
    QIcon icon() const;

    using ConfigWidgetCreator = std::function<QWidget *()>;
    void setConfigWidgetCreator(const ConfigWidgetCreator &configWidgetCreator);
    QWidget *createConfigWidget() const;

    virtual QAction *action();

    AspectContainer *container() const;

    virtual void fromMap(const Store &map);
    virtual void toMap(Store &map) const;
    virtual void toActiveMap(Store &map) const { toMap(map); }
    virtual void volatileToMap(Store &map) const;
    virtual void volatileFromMap(const Store &map);

    void addToLayout(Layouting::Layout &parent) const;

    // Describes this aspect's control without building one. Renderers use
    // this instead of asking what type the aspect is.
    virtual AspectPresentation presentation() const;

    virtual void readSettings();
    virtual void writeSettings() const;

    virtual QVariant toSettingsValue(const QVariant &valueToSave) const;
    virtual QVariant fromSettingsValue(const QVariant &savedValue) const;

    virtual void apply();
    virtual void cancel();
    virtual bool isDirty() const;
    bool hasAction() const;

    struct QTCREATOR_UTILS_EXPORT Changes
    {
        Changes();

        unsigned valueFromOutside : 1;
        unsigned valueFromVolatileValue : 1;
        unsigned volatileValueFromOutside : 1;
        unsigned volatileValueFromValue : 1;
        unsigned volatileValueFromGui : 1;
    };

    virtual void announceChanges(Changes changes, Announcement howToAnnounce = DoEmit);

    class QTCREATOR_UTILS_EXPORT Data
    {
    public:
        // The (unique) address of the "owning" aspect's meta object is used as identifier.
        using ClassId = const void *;

        virtual ~Data();

        Id id() const { return m_id; }
        ClassId classId() const { return m_classId; }
        Data *clone() const { return m_cloner(this); }

        QVariant value;

        class Ptr {
        public:
            Ptr() = default;
            explicit Ptr(const Data *data) : m_data(data) {}
            Ptr(const Ptr &other) { m_data = other.m_data->clone(); }
            ~Ptr() { delete m_data; }

            void operator=(const Ptr &other);
            void assign(const Data *other) { delete m_data; m_data = other; }

            const Data *get() const { return m_data; }

        private:
            const Data *m_data = nullptr;
        };

    protected:
        friend class BaseAspect;
        Id m_id;
        ClassId m_classId = 0;
        std::function<Data *(const Data *)> m_cloner;
    };

    using DataCreator = std::function<Data *()>;
    using DataCloner = std::function<Data *(const Data *)>;
    using DataExtractor = std::function<void(Data *data)>;

    Data::Ptr extractData() const;

    // This is expensive. Do not use without good reason
    void writeToSettingsImmediatly() const;

    void setMacroExpander(MacroExpander *expander);
    MacroExpander *macroExpander() const;

    using Callback = std::function<void()>;
    void addOnChanged(QObject *guard, const Callback &callback);
    void addOnVolatileValueChanged(QObject *guard, const Callback &callback);
    void addOnCheckedChanged(QObject *guard, const Callback &callback);
    void addOnEnabledChanged(QObject *guard, const Callback &callback);
    void addOnLabelTextChanged(QObject *guard, const Callback &callback);
    void addOnLabelPixmapChanged(QObject *guard, const Callback &callback);

signals:
    void changed();
    void volatileValueChanged();
    void labelLinkActivated(const QString &link);
    void checkedChanged();
    void enabledChanged();
    void readOnlyChanged(bool);
    void visibleChanged(bool);
    void tooltipChanged(const QString &tooltip);
    void labelTextChanged();
    void labelPixmapChanged();

    // Renderer-facing. An aspect holds no control, so a setter that has to
    // reach a live one says what happened and lets whoever built the control
    // re-read the aspect.
    void controlConfigurationChanged();
    void controlFocusRequested();
    void controlValidationRequested();

protected:
    virtual void addToLayoutImpl(Layouting::Layout &parent);
    [[deprecated("Use valueToVolatileValue()")]] bool internalToBuffer() { return valueToVolatileValue(); }
    [[deprecated("Use valueToVolatileValue()")]] bool bufferToInternal() { return volatileValueToValue(); }
    [[deprecated("Use valueToVolatileValue()")]] void bufferToGui() { volatileValueToGui(); }
    [[deprecated("Use valueToVolatileValue()")]] bool guiToBuffer() { return guiToVolatileValue(); }

    virtual bool valueToVolatileValue();
    virtual bool volatileValueToValue();
    virtual void volatileValueToGui();
    virtual bool guiToVolatileValue();

    virtual void handleGuiChanged();

    void addMacroExpansion(QWidget *w);

    QLabel *createLabel();
    QLabel *addLabeledItem(Layouting::Layout &parent, QWidget *widget);
    void addLabeledItems(Layouting::Layout &parent, const QList<QWidget *> &widgets);

    void setDataCreatorHelper(const DataCreator &creator) const;
    void setDataClonerHelper(const DataCloner &cloner) const;
    void addDataExtractorHelper(const DataExtractor &extractor) const;

    template <typename AspectClass, typename DataClass, typename Type>
    void addDataExtractor(AspectClass *aspect,
                          Type(AspectClass::*p)() const,
                          Type DataClass::*q) {
        setDataCreatorHelper([] {
            return new DataClass;
        });
        setDataClonerHelper([](const Data *data) {
            return new DataClass(*static_cast<const DataClass *>(data));
        });
        addDataExtractorHelper([aspect, p, q](Data *data) {
            static_cast<DataClass *>(data)->*q = (aspect->*p)();
        });
    }

    template <class Widget, typename ...Args>
    Widget *createSubWidget(Args && ...args) {
        auto w = new Widget(args...);
        registerSubWidget(w);
        if constexpr (std::is_base_of_v<QComboBox, Widget>
                      || std::is_base_of_v<QAbstractSpinBox, Widget>) {
            improveWheelScrolling(w);
        }
        return w;
    }

    void registerSubWidget(QWidget *widget) const;

    void saveToMap(Store &data, const QVariant &value,
                   const QVariant &defaultValue, const Key &key) const;
    bool skipSave() const;

protected:
    template <class Value>
    static bool updateStorage(Value &target, const Value &val)
    {
        if (target == val)
            return false;
        target = val;
        return true;
    }

private:
    friend class Internal::CheckableAspectImplementation;
    friend class AspectContainer;
    // Builds the same controls the addToLayoutImpl() bodies build and needs
    // the same protected widget helpers.
    friend class Internal::AspectWidgetRenderer;
    void setContainer(AspectContainer *container);
    void improveWheelScrolling(QWidget *widget);

    std::unique_ptr<Internal::BaseAspectPrivate> d;
};

QTCREATOR_UTILS_EXPORT void addToLayout(Layouting::Layout *layout, const BaseAspect *aspect);
QTCREATOR_UTILS_EXPORT void addToLayout(Layouting::Layout *layout, const BaseAspect &aspect);

// Builds the control described by presentation() for one GUI backend,
// installed at startup like Utils::Prompts (see aspectwidgetrenderer.h).
// Returns false for a control it does not handle; the aspect's inline widget
// construction then runs.
using AspectRenderer = std::function<bool(BaseAspect &aspect, Layouting::Layout &parent)>;
QTCREATOR_UTILS_EXPORT void setAspectRenderer(const AspectRenderer &renderer);

template<typename ValueType>
class TypedAspect : public BaseAspect
{
public:
    using valueType = ValueType;

    TypedAspect(AspectContainer *container = nullptr)
        : BaseAspect(container)
    {
        addDataExtractor(this, &TypedAspect::value, &Data::value);
    }

    struct Data : BaseAspect::Data
    {
        ValueType value;
    };

    ValueType operator()() const { return m_value; }
    ValueType value() const { return m_value; }
    ValueType defaultValue() const { return m_default; }
    ValueType volatileValue() const { return m_volatileValue; }

    // We assume that this is only used in the ctor and no signalling is needed.
    // If it is used elsewhere changes have to be detected and signalled externally.
    void setDefaultValue(const ValueType &value)
    {
        m_default = value;
        m_value = value;
        if (valueToVolatileValue()) // Might be more than a plain copy.
            volatileValueToGui();
    }

    bool isDefaultValue() const override
    {
        return m_default == m_value;
    }

    void setValue(const ValueType &value, Announcement howToAnnounce = DoEmit)
    {
        Changes changes;
        changes.valueFromOutside = updateStorage(m_value, value);
        if (valueToVolatileValue()) {
            changes.volatileValueFromValue = true;
            volatileValueToGui();
        }
        announceChanges(changes, howToAnnounce);
    }

    void setVolatileVariantValue(const QVariant &value,
                                 Announcement howToAnnounce = DoEmit) override
    {
        if (value.canConvert<ValueType>())
            setVolatileValue(value.value<ValueType>(), howToAnnounce);
    }

    void setVolatileValue(const ValueType &value, Announcement howToAnnounce = DoEmit)
    {
        Changes changes;
        if (updateStorage(m_volatileValue, value)) {
            changes.volatileValueFromOutside = true;
            volatileValueToGui();
        }
        if (isAutoApply() && volatileValueToValue())
            changes.valueFromVolatileValue = true;
        announceChanges(changes, howToAnnounce);
    }

    bool isDirty() const override
    {
        return m_value != m_volatileValue;
    }

    void volatileFromMap(const Store &map) override
    {
        if (skipSave())
            return;

        const QVariant val = map.value(settingsKey(), toSettingsValue(defaultVariantValue()));
        const QVariant converted = fromSettingsValue(val);
        setVolatileValue(converted.value<ValueType>());
    }

    QVariant toSettingsValue(const QVariant &valueToSave) const override {
        if constexpr (std::is_same_v<ValueType, QStringList>) {
            // QSettings stores empty QStringList as "@Invalid", which makes it impossible to
            // distinguish between an empty and an unset value. To work around this, we store
            // empty lists as "false" and convert them back when loading.
            if (valueToSave.value<QStringList>().isEmpty())
                return QVariant(false);
        } else if constexpr (std::is_same_v<ValueType, Id>) {
            // Id holds a process-local handle, not the string, and has no QVariant converter.
            // Store its persistent string form so it round-trips through QSettings.
            return valueToSave.value<Id>().toSetting();
        }
        return valueToSave;
    }

    QVariant fromSettingsValue(const QVariant &savedValue) const override {
        if constexpr (std::is_same_v<ValueType, QStringList>) {
            if (savedValue.typeId() == QMetaType::Bool && !savedValue.toBool())
                return QVariant(QStringList());
        } else if constexpr (std::is_same_v<ValueType, Id>) {
            return QVariant::fromValue(Id::fromSetting(savedValue));
        }
        return savedValue;
    }

protected:
    bool valueToVolatileValue() override
    {
        return updateStorage(m_volatileValue, m_value);
    }

    bool volatileValueToValue() override
    {
        return updateStorage(m_value, m_volatileValue);
    }

    QVariant variantValue() const override
    {
        return QVariant::fromValue<ValueType>(m_value);
    }

    QVariant volatileVariantValue() const override
    {
        return QVariant::fromValue<ValueType>(m_volatileValue);
    }

    void setVariantValue(const QVariant &value, Announcement howToAnnounce = DoEmit) override
    {
        setValue(value.value<ValueType>(), howToAnnounce);
    }

    QVariant defaultVariantValue() const override
    {
        return QVariant::fromValue<ValueType>(m_default);
    }

    void setDefaultVariantValue(const QVariant &value) override
    {
        setDefaultValue(value.value<ValueType>());
    }

    ValueType m_default{};
    ValueType m_value{};
    ValueType m_volatileValue{};
};

template <typename ValueType>
class FlexibleTypedAspect : public TypedAspect<ValueType>
{
public:
    using Base = TypedAspect<ValueType>;
    using Updater = std::function<bool(ValueType &, const ValueType &)>;

    using Base::Base;

    void setInternalToBuffer(const Updater &updater) { m_internalToBuffer = updater; }
    void setBufferToInternal(const Updater &updater) { m_bufferToInternal = updater; }
    void setInternalToExternal(const Updater &updater) { m_internalToExternal = updater; }

protected:
    bool internalToBuffer() override
    {
        if (m_internalToBuffer)
            return m_internalToBuffer(Base::m_buffer, Base::m_internal);
        return Base::internalToBuffer();
    }

    bool bufferToInternal() override
    {
        if (m_bufferToInternal)
            return m_bufferToInternal(Base::m_internal, Base::m_buffer);
        return Base::bufferToInternal();
    }

    ValueType expandedValue()
    {
        if (!m_internalToExternal)
            return Base::m_internal;
        ValueType val;
        m_internalToExternal(val, Base::m_internal);
        return val;
    }

    Updater m_internalToBuffer;
    Updater m_bufferToInternal;
    Updater m_internalToExternal;
};

class QTCREATOR_UTILS_EXPORT BoolAspect : public TypedAspect<bool>
{
    Q_OBJECT

public:
    void setVolatileVariantValueFromGui(const QVariant &value) override;
    AspectPresentation presentation() const override;
    BoolAspect(AspectContainer *container = nullptr);
    ~BoolAspect() override;

    void addToLayoutImpl(Layouting::Layout &parent) override;
    std::function<void(QObject *)> groupChecker();

    Utils::CheckableDecider askAgainCheckableDecider();
    Utils::CheckableDecider doNotAskAgainCheckableDecider();

    QAction *action() override;

    enum class LabelPlacement { AtCheckBox, Compact, InExtraLabel, ShowTip };
    void setLabel(const QString &labelText,
                  LabelPlacement labelPlacement = LabelPlacement::InExtraLabel);
    void setLabelPlacement(LabelPlacement labelPlacement);

    enum class DisplayStyle { CheckBox, RadionButton };
    void setDisplayStyle(DisplayStyle displayStyle);

    std::function<void(Layouting::Layout *)> adoptButton(QAbstractButton *button);

private:
    void addToLayoutHelper(Layouting::Layout &parent, QAbstractButton *button);

    void volatileValueToGui() override;
    bool guiToVolatileValue() override;

    std::unique_ptr<Internal::BoolAspectPrivate> d;
};

// For bool values that have changed their saved representation
class QTCREATOR_UTILS_EXPORT InvertedSavedBoolAspect : public BoolAspect
{
public:
    using BoolAspect::BoolAspect;

    QVariant fromSettingsValue(const QVariant &savedValue) const override;
    QVariant toSettingsValue(const QVariant &valueToSave) const override;
};

class QTCREATOR_UTILS_EXPORT ToggleAspect : public BoolAspect
{
public:
    ToggleAspect(AspectContainer *container = nullptr);
    ~ToggleAspect();

    void setOffIcon(const QIcon &icon);
    QIcon offIcon() const;

    void setOffTooltip(const QString &tooltip);
    QString offTooltip() const;

    void setOnIcon(const QIcon &icon);
    QIcon onIcon() const;

    void setOnTooltip(const QString &tooltip);
    QString onTooltip() const;

    void setOnText(const QString &text);
    QString onText() const;

    void setOffText(const QString &text);
    QString offText() const;

    QAction *action() override;

protected:
    void announceChanges(Changes changes, Announcement howToAnnounce = DoEmit) override;

private:
    std::unique_ptr<Internal::ToggleAspectPrivate> d;
};

class QTCREATOR_UTILS_EXPORT ColorAspect : public TypedAspect<QColor>
{
    Q_OBJECT

public:
    AspectPresentation presentation() const override;
    ColorAspect(AspectContainer *container = nullptr);
    ~ColorAspect() override;

    void addToLayoutImpl(Layouting::Layout &parent) override;
    void setAlphaAllowed(bool allowed);
    void setWithResetButton(bool withResetButton);
    void setMinimumSize(const QSize &size);

    std::unique_ptr<Internal::ColorAspectPrivate> d;
};

class QTCREATOR_UTILS_EXPORT FontFamilyAspect : public TypedAspect<QString>
{
    Q_OBJECT

public:
    void setVolatileVariantValueFromGui(const QVariant &value) override;
    AspectPresentation presentation() const override;
    FontFamilyAspect(AspectContainer *container = nullptr);
    ~FontFamilyAspect() override;

    void addToLayoutImpl(Layouting::Layout &parent) override;
    enum FontFilter {
        AllFonts = 0,
        ScalableFonts = 0x1,
        NonScalableFonts = 0x2,
        MonospacedFonts = 0x4,
        ProportionalFonts = 0x8,
    };
    Q_DECLARE_FLAGS(FontFilters, FontFilter)

    void setFontFilters(FontFilters fontFilters);

    void setDefaultValue(const QString &font);

private:
    void volatileValueToGui() override;
    bool guiToVolatileValue() override;

    bool valueToVolatileValue() override;
    bool volatileValueToValue() override;

    bool isDirty() const override;

    std::unique_ptr<Internal::FontFamilyAspectPrivate> d;
};

class QTCREATOR_UTILS_EXPORT SelectionAspect : public TypedAspect<int>
{
    Q_OBJECT

public:
    void setVolatileVariantValueFromGui(const QVariant &value) override;
    AspectPresentation presentation() const override;
    SelectionAspect(AspectContainer *container = nullptr);
    ~SelectionAspect() override;

    QString stringValue() const;
    void setStringValue(const QString &val);

    void setDefaultValue(const QString &val);
    void setDefaultValue(int val);

    QVariant itemValue() const;

    enum class DisplayStyle { RadioButtons, ComboBox };
    Q_ENUM(DisplayStyle)
    void setDisplayStyle(DisplayStyle style);
    DisplayStyle displayStyle() const;

    QVariant toSettingsValue(const QVariant &valueToSave) const override;
    QVariant fromSettingsValue(const QVariant &savedValue) const override;

    void setUseDataAsSavedValue();

    class Option
    {
    public:
        Option(const QString &displayName, const QString &toolTip, const QVariant &itemData)
            : displayName(displayName), tooltip(toolTip), itemData(itemData)
        {}
        QString displayName;
        QString tooltip;
        QVariant itemData;
        bool enabled = true;
    };

    void addOption(const QString &displayName, const QString &toolTip = {});
    void addOption(const Option &option);
    Q_INVOKABLE int optionCount() const;
    int indexForDisplay(const QString &displayName) const;
    Q_INVOKABLE QString displayForIndex(int index) const;
    std::optional<Option> optionForIndex(int index) const;
    void setOptionForIndex(int index, const Option &option);
    int indexForItemValue(const QVariant &value) const;
    QVariant itemValueForIndex(int index) const;

protected:
    void addToLayoutImpl(Layouting::Layout &parent) override;

    void volatileValueToGui() override;
    bool guiToVolatileValue() override;

    std::unique_ptr<Internal::SelectionAspectPrivate> d;
};

template <class ValueType>
class TypedSelectionAspect : public SelectionAspect
{
public:
    TypedSelectionAspect(AspectContainer *container = nullptr)
        : SelectionAspect(container) { setUseDataAsSavedValue(); }

    ValueType operator()() const { return fromIndex(SelectionAspect::operator()()); }
    ValueType value() const { return fromIndex(SelectionAspect::value()); }
    void setValue(ValueType value, Announcement announce = DoEmit)
    {
        SelectionAspect::setValue(toIndex(value), announce);
    }

    ValueType defaultValue() const { return fromIndex(SelectionAspect::defaultValue()); }
    void setDefaultValue(ValueType value) { SelectionAspect::setDefaultValue(toIndex(value)); }

    ValueType volatileValue() const { return fromIndex(SelectionAspect::volatileValue()); }
    void setVolatileValue(ValueType value) { SelectionAspect::setVolatileValue(toIndex(value)); }

private:
    ValueType fromIndex(int index) const {
        const QVariant iv = itemValueForIndex(index);
        return static_cast<ValueType>(iv.isValid() ? iv.toInt() : index);
    }
    int toIndex(ValueType value) const {
        const int idx = indexForItemValue(QVariant(static_cast<int>(value)));
        return idx >= 0 ? idx : static_cast<int>(value);
    }
};

class QTCREATOR_UTILS_EXPORT MultiSelectionAspect : public TypedAspect<QStringList>
{
    Q_OBJECT

public:
    void setVolatileVariantValueFromGui(const QVariant &value) override;
    AspectPresentation presentation() const override;
    MultiSelectionAspect(AspectContainer *container = nullptr);
    ~MultiSelectionAspect() override;

    enum class DisplayStyle { ListView };
    void setDisplayStyle(DisplayStyle style);

    QStringList allValues() const;
    void setAllValues(const QStringList &val);

protected:
    void addToLayoutImpl(Layouting::Layout &parent) override;

    void volatileValueToGui() override;
    bool guiToVolatileValue() override;

private:
    std::unique_ptr<Internal::MultiSelectionAspectPrivate> d;
};

enum class UncheckedSemantics { Disabled, ReadOnly };
enum class CheckBoxPlacement { Top, Right, Left };

class QTCREATOR_UTILS_EXPORT StringAspect : public TypedAspect<QString>
{
    Q_OBJECT

public:
    void setVolatileVariantValueFromGui(const QVariant &value) override;
    AspectPresentation presentation() const override;
    StringAspect(AspectContainer *container = nullptr);
    ~StringAspect() override;

    QString operator()() const;
    [[deprecated("Use operator()() instead")]] QString expandedValue() const;

    // Hook between UI and StringAspect:
    using ValueAcceptor = std::function<std::optional<QString>(const QString &, const QString &)>;
    void setValueAcceptor(ValueAcceptor &&acceptor);

    void setShowToolTipOnLabel(bool show);
    void setDisplayFilter(const std::function<QString (const QString &)> &displayFilter);
    void setPlaceHolderText(const QString &placeHolderText);
    void setHistoryCompleter(const Key &historyCompleterKey);
    void setAcceptRichText(bool acceptRichText);
    void setUseResetButton();
    void setValidationFunction(const ValidationFunction &validator);
    void setValidatorFactory(const std::function<QValidator *(QObject *parent)> &validatorFactory);
    void setAutoApplyOnEditingFinished(bool applyOnEditingFinished);
    void setElideMode(Qt::TextElideMode elideMode);

    void makeCheckable(CheckBoxPlacement checkBoxPlacement, const QString &optionalLabel, const Key &optionalBaseKey);
    bool isChecked() const;
    void setChecked(bool checked);

    void setRightSideIconPath(const FilePath &path);
    void addOnRightSideIconClicked(QObject *guard, const std::function<void()> &);
    void setMinimumHeight(int);
    // What typing in the control completes against. The control owns the
    // completer; the aspect only says what to complete.
    void setCompletions(const QStringList &completions);

    enum DisplayStyle {
        LabelDisplay,
        LineEditDisplay,
        TextEditDisplay,
        PasswordLineEditDisplay,
    };
    void setDisplayStyle(DisplayStyle style);

    void fromMap(const Utils::Store &map) override;
    void toMap(Utils::Store &map) const override;
    void volatileToMap(Utils::Store &map) const override;
    void volatileFromMap(const Utils::Store &map) override;

signals:
    void validChanged(bool validState);
    void elideModeChanged(Qt::TextElideMode elideMode);
    void historyCompleterKeyChanged(const Key &historyCompleterKey);
    void acceptRichTextChanged(bool acceptRichText);
    void validationFunctionChanged(const ValidationFunction &validator);
    void placeholderTextChanged(const QString &placeholderText);
    void rightSideIconClicked();

protected:
    void addToLayoutImpl(Layouting::Layout &parent) override;

    void volatileValueToGui() override;
    bool guiToVolatileValue() override;

    bool valueToVolatileValue() override;
    bool volatileValueToValue() override;

    std::unique_ptr<Internal::StringAspectPrivate> d;

private:
    // Read access for the widget renderer, mirroring the public setters.
    friend class Internal::AspectWidgetRenderer;
    bool isCheckable() const;
    Internal::CheckableAspectImplementation &checker();
    UndoableValue<QString> &undoableValue();
    std::function<QString(const QString &)> displayFilter() const;
    Qt::TextElideMode elideMode() const;
    bool showToolTipOnLabel() const;
    int minimumHeight() const;
    QStringList completions() const;
    FilePath rightSideIconPath() const;
    Key historyCompleterKey() const;
    std::optional<ValidationFunction> validationFunction() const;
    std::function<QValidator *(QObject *)> validatorFactory() const;
    bool validatePlaceHolder() const;
    bool autoApplyOnEditingFinished() const;
    bool acceptRichText() const;
};

class QTCREATOR_UTILS_EXPORT ByteArrayAspect : public TypedAspect<QByteArray>
{
    Q_OBJECT

public:
    ByteArrayAspect(AspectContainer *container = nullptr);
    ~ByteArrayAspect() override;
};

class QTCREATOR_UTILS_EXPORT FilePathAspect : public TypedAspect<QString>
{
    Q_OBJECT

public:
    void setVolatileVariantValueFromGui(const QVariant &value) override;
    AspectPresentation presentation() const override;
    FilePathAspect(AspectContainer *container = nullptr);
    ~FilePathAspect();

    struct Data : BaseAspect::Data
    {
        QString value;
        FilePath filePath;
    };

    FilePath operator()() const;
    FilePath effectiveBinary() const;
    FilePath expandedValue() const;
    FilePath expandedVolatileValue() const;
    // Resolves the current input the way the control does: macros and
    // environment variables expanded, then the base directory applied, or the
    // search path for the command kinds. expandedVolatileValue() only expands
    // macros.
    FilePath resolvedVolatileValue() const;
    QString value() const;
    void setValue(const FilePath &filePath, Announcement howToAnnounce = DoEmit);
    void setValue(const QString &filePath, Announcement howToAnnounce = DoEmit);
    void setValueAlternatives(const FilePaths &candidate);
    void setDefaultValue(const QString &filePath);
    void setDefaultPathValue(const FilePath &filePath);

    void setPromptDialogFilter(const QString &filter);
    void setPromptDialogTitle(const QString &title);
    void setCommandVersionArguments(const QStringList &arguments);
    void setAllowPathFromDevice(bool allowPathFromDevice);
    void setValidatePlaceHolder(bool validatePlaceHolder);
    void setOpenTerminalHandler(const std::function<void()> &openTerminal);
    void setExpectedKind(const PathChooserKind &expectedKind);
    void setEnvironment(const Environment &env);
    void setBaseDirectory(const Lazy<FilePath> &baseDirectory);
    void setInitialBrowsePathBackup(const FilePath &initialBrowsePathBackup);

    void setPlaceHolderText(const QString &placeHolderText);
    void setValidationFunction(const ValidationFunction &validator);
    void setDisplayFilter(const std::function<QString (const QString &)> &displayFilter);
    void setHistoryCompleter(const Key &historyCompleterKey);
    void setShowToolTipOnLabel(bool show);
    void setAutoApplyOnEditingFinished(bool applyOnEditingFinished);

    void validateInput();

    void makeCheckable(CheckBoxPlacement checkBoxPlacement, const QString &optionalLabel, const Key &optionalBaseKey);
    bool isChecked() const;
    void setChecked(bool checked);

    // Hook between UI and StringAspect:
    using ValueAcceptor = std::function<std::optional<QString>(const QString &, const QString &)>;
    void setValueAcceptor(ValueAcceptor &&acceptor);

    // Whether the control currently shows something the expected kind
    // accepts. False until the first validation of a control has finished,
    // and false while no control is showing the aspect.
    bool isValid() const;

    // The validation the control applies when no validation function is set.
    // Wrap it to add a check of your own.
    AsyncValidationFunction defaultValidationFunction() const;

    // An extra button next to the browse button, for example to install what
    // the path is supposed to point at.
    class Button
    {
    public:
        QString text;
        QPointer<QObject> context;
        std::function<void()> callback;
    };
    void addButton(const QString &text, QObject *context,
                   const std::function<void()> &callback);

    void addToLayoutImpl(Layouting::Layout &parent) override;

    void fromMap(const Utils::Store &map) override;
    void toMap(Utils::Store &map) const override;
    void volatileToMap(Utils::Store &map) const override;
    void volatileFromMap(const Utils::Store &map) override;

    void setFocusToInputField();

signals:
    void validChanged(bool validState);

protected:
    bool isCheckable() const;

    void volatileValueToGui() override;
    bool guiToVolatileValue() override;

    bool valueToVolatileValue() override;
    bool volatileValueToValue() override;

    std::unique_ptr<Internal::FilePathAspectPrivate> d;

private:
    // Read access for the widget renderer, mirroring the public setters.
    friend class Internal::AspectWidgetRenderer;
    Internal::CheckableAspectImplementation &checker();
    UndoableValue<QString> &undoableValue();
    std::function<QString(const QString &)> displayFilter() const;
    PathChooserKind expectedKind() const;
    Key historyCompleterKey() const;
    std::optional<ValidationFunction> validationFunction() const;
    Environment environment() const;
    Lazy<FilePath> baseDirectory() const;
    FilePath initialBrowsePathBackup() const;
    std::function<void()> openTerminalHandler() const;
    QString promptDialogFilter() const;
    QString promptDialogTitle() const;
    QStringList commandVersionArguments() const;
    bool allowPathFromDevice() const;
    bool validatePlaceHolder() const;
    FilePaths valueAlternatives() const;
    bool autoApplyOnEditingFinished() const;
    Guard &editFinishedGuard();

    QList<Button> buttons() const;
    void setValid(bool valid);
};

class QTCREATOR_UTILS_EXPORT IntegerAspect : public TypedAspect<qint64>
{
    Q_OBJECT

public:
    void setVolatileVariantValueFromGui(const QVariant &value) override;
    AspectPresentation presentation() const override;
    IntegerAspect(AspectContainer *container = nullptr);
    ~IntegerAspect() override;

    void addToLayoutImpl(Layouting::Layout &parent) override;

    void setRange(qint64 min, qint64 max);
    std::optional<qint64> minimumValue() const;
    std::optional<qint64> maximumValue() const;
    qint64 singleStep() const;
    QString prefix() const;
    QString suffix() const;
    QString specialValueText() const;
    int displayIntegerBase() const;
    qint64 displayScaleFactor() const;
    void setLabel(const QString &label); // FIXME: Use setLabelText
    void setPrefix(const QString &prefix);
    void setSuffix(const QString &suffix);
    void setDisplayIntegerBase(int base);
    void setDisplayScaleFactor(qint64 factor);
    void setSpecialValueText(const QString &specialText);
    void setSingleStep(qint64 step);

    struct Data : BaseAspect::Data { qint64 value = 0; };

protected:
    void volatileValueToGui() override;
    bool guiToVolatileValue() override;
    QVariant fromSettingsValue(const QVariant &savedValue) const override;

private:
    std::unique_ptr<Internal::IntegerAspectPrivate> d;
};

class QTCREATOR_UTILS_EXPORT DoubleAspect : public TypedAspect<double>
{
    Q_OBJECT

public:
    void setVolatileVariantValueFromGui(const QVariant &value) override;
    AspectPresentation presentation() const override;
    DoubleAspect(AspectContainer *container = nullptr);
    ~DoubleAspect() override;

    void addToLayoutImpl(Layouting::Layout &parent) override;

    void setRange(double min, double max);
    std::optional<double> minimumValue() const;
    std::optional<double> maximumValue() const;
    double singleStep() const;
    QString prefix() const;
    QString suffix() const;
    QString specialValueText() const;
    void setPrefix(const QString &prefix);
    void setSuffix(const QString &suffix);
    void setSpecialValueText(const QString &specialText);
    void setSingleStep(double step);

protected:
    void volatileValueToGui() override;
    bool guiToVolatileValue() override;

private:
    std::unique_ptr<Internal::DoubleAspectPrivate> d;
};

class QTCREATOR_UTILS_EXPORT TriState
{
public:
    enum Value { EnabledValue, DisabledValue, DefaultValue };

    TriState() = default;
    explicit TriState(Value v) : m_value(v) {}

    int toInt() const { return int(m_value); }
    QVariant toVariant() const { return int(m_value); }
    static TriState fromInt(int value);
    static TriState fromVariant(const QVariant &variant);

    static const TriState Enabled;
    static const TriState Disabled;
    static const TriState Default;

private:
    friend bool operator==(TriState, TriState) = default;

    Value m_value = DefaultValue;
};

class QTCREATOR_UTILS_EXPORT TriStateAspect : public SelectionAspect
{
    Q_OBJECT

public:
    TriStateAspect(AspectContainer *container = nullptr,
                   const QString &enabledDisplay = {},
                   const QString &disabledDisplay = {},
                   const QString &defaultDisplay = {});

    TriState operator()() const { return value(); }
    TriState value() const;
    void setValue(TriState setting);

    TriState defaultValue() const;
    void setDefaultValue(TriState setting);

    void setOptionText(const TriState::Value tristate, const QString &display);

private:
    void addOption(const QString &displayName, const QString &toolTip = {}) = delete;
    void addOption(const Option &option) = delete;
};

class QTCREATOR_UTILS_EXPORT StringListAspect : public TypedAspect<QStringList>
{
    Q_OBJECT

public:
    void setVolatileVariantValueFromGui(const QVariant &value) override;
    AspectPresentation presentation() const override;
    StringListAspect(AspectContainer *container = nullptr);
    ~StringListAspect() override;

    bool guiToVolatileValue() override;
    void volatileValueToGui() override;

    enum class DisplayStyle { ListView, CommaSeparatedLineEdit };
    void setDisplayStyle(DisplayStyle displayStyle);

    void addToLayoutImpl(Layouting::Layout &parent) override;

    void appendValue(const QString &value, bool allowDuplicates = true);
    void removeValue(const QString &value);
    void appendValues(const QStringList &values, bool allowDuplicates = true);
    void removeValues(const QStringList &values);

    void setUiAllowAdding(bool allowAdding);
    void setUiAllowRemoving(bool allowRemoving);
    void setUiAllowEditing(bool allowEditing);

    bool uiAllowAdding() const;
    bool uiAllowRemoving() const;
    bool uiAllowEditing() const;

private:
    // The tree editor's Add flow appends the row-to-edit silently; only the
    // edit itself records the undo command.
    friend class Internal::AspectWidgetRenderer;
    UndoableValue<QStringList> &undoableValue();

    std::unique_ptr<Internal::StringListAspectPrivate> d;
};

class QTCREATOR_UTILS_EXPORT FilePathListAspect : public TypedAspect<QStringList>
{
    Q_OBJECT

public:
    void setVolatileVariantValueFromGui(const QVariant &value) override;
    AspectPresentation presentation() const override;
    FilePathListAspect(AspectContainer *container = nullptr);
    ~FilePathListAspect() override;

    FilePaths operator()() const;

    bool guiToVolatileValue() override;
    void volatileValueToGui() override;

    void addToLayoutImpl(Layouting::Layout &parent) override;
    void setPlaceHolderText(const QString &placeHolderText);
    QString placeHolderText() const;

    void appendValue(const FilePath &path, bool allowDuplicates = true);
    void removeValue(const FilePath &path);
    void appendValues(const FilePaths &values, bool allowDuplicates = true);
    void removeValues(const FilePaths &values);

signals:
    void placeHolderTextChanged(const QString &placeHolderText);

private:
    std::unique_ptr<Internal::FilePathListAspectPrivate> d;
};

class QTCREATOR_UTILS_EXPORT IntegersAspect : public TypedAspect<QList<int>>
{
    Q_OBJECT

public:
    AspectPresentation presentation() const override;
    IntegersAspect(AspectContainer *container = nullptr);
    ~IntegersAspect() override;

    void addToLayoutImpl(Layouting::Layout &parent) override;
};

class QTCREATOR_UTILS_EXPORT IdAspect : public TypedAspect<Id>
{
    Q_OBJECT

public:
    using TypedAspect::TypedAspect;
};

class QTCREATOR_UTILS_EXPORT TextDisplay : public BaseAspect
{
    Q_OBJECT

public:
    AspectPresentation presentation() const override;
    explicit TextDisplay(AspectContainer *container = nullptr, const QString &message = {});
    ~TextDisplay() override;

    void addToLayoutImpl(Layouting::Layout &parent) override;

    void setIconType(InfoType type);
    void setText(const QString &message);
    void setWordWrap(bool on);

    QString text() const;

signals:
    void linkActivated(const QString &link);

private:
    std::unique_ptr<Internal::TextDisplayPrivate> d;
};

class QTCREATOR_UTILS_EXPORT AspectContainerData
{
public:
    AspectContainerData() = default;

    const BaseAspect::Data *aspect(Id instanceId) const;
    const BaseAspect::Data *aspect(BaseAspect::Data::ClassId classId) const;

    void append(const BaseAspect::Data::Ptr &data);

    template <typename T> const typename T::Data *aspect() const
    {
        return static_cast<const typename T::Data *>(aspect(&T::staticMetaObject));
    }

private:
    QList<BaseAspect::Data::Ptr> m_data; // Owned.
};

class QTCREATOR_UTILS_EXPORT SettingsGroupNester
{
    Q_DISABLE_COPY_MOVE(SettingsGroupNester)

public:
    explicit SettingsGroupNester(const QStringList &groups);
    ~SettingsGroupNester();

private:
    const int m_groupCount;
};

class QTCREATOR_UTILS_EXPORT AspectContainer : public BaseAspect
{
    Q_OBJECT

public:
    AspectPresentation presentation() const override;
    explicit AspectContainer(AspectContainer *parentContainer = nullptr);
    ~AspectContainer();

    AspectContainer(const AspectContainer &) = delete;
    AspectContainer &operator=(const AspectContainer &) = delete;

    void addToLayoutImpl(Layouting::Layout &parent) override;

    void registerAspect(BaseAspect *aspect, bool takeOwnership = false);
    void registerAspects(const AspectContainer &aspects);

    void fromMap(const Utils::Store &map) override;
    void toMap(Utils::Store &map) const override;
    void volatileToMap(Utils::Store &map) const override;
    void volatileFromMap(const Utils::Store &map) override;

    void readSettings() override;
    void writeSettings() const override;

    void volatileValueToGui() override;
    bool guiToVolatileValue() override;

    void setSettingsGroup(const QString &groupKey);
    void setSettingsGroups(const QString &groupKey, const QString &subGroupKey);
    QStringList settingsGroups() const;

    void apply() override;
    void cancel() override;

    void reset();
    bool equals(const AspectContainer &other) const;
    void copyFrom(const AspectContainer &other);
    void setAutoApply(bool on) override;
    bool isDirty() const override;
    void setUndoStack(QUndoStack *undoStack) override;
    void setEnabled(bool enabled) override;

    template <typename T> T *aspect() const
    {
        for (BaseAspect *aspect : aspects())
            if (T *result = qobject_cast<T *>(aspect))
                return result;
        return nullptr;
    }

    BaseAspect *aspect(Id id) const;

    template <typename T> T *aspect(Id id) const
    {
        return qobject_cast<T*>(aspect(id));
    }

    void forEachAspect(const std::function<void(BaseAspect *)> &run) const;

    const QList<BaseAspect *> &aspects() const;

    using const_iterator = QList<BaseAspect *>::const_iterator;
    using value_type = QList<BaseAspect *>::value_type;

    const_iterator begin() const;
    const_iterator end() const;

    void setLayouter(const std::function<Layouting::Layout()> &layouter);
    std::function<Layouting::Layout()> layouter() const;

#ifdef WITH_TESTS
    // Registry of all live AspectContainer instances, for settings
    // introspection from tests and tooling (e.g. the MCP server). Lets a walker
    // reach per-instance containers - run/build configs, kits, devices - that
    // are not exposed as Core::IOptionsPages.
    static const QList<AspectContainer *> &registeredContainers();
#endif

signals:
    void applied();
    void fromMapFinished();
    void subAspectChanged(BaseAspect *aspect);

private:
    std::unique_ptr<Internal::AspectContainerPrivate> d;
};

// Because QObject cannot be a template
class QTCREATOR_UTILS_EXPORT UndoSignaller : public QObject
{
    Q_OBJECT
public:
    void emitChanged() { emit changed(); }
signals:
    void changed();
};

template<class T>
class UndoableValue
{
public:
    class UndoCmd : public QUndoCommand
    {
    public:
        UndoCmd(UndoableValue<T> *value, const T &oldValue, const T &newValue)
            : m_value(value)
            , m_oldValue(oldValue)
            , m_newValue(newValue)
        {}

        void undo() override { m_value->setInternal(m_oldValue); }
        void redo() override { m_value->setInternal(m_newValue); }

    private:
        UndoableValue<T> *m_value;
        T m_oldValue;
        T m_newValue;
    };

    void set(QUndoStack *stack, const T &value)
    {
        if (m_value == value)
            return;

        if (stack)
            stack->push(new UndoCmd(this, m_value, value));
        else
            setInternal(value);
    }

    void setSilently(const T &value) { m_value = value; }
    void setWithoutUndo(const T &value) { setInternal(value); }

    T get() const { return m_value; }

    UndoSignaller m_signal;

private:
    void setInternal(const T &value)
    {
        m_value = value;
        m_signal.emitChanged();
    }

private:
    T m_value{};
};

// FIXME: Merge into SelectionAspect
class QTCREATOR_UTILS_EXPORT StringSelectionAspect : public Utils::TypedAspect<QString>
{
    Q_OBJECT
public:
    void setVolatileVariantValueFromGui(const QVariant &value) override;
    AspectPresentation presentation() const override;
    StringSelectionAspect(Utils::AspectContainer *container = nullptr);

    void addToLayoutImpl(Layouting::Layout &parent) override;

    void setSizeAdjustPolicy(AspectControls::SizeAdjustPolicy policy);
    void setMinimumContentsLength(int characters);

    using ResultCallback = std::function<void(QList<QStandardItem *> items)>;
    using FillCallback = std::function<void(ResultCallback)>;
    void setFillCallback(FillCallback callback) { m_fillCallback = callback; }

    void refill() { emit refillRequested(); }

    void volatileValueToGui() override;
    bool guiToVolatileValue() override;

    void setComboBoxEditable(bool editable) { m_comboBoxEditable = editable; }

signals:
    void refillRequested();
    void modelChange(bool isChanging);

protected:
    QStandardItem *itemById(const QString &id);

private:
    friend class Internal::AspectWidgetRenderer;
    FillCallback m_fillCallback;
    QStandardItemModel *m_model{nullptr};
    QItemSelectionModel *m_selectionModel{nullptr};
    bool m_comboBoxEditable{true};
    AspectControls::SizeAdjustPolicy m_sizeAdjustPolicy
        = AspectControls::SizeAdjustPolicy::ToMinimumContentsLengthWithIcon;
    int m_minimumContentsLength = 0;

    Utils::UndoableValue<QString> m_undoable;
};

class QTCREATOR_UTILS_EXPORT FontAspect : public AspectContainer
{
    Q_OBJECT

public:
    AspectPresentation presentation() const override;
    FontAspect(Utils::AspectContainer *container = nullptr);

    QFont operator()() const;
    QFont value() const;
    QFont volatileValue() const;

    void setValue(const QFont &font);
    void setVolatileValue(const QFont &font);

    void addToLayoutImpl(Layouting::Layout &parent) override;

    FontFamilyAspect fontFamily{this};
    Utils::IntegerAspect fontPointSize{this};
};

} // namespace Utils
