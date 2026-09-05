// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/guitest.h>

#include <QObject>

#include <memory>

namespace CppEditor::Internal::Tests {

class FollowSymbolTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void testSwitchMethodDeclDef_data();
    void testSwitchMethodDeclDef();

    void testFollowSymbolMultipleDocuments_data();
    void testFollowSymbolMultipleDocuments();

    void testFollowSymbol_data();
    void testFollowSymbol();

    void testFollowSymbolQTCREATORBUG7903_data();
    void testFollowSymbolQTCREATORBUG7903();

    void testFollowCall_data();
    void testFollowCall();

    void testFollowSymbolQObjectConnect_data();
    void testFollowSymbolQObjectConnect();
    void testFollowSymbolQObjectOldStyleConnect();

    void testFollowClassOperatorOnOperatorToken_data();
    void testFollowClassOperatorOnOperatorToken();

    void testFollowClassOperator_data();
    void testFollowClassOperator();

    void testFollowClassOperatorInOp_data();
    void testFollowClassOperatorInOp();

    void testFollowVirtualFunctionCall_data();
    void testFollowVirtualFunctionCall();
    void testFollowVirtualFunctionCallMultipleDocuments();

    void testFollowSymbolWithoutAnEditorWidget();

private:
    // What this class must not print. Not "no warnings at all": Qt's own
    // animation driver says something one run in five, and a gate that benign
    // noise trips is a gate that gets removed.
    std::unique_ptr<Utils::GuiTest::CollectedWarnings> m_mustNotSay;
};

} // namespace CppEditor::Internal::Tests
