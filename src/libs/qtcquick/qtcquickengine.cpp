// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qtcquickengine.h"

#include "qtciconprovider.h"

#include <utils/environment.h>
#include <utils/shutdownguard.h>

#include <QLoggingCategory>
#include <QQmlEngine>
#include <QQuickStyle>

namespace QtcQuick {

static Q_LOGGING_CATEGORY(qmlLog, "qtc.quick", QtWarningMsg)

static const char kImportPathVariable[] = "QTC_QML_IMPORT_PATH";

static QQmlEngine *createEngine()
{
    QQuickStyle::setStyle("QtCreatorStyle");

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
