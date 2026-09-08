// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "core_global.h"

#include <utils/filepath.h>
#include <utils/id.h>

#include <QObject>
#include <QList>
#include <QKeySequence>

#include <functional>

QT_BEGIN_NAMESPACE
class QToolButton;
class QUrl;
class QWidget;
QT_END_NAMESPACE

namespace Utils {
class QtcSettings;
}

namespace Core {

struct NavigationView
{
    QWidget *widget;
    QList<QToolButton *> dockToolBarWidgets;
};

// Installed by a front end that can host a QML scene, the same way
// setAspectFormFactory() is - and for the same reason: this plugin names the
// QML its views are written in and never links the Quick library, so which
// front end draws them stays the front end's business. The object is handed
// to the scene as its "controller" property before the source is set, and
// parented to the widget that comes back. Nothing installed, or nullptr back,
// means the caller keeps its widget view.
using QmlViewFactory = std::function<QWidget *(const QUrl &source, QObject *controller)>;
CORE_EXPORT void setQmlViewFactory(const QmlViewFactory &factory);
CORE_EXPORT QWidget *createQmlView(const QUrl &source, QObject *controller);
CORE_EXPORT bool hasQmlViewFactory();

class CORE_EXPORT INavigationWidgetFactory : public QObject
{
    Q_OBJECT

public:
    INavigationWidgetFactory();
    ~INavigationWidgetFactory() override;

    static const QList<INavigationWidgetFactory *> allNavigationFactories();

    void setDisplayName(const QString &displayName);
    void setPriority(int priority);
    void setId(Utils::Id id);
    void setActivationSequence(const QKeySequence &keys);

    QString displayName() const { return m_displayName ; }
    int priority() const { return m_priority; }
    Utils::Id id() const { return m_id; }
    QKeySequence activationSequence() const;

    // This design is not optimal, think about it again once we need to extend it
    // It could be implemented as returning an object which has both the widget
    // and the docktoolbar widgets
    // Similar to how IView
    virtual NavigationView createWidget() = 0;

    virtual void saveSettings(Utils::QtcSettings *settings, int position, QWidget *widget);
    virtual void restoreSettings(Utils::QtcSettings *settings, int position, QWidget *widget);

    virtual void addRootPath(Utils::Id id, const QString &displayName, const QIcon &icon, const Utils::FilePath &path);
    virtual void removeRootPath(Utils::Id id);

private:
    QString m_displayName;
    int m_priority = 0;
    Utils::Id m_id;
    QKeySequence m_activationSequence;
};

} // namespace Core
