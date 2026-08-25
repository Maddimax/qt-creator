// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "devicesupport/idevicefwd.h"
#include "projectexplorer_export.h"
#include "task.h"

#include <QtTaskTree/QTaskTree>

#include <utils/aspects.h>

#include <QAbstractItemModel>
#include <QPair>
#include <QSet>

#include <functional>

QT_BEGIN_NAMESPACE
class QAction;
QT_END_NAMESPACE

namespace Utils {
class Environment;
class MacroExpander;
class OutputLineParser;
} // namespace Utils

namespace ProjectExplorer {
class Kit;
class KitAspect;

using LogCallback = std::function<void(const QString &message)>;

class PROJECTEXPLORER_EXPORT DetectionSource
{
public:
    enum DetectionType {
        Manual,
        FromSystem,
        FromSdk,
        Temporary,
        Uninitialized,
    };

    DetectionSource() = default;
    DetectionSource(DetectionType type, const QString &id = {})
        : type(type), id(id)
    {}

    bool isAutoDetected() const
    {
        return type == FromSystem || type == FromSdk || type == Temporary;
    }

    bool isTemporary() const
    {
        return type == Temporary;
    }

    bool isSdkProvided() const
    {
        return type == FromSdk;
    }

    bool isSystemDetected() const
    {
        return type == FromSystem;
    }

    bool operator==(const DetectionSource &other) const
    {
        return type == other.type && id == other.id;
    }

    void fromMap(const Utils::Store &);
    void toMap(Utils::Store &) const;

    static std::optional<DetectionSource> createFromMap(const Utils::Store &);

    DetectionType type = Uninitialized;
    QString id;

private:
    PROJECTEXPLORER_EXPORT friend QDebug operator<<(QDebug dbg, const DetectionSource &source);
};

PROJECTEXPLORER_EXPORT QtTaskTree::Group kitDetectionRecipe(
    const IDeviceConstPtr &device,
    DetectionSource::DetectionType detectionType,
    const LogCallback &logCallback);

PROJECTEXPLORER_EXPORT QtTaskTree::Group removeDetectedKitsRecipe(
    const IDeviceConstPtr &device, const LogCallback &logCallback);

PROJECTEXPLORER_EXPORT void listAutoDetected(
    const IDeviceConstPtr &device, const LogCallback &logCallback);

/**
 * @brief The KitAspectFactory class
 *
 * A KitAspectFactory can create instances of one type of KitAspect.
 * A KitAspect handles a specific piece of information stored in the kit.
 *
 * They auto-register with the \a KitManager for their life time
 */
class PROJECTEXPLORER_EXPORT KitAspectFactory : public QObject
{
public:
    using Item = QPair<QString, QString>;
    using ItemList = QList<Item>;

    Utils::Id id() const { return m_id; }
    QList<Utils::Id> jsonKeys() const { return m_jsonKeys; }
    int priority() const { return m_priority; }
    QString displayName() const { return m_displayName; }
    QString description() const { return m_description; }
    bool isEssential() const { return m_essential; }

    // called to find issues with the kit
    virtual Tasks validate(const Kit *) const = 0;
    // called after restoring a kit, so upgrading of kit information settings can be done
    virtual void upgrade(Kit *) { return; }
    // called to fix issues with this kitinformation. Does not modify the rest of the kit.
    virtual void fix(Kit *) { return; }
    // called on initial setup of a kit.
    virtual void setup(Kit *) { return; }

    virtual int weight(const Kit *k) const;

    virtual QVariant getInfo(const Kit *k, Utils::Id request, const QVariant &input) const;

    virtual ItemList toUserOutput(const Kit *) const = 0;

    // Enumerable values this aspect can be set to, for item-backed aspects such
    // as the debugger, toolchain, Qt version or device. An empty list means the
    // value is free-form or not enumerable. Used e.g. to let tooling pick a
    // valid value for setValue().
    struct Candidate { QVariant value; QString displayName; };
    virtual QList<Candidate> candidateValues(const Kit *) const { return {}; }

