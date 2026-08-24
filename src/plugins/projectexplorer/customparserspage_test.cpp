// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "customparserspage_test.h"

#include "customparser.h"
#include "projectexplorerconstants.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/algorithm.h>
#include <utils/aspectpresentation.h>
#include <utils/aspects.h>

#include <QAbstractItemModel>
#include <QTest>

using namespace Utils;

namespace ProjectExplorer::Internal {

class CustomParsersPageTest final : public QObject
{
    Q_OBJECT

private:
    static AspectContainer *pageAspects()
    {
        Core::IOptionsPage *page = findOr(
            Core::IOptionsPage::allOptionsPages(), nullptr, [](Core::IOptionsPage *candidate) {
                return candidate->id() == Constants::CUSTOM_PARSERS_SETTINGS_PAGE_ID;
            });
        if (!page)
            return nullptr;
        const std::optional<AspectContainer *> aspects = page->aspects();
        return aspects ? *aspects : nullptr;
    }

    static BaseAspect *aspectNamed(const AspectContainer *container, const QString &name)
    {
        const QList<BaseAspect *> aspects = container->aspects();
        for (BaseAspect *aspect : aspects) {
            if (aspect->qmlName() == name)
                return aspect;
        }
        return nullptr;
    }

    static CustomParserSettings parser(const QString &name)
    {
        CustomParserSettings s;
        s.id = Id::fromString(name);
        s.displayName = name;
        return s;
    }

private slots:
    // What the table shows of a parser, and that acting on a selection reaches
    // the page: the buttons beside the table are the page's aspects, and only
    // the table knows what is selected.
    void testSelectingRowsDecidesWhatCanBeDone()
    {
        AspectContainer *aspects = pageAspects();
        QVERIFY(aspects);

        auto parsers = aspectNamed(aspects, "Parsers");
        BaseAspect *edit = aspectNamed(aspects, "EditParser");
        BaseAspect *exportAction = aspectNamed(aspects, "ExportParsers");
        QVERIFY(parsers);
        QVERIFY(edit);
        QVERIFY(exportAction);

        const QList<CustomParserSettings> original = CustomParsers::parsersAvailableInProject(
            nullptr);
        CustomParsers::set({parser("First"), parser("Second")});

        QAbstractItemModel *model = parsers->tableModel();
        QVERIFY(model);
        QCOMPARE(model->columnCount(), 3);
        QTRY_COMPARE(model->rowCount(), 2);
        QCOMPARE(model->index(0, 0).data().toString(), QString("First"));
        // The two defaults are check boxes, and the name is typed.
        QVERIFY(model->index(0, 1).data(AspectTable::CheckableRole).toBool());
        QVERIFY(!model->index(0, 0).data(AspectTable::CheckableRole).toBool());
        QVERIFY(model->index(0, 0).data(AspectTable::EditableRole).toBool());

        // Built element by element: QVariantList{0} is the size-one-list
        // constructor as easily as the one-element one, and an empty list is
        // not what this is testing.
        const auto rows = [](std::initializer_list<int> values) {
            QVariantList list;
            for (int value : values)
                list.append(QVariant(value));
            return list;
        };

        // Nothing selected: nothing to edit and nothing to export.
        QVERIFY(QMetaObject::invokeMethod(parsers, "setSelectedRows",
                                          Q_ARG(QVariantList, rows({}))));
        QVERIFY(!edit->isEnabled());
        QVERIFY(!exportAction->isEnabled());

        // One: both.
        QVERIFY(QMetaObject::invokeMethod(parsers, "setSelectedRows",
                                          Q_ARG(QVariantList, rows({0}))));
        QVERIFY(edit->isEnabled());
        QVERIFY(exportAction->isEnabled());

        // Two: exporting takes them both, and the dialog edits one parser.
        QVERIFY(QMetaObject::invokeMethod(parsers, "setSelectedRows",
                                          Q_ARG(QVariantList, rows({0, 1}))));
        QVERIFY(!edit->isEnabled());
        QVERIFY(exportAction->isEnabled());

        CustomParsers::set(original);
    }

    // Removing is deferred, and an auto-imported parser is not the user's to
    // take away.
    void testRemovingIsDeferredAndRefusesReadOnlyRows()
    {
        AspectContainer *aspects = pageAspects();
        QVERIFY(aspects);
        auto parsers = aspectNamed(aspects, "Parsers");
        QVERIFY(parsers);

        const QList<CustomParserSettings> original = CustomParsers::parsersAvailableInProject(
            nullptr);
        CustomParserSettings readOnly = parser("Imported");
        readOnly.readOnly = true;
        CustomParsers::set({parser("Mine"), readOnly});

        QAbstractItemModel *model = parsers->tableModel();
        QTRY_COMPARE(model->rowCount(), 2);

        QVERIFY(!model->removeRows(1, 1));
        QCOMPARE(model->rowCount(), 2);

        QVERIFY(model->removeRows(0, 1));
        QCOMPARE(model->rowCount(), 1);
        // Not committed until apply(): the parsers themselves still have both.
        QCOMPARE(CustomParsers::parsersAvailableInProject(nullptr).size(), 2);
        QVERIFY(parsers->isDirty());

        parsers->cancel();
        QTRY_COMPARE(model->rowCount(), 2);
        QVERIFY(!parsers->isDirty());

        CustomParsers::set(original);
    }
};

QObject *createCustomParsersPageTest()
{
    return new CustomParsersPageTest;
}

} // namespace ProjectExplorer::Internal

#include "customparserspage_test.moc"
