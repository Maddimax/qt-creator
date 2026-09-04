// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#ifdef WITH_TESTS
#include <QObject>
#endif

namespace Core { class IDocument; }

namespace LanguageClient {

class Client;

void setupCallHierarchyFactory();
bool supportsCallHierarchy(Client *client, const Core::IDocument *document);

void setupTypeHierarchyFactory();
bool supportsTypeHierarchy(Client *client, const Core::IDocument *document);

#ifdef WITH_TESTS
QObject *createHierarchyInAnyViewTest();
#endif

} // namespace LanguageClient
