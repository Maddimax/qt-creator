// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../core_global.h"

#include <utils/aspects.h>
#include <utils/id.h>
#include <utils/result.h>

#include <QWidget>

#include <functional>
#include <memory>

namespace Core {

namespace Internal {
class IOptionsPageWidgetPrivate;
class IOptionsPagePrivate;
class IOptionsPageProviderPrivate;
} // namespace Internal

class CORE_EXPORT IOptionsPageWidget : public QWidget
{
    Q_OBJECT

public:
    IOptionsPageWidget();
    ~IOptionsPageWidget();

    void setOnApply(const std::function<void()> &func);
    void setOnCancel(const std::function<void()> &func);
    void setDirtyChecker(const std::function<bool()> &func);

    virtual void apply();
    virtual void cancel();
    virtual bool isDirty() const;

private:
    friend class Internal::IOptionsPagePrivate;
    std::unique_ptr<Internal::IOptionsPageWidgetPrivate> d;
};

// Installed by a front end that can render an AspectContainer itself, e.g. in
// Qt Quick. Consulted before the container's own layouter. Returning nullptr
// falls back to the widget path.
using AspectFormFactory = std::function<QWidget *(Utils::AspectContainer *)>;
CORE_EXPORT void setAspectFormFactory(const AspectFormFactory &factory);

// A settings form for a container, for the places that are not options pages -
// a project panel showing the same settings per project. Uses the factory
// where it takes the container and the container's own layouter otherwise, so
// that both surfaces show the same thing.
CORE_EXPORT QWidget *createAspectForm(Utils::AspectContainer *container);

// The same, for a container that has no page of its own to name: its aspects
// are listed generically, in order, the way an unported page used to be drawn
// with widgets. Null where Qt Quick is not there to draw it, so a caller can
// keep whatever it did before.
CORE_EXPORT void setGenericAspectFormFactory(const AspectFormFactory &factory);
CORE_EXPORT QWidget *createGenericAspectForm(Utils::AspectContainer *container);

#ifdef WITH_TESTS
// Builds \a container's Qt Quick form and reports what is wrong with it, or
// nothing when it is fine. For the containers no census walks - a dialog is not
// registered anywhere and cannot be enumerated - so what a settings page gets
// from QuickUiTest has to be asked for by hand, once per dialog.
//
// Three things go wrong and only the first is loud on its own:
//   - the container names no QML, or the file will not load, and the form is
//     silently the widget layout instead;
//   - it loads with a status other than Ready;
//   - it loads perfectly and a binding names an aspect that is not there, which
//     the engine reports as a warning and nothing else notices. The control is
//     simply missing from the form.
//
// \a qmlFileName is the file the complaints have to be about: a warning from
// some other component is not this form's fault. Lives here rather than in
// QtcQuick because every plugin already links Core, and asks the form by
// property name rather than by type so that none of them has to link Qt Quick
// to run the check.
CORE_EXPORT Utils::Result<> aspectFormRenders(Utils::AspectContainer *container,
                                              const QString &qmlFileName);

// The root of the object tree a Qt Quick form was drawn from, so a plugin can
// drive its own form without linking Qt Quick: from here,
// findChild<QObject *>("someObjectName") reaches the QML items, and
// QMetaObject::invokeMethod() and property() do the rest.
//
// A provider rather than a search, because there is nothing to search: the QML
// objects are not QObject children of the widget - measured, a rendered form
// has exactly one child object and it is not the root item - so only the front
// end that drew it can say what its root is.
//
// Null for a widget form, and before a front end has installed a provider.
CORE_EXPORT QObject *aspectFormRoot(QWidget *form);

using AspectFormRootProvider = std::function<QObject *(QWidget *)>;
CORE_EXPORT void setAspectFormRootProvider(const AspectFormRootProvider &provider);

QObject *createAspectFormRendersTest();
#endif

class CORE_EXPORT IOptionsPage
{
    Q_DISABLE_COPY_MOVE(IOptionsPage)

public:
    explicit IOptionsPage(bool registerGlobally = true);
    virtual ~IOptionsPage();

    static void registerCategory(
        Utils::Id id, const QString &displayName, const Utils::FilePath &iconPath);
    static const QList<IOptionsPage *> allOptionsPages();

    IOptionsPageWidget *createWidget();
    void deleteWidget();

    Utils::Id id() const;
    QString displayName() const;
    Utils::Id category() const;
    QString displayCategory() const;
    Utils::FilePath categoryIconPath() const;
    bool recreateOnCancel() const;
    bool autoApply() const;

    std::optional<Utils::AspectContainer *> aspects() const;
    bool matches(const QRegularExpression &regexp) const;

protected:
    void setId(Utils::Id id);
    void setDisplayName(const QString &displayName);
    void setCategory(Utils::Id category);
    void setSettingsProvider(const std::function<Utils::AspectContainer *()> &provider);
    void setWidgetCreator(const std::function<IOptionsPageWidget *()> &widgetCreator);
    void setFixedKeywords(const QStringList &);
    void setRecreateOnCancel(bool on);
    void setAutoApply();

private:
    std::unique_ptr<Internal::IOptionsPagePrivate> d;
};

/*
    Alternative way for providing option pages instead of adding IOptionsPage
    objects into the plugin manager pool. Should only be used if creation of the
    actual option pages is not possible or too expensive at Qt Creator startup.
    (Like the designer integration, which needs to initialize designer plugins
    before the options pages get available.)
*/

class CORE_EXPORT IOptionsPageProvider
{
    Q_DISABLE_COPY_MOVE(IOptionsPageProvider)

public:
    IOptionsPageProvider();
    virtual ~IOptionsPageProvider();

    static const QList<IOptionsPageProvider *> allOptionsPagesProviders();

    Utils::Id category() const;
    QString displayCategory() const;
    Utils::FilePath categoryIconPath() const;

    virtual QList<IOptionsPage *> pages() const = 0;
    virtual bool matches(const QRegularExpression &regexp) const = 0;

protected:
    void setCategory(Utils::Id category);
    void setDisplayCategory(const QString &displayCategory);
    void setCategoryIconPath(const Utils::FilePath &iconPath);

    std::unique_ptr<Internal::IOptionsPageProviderPrivate> d;
};

// Which part of the settings page to pre-select, if applicable. In practice, this will
// usually be an item in some sort of (list) view.
void CORE_EXPORT setPreselectedOptionsPageItem(Utils::Id page, Utils::Id item);
Utils::Id CORE_EXPORT preselectedOptionsPageItem(Utils::Id page);

} // namespace Core