    virtual KitAspect *createKitAspect(Kit *) const = 0;

    virtual void addToBuildEnvironment(const Kit *k, Utils::Environment &env) const;
    virtual void addToRunEnvironment(const Kit *k, Utils::Environment &env) const;

    virtual QList<Utils::OutputLineParser *> createOutputParsers(const Kit *k) const;

    virtual QSet<Utils::Id> supportedPlatforms(const Kit *k) const;
    virtual QSet<Utils::Id> availableFeatures(const Kit *k) const;

    QList<Utils::Id> embeddableAspects() const { return m_embeddableAspects; }

    virtual void addToMacroExpander(ProjectExplorer::Kit *kit, Utils::MacroExpander *expander) const;

    virtual void onKitsLoaded() {}

    static void handleKitsLoaded();
    static const QList<KitAspectFactory *> kitAspectFactories();

    virtual std::optional<QtTaskTree::ExecutableItem> autoDetect(
        Kit *kit,
        const Utils::FilePaths &searchPaths,
        const DetectionSource &detectionSource,
        const LogCallback &logCallback) const;

    virtual std::optional<QtTaskTree::ExecutableItem> removeAutoDetected(
        const QString &detectionSourceId, const LogCallback &logCallback) const;

    virtual void listAutoDetected(
        const QString &detectionSourceId, const LogCallback &logCallback) const;

    virtual Utils::Result<QtTaskTree::ExecutableItem> createAspectFromJson(
        const DetectionSource &detectionSource,
        const Utils::FilePath &rootPath,
        Kit *kit,
        const QJsonValue &json,
        const LogCallback &logCallback) const;

protected:
    KitAspectFactory();
    ~KitAspectFactory() override;

    void setId(Utils::Id id) { m_id = id; }
    void setJsonKeys(const QList<Utils::Id> &ids) { m_jsonKeys = ids; }
    void setDisplayName(const QString &name) { m_displayName = name; }
    void setDescription(const QString &desc) { m_description = desc; }
    void makeEssential() { m_essential = true; }
    void setPriority(int priority) { m_priority = priority; }
    void setEmbeddableAspects(const QList<Utils::Id> &aspects) { m_embeddableAspects = aspects; }
    void notifyAboutUpdate(Kit *k);

private:
    QString m_displayName;
    QString m_description;
    Utils::Id m_id;
    QList<Utils::Id> m_jsonKeys;
    QList<Utils::Id> m_embeddableAspects;
    int m_priority = 0; // The higher the closer to the top.
    bool m_essential = false;
};

// One row of the Kits page: what the aspect is called, what it holds, and the
// page that manages the things it offers. An AspectContainer so that what it
// holds is aspects rather than widgets - a Qt Quick page cannot host a
// QComboBox, and neither can anything else that wants to draw a kit.
class PROJECTEXPLORER_EXPORT KitAspect : public Utils::AspectContainer
{
    Q_OBJECT

public:
    enum ItemRole {
        IdRole = Qt::UserRole + 100,
        IsNoneRole,
        TypeRole,
        QualityRole
    };

    KitAspect(Kit *kit, const KitAspectFactory *factory);
    ~KitAspect() override;

    virtual void refresh();

    void addToLayoutImpl(Layouting::Layout &layout) override;
    static QString msgManage();

    Kit *kit() const;
    const KitAspectFactory *factory() const;
    QAction *mutableAction() const;
    // For a subclass that still draws its own widgets: the widget renderer
    // puts this on the controls it builds for an aspect, and a hand-built one
    // has to ask.
    void addMutableAction(QWidget *child);
    void setManagingPage(Utils::Id pageId);

    // Which other kit aspects this one shows inside its own row - a Qt shows
    // the mkspec, a device shows its type. The embedded aspect is not a row of
    // its own; its controls become part of this one.
    virtual void setAspectsToEmbed(const QList<KitAspect *> &aspects);
    QList<KitAspect *> aspectsToEmbed() const;

