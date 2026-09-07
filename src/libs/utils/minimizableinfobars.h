// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include "id.h"
#include "infobar.h"

#include <QHash>
#include <QObject>

QT_BEGIN_NAMESPACE
class QAction;
QT_END_NAMESPACE

namespace Utils {

class QTCREATOR_UTILS_EXPORT MinimizableInfoBars : public QObject
{
    Q_OBJECT

public:
    explicit MinimizableInfoBars(InfoBar &infoBar);

    void setSettingsGroup(const Key &settingsGroup);
    void setPossibleInfoBarEntries(const QList<InfoBarEntry> &entries);
    void updateEntry(const InfoBarEntry &entry);

    // The way back from a minimized bar, one action per possible entry, each
    // carrying its own visibility. Both views put them in their tool bar
    // through the document, so there is one way in and nobody draws two.
    QList<QAction *> showInfoBarActions() const;

    void setInfoVisible(const Id &id, bool visible);
    bool isShownInInfoBar(const Id &id) const;

signals:
    // The entries a caller says are possible arrive after a view may already
    // have asked what belongs in its tool bar, so whoever draws them has to be
    // told they now exist.
    void showInfoBarActionsChanged();

private:
    void createActions();

    Key settingsKey(const Id &id) const;
    bool showInInfoBar(const Id &id) const;
    void setShowInInfoBar(const Id &id, bool show);

    void updateInfo(const Id &id);

    void showInfoBar(const Id &id);

    InfoBar &m_infoBar;
    Key m_settingsGroup;
    QHash<Id, QAction *> m_actions;
    QHash<Id, bool> m_isInfoVisible;
    QHash<Id, InfoBarEntry> m_infoEntries;
};

} // namespace Utils
