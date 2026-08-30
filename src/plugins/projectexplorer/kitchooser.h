// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "projectexplorer_export.h"

#include "kit.h"

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
