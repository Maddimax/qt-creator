// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "projectexplorer_export.h"

#include "kit.h"

#include <utils/aspects.h>

#include <QWidget>

QT_BEGIN_NAMESPACE
class QComboBox;
class QPushButton;
QT_END_NAMESPACE

namespace ProjectExplorer {

// One entry a kit chooser offers.
struct KitChoice
{
    Utils::Id kitId;
    QString displayName;
    QString toolTip;
    // The active project's kit, which is offered first and named as such so it
    // can be told from the same kit appearing again in the sorted list.
    bool isActiveProjectKit = false;
};

// What a chooser offers, out of \a sortedKits and the active project's kit,
// keeping only what \a predicate accepts.
//
// Kept out of the chooser because it is a question about a set of kits, and
// there the only way to ask it was to build a combo box and read it back.
PROJECTEXPLORER_EXPORT QList<KitChoice> kitChoices(const QList<Kit *> &sortedKits,
                                                   Kit *activeProjectKit,
                                                   const Kit::Predicate &predicate);

// Which entry to open on: the one chosen last, and failing that the active
// project's kit. -1 when there is nothing to open on at all.
PROJECTEXPLORER_EXPORT int initialKitChoice(const QList<KitChoice> &choices,
                                            Utils::Id lastChosen);

// What is remembered when the entry at \a index is chosen. The active
// project's entry is remembered as an *invalid* id on purpose, so that what is
// restored follows the project rather than whichever kit it had at the time.
PROJECTEXPLORER_EXPORT Utils::Id rememberedKitId(const QList<KitChoice> &choices, int index);

// Let the user pick a kit, on a form. The Quick counterpart of KitChooser
// below: one row holding the choice and the way to the kit settings, which is
// what the widget put in a QHBoxLayout.
//
// A container rather than a plain SelectionAspect because of that second half -
// setInlineRow() is what keeps the two on one line.
class PROJECTEXPLORER_EXPORT KitChooserAspect : public Utils::AspectContainer
{
    Q_OBJECT

public:
    explicit KitChooserAspect(Utils::AspectContainer *container = nullptr);

    // Which kits this form can use. Asking again is what populate() is for.
    void setKitPredicate(const Kit::Predicate &predicate);
    void populate();

    Utils::Id currentKitId() const;
    Kit *currentKit() const;
    void setCurrentKitId(Utils::Id id);

    // Whether the first entry is the active project's kit rather than a kit in
    // its own right, which is what makes it remembered as an invalid id.
    bool hasStartupKit() const { return m_hasStartupKit; }

    // Remembers what is chosen, as the widget did when the reader picked
    // something rather than on every change.
    void rememberChoice();

    Utils::SelectionAspect kit{this};
    Utils::ActionAspect manage{this};

private:
    Kit::Predicate m_kitPredicate;
    QList<KitChoice> m_choices;
    bool m_hasStartupKit = false;
};

// Let the user pick a kit.
class PROJECTEXPLORER_EXPORT KitChooser : public QWidget
{
    Q_OBJECT

public:
    explicit KitChooser(QWidget *parent = nullptr);

    void setCurrentKitId(Utils::Id id);
    Utils::Id currentKitId() const;

    void setKitPredicate(const Kit::Predicate &predicate);
    void setShowIcons(bool showIcons);

    Kit *currentKit() const;
    bool hasStartupKit() const { return m_hasStartupKit; }

    static Kit *lastKit();

signals:
    void currentIndexChanged();
    void activated();

public slots:
    void populate();

protected:
    virtual QString kitText(const Kit *k) const;
    virtual QString kitToolTip(Kit *k) const;

private:
    void onActivated();
    void onCurrentIndexChanged();
    void onManageButtonClicked();

    Kit::Predicate m_kitPredicate;
    QComboBox *m_chooser;
    QPushButton *m_manageButton;
    bool m_hasStartupKit = false;
    bool m_showIcons = false;
};

#ifdef WITH_TESTS
QObject *createKitChooserTest();
#endif

} // namespace ProjectExplorer
