// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QtGlobal>

QT_BEGIN_NAMESPACE
class QObject;
QT_END_NAMESPACE

namespace TextEditor::Internal {

void setupSnippetsSettings();

#ifdef WITH_TESTS
QObject *createSnippetsSettingsTest();
#endif

} // TextEditor::Internal