    void makeStickySubWidgetsReadOnly();
    void reload();

    // The selections this aspect offers, one per list it was given. An aspect
    // that draws another one beside its own - Qt draws qmake's, a device draws
    // its type's - reaches them here.
    QList<Utils::SelectionAspect *> listAspects() const;
    // The button that opens the page managing what this aspect offers, or null
    // where there is no such page. Not a control of the setting itself, so a
    // view that lays the row out itself puts it at the end.
    Utils::ActionAspect *manageButton() const;
    // What this row draws: everything it holds except the "Manage..." button,
    // which is not a control of the setting. Includes the controls of any
    // aspect embedded in this one.
    QList<Utils::BaseAspect *> controls() const;
    // Where an embedded aspect's controls go among this row's own. -1 puts
    // them at the end; a device shows the type before the device it narrows
    // down.
    virtual int embedIndex() const { return -1; }
    // Where a control of this row's own goes: before the "Manage..." button,
    // which stays at the end.
    int controlIndex() const;

    virtual void addToInnerLayout(Layouting::Layout &layout);

    // Whether the setting may be changed per run configuration. A state about
    // the setting rather than about its value, so a right-click rather than a
    // control of its own; offered by whatever this aspect ends up drawn as.
    bool offersMutability() const;
    void decorateWithMutability(Utils::AspectPresentation &presentation) const;
    void triggerContextAction(bool checked) override;

    // The control this kit aspect is: any aspect, plus the aspect's own
    // "Mark as Mutable". Owned here, since a kit aspect is made afresh for
    // every kit that is looked at.
    template<class T> T *addControl();

protected:
    virtual void makeReadOnly(bool readOnly);
    virtual Utils::Id settingsPageItemToPreselect() const { return {}; }

    void addLabelToLayout(Layouting::Layout &layout);
    void addControlsToLayout(Layouting::Layout &layout);
    void addListAspectsToLayout(Layouting::Layout &layout);
    void addManageButtonToLayout(Layouting::Layout &layout);

    // Convenience for aspects that provide a list model from which one value
    // can be chosen. It becomes a SelectionAspect, which either renderer draws.
    class ListAspectSpec
    {
    public:
        using Getter = std::function<QVariant(const Kit &)>;
        using Setter = std::function<void(Kit &, const QVariant &)>;
        using ResetModel = std::function<void()>;

        ListAspectSpec(
            QAbstractItemModel *model,
            Getter &&getter,
            Setter &&setter,
            ResetModel &&resetModel)
            : model(model)
            , getter(std::move(getter))
            , setter(std::move(setter))
            , resetModel(std::move(resetModel))
        {}

        QAbstractItemModel *model;
        Getter getter;
        Setter setter;
        ResetModel resetModel;
    };
    void addListAspectSpec(const ListAspectSpec &listAspectSpec);

private:
    class Private;
    Private * const d;
};

// A control on a kit aspect's row. Whatever aspect it wraps, it also offers
// the row's "Mark as Mutable", so that either renderer draws it without
// knowing what a kit is. See KitAspect::addControl().
template<class T> class KitAspectControl final : public T
{
public:
    explicit KitAspectControl(KitAspect *owner)
        : m_owner(owner)
    {}

    Utils::AspectPresentation presentation() const override
    {
        Utils::AspectPresentation p = T::presentation();
        m_owner->decorateWithMutability(p);
        return p;
    }

    void triggerContextAction(bool checked) override { m_owner->triggerContextAction(checked); }

private:
    KitAspect * const m_owner;
};

template<class T> T *KitAspect::addControl()
{
    const auto control = new KitAspectControl<T>(this);
    insertAspect(controlIndex(), control, /*takeOwnership=*/true);
    return control;
}

#ifdef WITH_TESTS
QObject *createKitAspectTest();
#endif

} // namespace ProjectExplorer
