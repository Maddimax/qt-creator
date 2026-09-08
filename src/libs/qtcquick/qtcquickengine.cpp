// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qtcquickengine.h"

#include "qtciconprovider.h"
#include "style/qtcquickstyle.h"

#include <utils/environment.h>
#include <utils/shutdownguard.h>

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QQmlEngine>

namespace QtcQuick {

static Q_LOGGING_CATEGORY(qmlLog, "qtc.quick", QtWarningMsg)

static const char kImportPathVariable[] = "QTC_QML_IMPORT_PATH";

// Qt Quick Controls latch onto a style the first time one of them is loaded,
// and refuse to change afterwards. The engine below is created on first use,
// which is not necessarily before some other engine in the process has drawn
// a control - so the style is set when the application starts instead, which
// is early enough for every engine in it.
//
// The call is what keeps QtcQuickStyle linked, so it has to happen from here
// rather than from a startup function inside that library; see its header.
static void setStyleAtStartup()
{
    setQtCreatorStyle();
}
Q_COREAPP_STARTUP_FUNCTION(setStyleAtStartup)

static QQmlEngine *createEngine()
{
    auto engine = new QQmlEngine;
    if (Utils::qtcEnvironmentVariableIsSet(kImportPathVariable))
        engine->addImportPath(Utils::qtcEnvironmentVariable(kImportPathVariable));

    engine->addImageProvider(QLatin1String(IconProvider::name()), new IconProvider);

    QObject::connect(engine, &QQmlEngine::warnings, engine, [](const QList<QQmlError> &errors) {
        for (const QQmlError &error : errors)
            qCWarning(qmlLog) << qUtf8Printable(error.toString());
    });
    return engine;
}

QQmlEngine *engine()
{
    static Utils::GuardedObject<QQmlEngine> theEngine(createEngine());
    return theEngine.get();
}

} // namespace QtcQuick
