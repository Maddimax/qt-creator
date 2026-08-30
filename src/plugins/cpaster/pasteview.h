// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QList>

QT_BEGIN_NAMESPACE
class QObject;
QT_END_NAMESPACE

namespace CodePaster {

#ifdef WITH_TESTS
QObject *createPasteViewTest();
#endif

class PasteInputData;
class Protocol;

std::optional<PasteInputData> executePasteDialog(const QList<Protocol *> &protocols,
                                                 const QString &data, const QString &mimeType);

} // CodePaster
