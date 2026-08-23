// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "quickui_test.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <qtcquick/aspectcontainermodel.h>
#include <qtcquick/aspectform.h>

#include <utils/algorithm.h>
#include <utils/aspects.h>

#include <QFile>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWidget>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace QuickUi::Internal {

// Runs inside a fully initialised Qt Creator, so it exercises the real
// registered options pages rather than a synthetic container.
class QuickUiTest final : public QObject
{
    Q_OBJECT

private slots:
    void testAspectDrivenPagesRenderWithQuick();
    void testNestedContainerIsAModelGroup();
    void testSelectionWithoutDescribedChoicesIsUnsupported();
    void testNestedContainerRendersAsGroup();
    void testQmlNameIsDerivedFromTheSettingsKey();
    void testPageQmlReachesItsAspectsByName();
    void testLabelChangeReachesTheControl();
    void testIdValuedSelectionRoundTrips();
};

void QuickUiTest::testAspectDrivenPagesRenderWithQuick()
{
    Core::setAspectFormFactory([](Utils::AspectContainer *container) {
        return QtcQuick::createAspectForm(container);
    });

    int aspectDriven = 0;
    int renderedWithQuick = 0;

    for (Core::IOptionsPage *page : Core::IOptionsPage::allOptionsPages()) {
        const std::optional<Utils::AspectContainer *> aspects = page->aspects();
        if (!aspects || !*aspects)
            continue;
        ++aspectDriven;

        Core::IOptionsPageWidget *widget = page->createWidget();
        if (!widget)
            continue;
        auto quickWidget = widget->findChild<QQuickWidget *>();
        if (!quickWidget)
            continue;
        // A QQuickWidget whose component failed to load has no root object,
        // and a page showing nothing is not a page rendered with Quick.
        if (!quickWidget->rootObject()) {
            const QStringList errors
                = Utils::transform(quickWidget->errors(), &QQmlError::toString);
            QFAIL(qPrintable(page->displayName() + ": " + errors.join("; ")));
        }
        // A page that names a QML file must render through it, not fall back to
        // the generic form. The root object's type is named after the file.
        if (const QUrl source = (*aspects)->qmlSource(); !source.isEmpty()) {
            const QString component = source.fileName().chopped(strlen(".qml"));
            QVERIFY2(QString::fromLatin1(
                         quickWidget->rootObject()->metaObject()->className())
                         .startsWith(component),
                     qPrintable(page->displayName() + " did not render " + component));
        }
        ++renderedWithQuick;
    }

    qInfo() << "aspect-driven pages:" << aspectDriven
            << "rendered with Qt Quick:" << renderedWithQuick;

    // How many aspect-driven pages exist depends on which plugins this test run
    // loads, which is only QuickUi's dependency closure. The invariant is that
    // every one of them goes through the Qt Quick path.
    QVERIFY(aspectDriven > 0);
    QCOMPARE(renderedWithQuick, aspectDriven);

    Core::setAspectFormFactory({});
}

// The first item in the visual tree whose type name starts with \a component.
// A QML component's class name is its own file name plus a suffix, so this
// finds delegates by the file that declares them. findChildren() is no use
// here: a Repeater's delegates are visual children of its parent but not
// QObject children of it.
static QQuickItem *findQmlComponent(QQuickItem *root, const QString &component)
{
    if (QString::fromLatin1(root->metaObject()->className()).startsWith(component))
        return root;
    const QList<QQuickItem *> children = root->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *found = findQmlComponent(child, component))
            return found;
    }
    return nullptr;
}

void QuickUiTest::testNestedContainerIsAModelGroup()
{
    Utils::AspectContainer page;
    Utils::AspectContainer group(&page);
    group.setLabelText("Group title");
    Utils::BoolAspect flag(&group);

    QtcQuick::AspectContainerModel model(&page);
    QCOMPARE(model.rowCount(), 1);
    const QModelIndex index = model.index(0, 0);
    QCOMPARE(index.data(QtcQuick::AspectContainerModel::KindRole).toInt(),
             int(QtcQuick::AspectContainerModel::Container));
    auto child = index.data(QtcQuick::AspectContainerModel::ChildModelRole)
                     .value<QtcQuick::AspectContainerModel *>();
    QVERIFY(child);
    QCOMPARE(child->rowCount(), 1);
    QCOMPARE(child->index(0, 0).data(QtcQuick::AspectContainerModel::KindRole).toInt(),
             int(QtcQuick::AspectContainerModel::Bool));
}

void QuickUiTest::testSelectionWithoutDescribedChoicesIsUnsupported()
{
    Utils::AspectContainer page;

    Utils::SelectionAspect described(&page);
    described.addOption("One");
    described.addOption("Two");

    // Its choices come from an async fill callback, not from presentation().
    Utils::StringSelectionAspect undescribed(&page);

    QtcQuick::AspectContainerModel model(&page);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.index(0, 0).data(QtcQuick::AspectContainerModel::KindRole).toInt(),
             int(QtcQuick::AspectContainerModel::Selection));
    QCOMPARE(model.index(1, 0).data(QtcQuick::AspectContainerModel::KindRole).toInt(),
             int(QtcQuick::AspectContainerModel::Unsupported));
}

void QuickUiTest::testNestedContainerRendersAsGroup()
{
    // A page expresses grouping by nesting containers, not by a Layouting
    // closure: the nested container's label is the group title.
    Utils::AspectContainer page;
    Utils::AspectContainer group(&page);
    group.setLabelText("Group title");
    Utils::BoolAspect flag(&group);
    flag.setLabelText("Flag");

    const std::unique_ptr<QWidget> form(QtcQuick::createAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QQuickItem *rootItem = quickWidget->rootObject();
    QVERIFY(rootItem);

    // The delegates are created when the component completes.
    QQuickItem *groupItem = nullptr;
    QTRY_VERIFY(groupItem = findQmlComponent(rootItem, "GroupDelegate"));
    QCOMPARE(groupItem->property("title").toString(), QString("Group title"));

    // The aspect is inside the group, not merely somewhere on the page: that
    // is what makes the nesting - and so the grouping - real.
    QVERIFY(findQmlComponent(groupItem, "BoolDelegate"));
    QVERIFY(!findQmlComponent(groupItem, "UnsupportedDelegate"));
}

void QuickUiTest::testQmlNameIsDerivedFromTheSettingsKey()
{
    Utils::BoolAspect slashSeparated;
    slashSeparated.setSettingsKey("General/ShowShortcutsInContextMenu");
    QCOMPARE(slashSeparated.qmlName(), QString("ShowShortcutsInContextMenu"));

    Utils::BoolAspect dotSeparated;
    dotSeparated.setSettingsKey("QdbRunConfig.RemoteExecutable");
    QCOMPARE(dotSeparated.qmlName(), QString("RemoteExecutable"));

    Utils::BoolAspect bare;
    bare.setSettingsKey("binary");
    QCOMPARE(bare.qmlName(), QString("binary"));

    // Nothing to derive from, so nothing to reach it by.
    Utils::BoolAspect keyless;
    QVERIFY(keyless.qmlName().isEmpty());

    // An explicit name wins over the derived one.
    slashSeparated.setQmlName("showShortcuts");
    QCOMPARE(slashSeparated.qmlName(), QString("showShortcuts"));
}

void QuickUiTest::testPageQmlReachesItsAspectsByName()
{
    Utils::AspectContainer page;
    Utils::BoolAspect flag(&page);
    flag.setSettingsKey("Test/TheFlag");
    flag.setLabelText("The flag");
    flag.setValue(true);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pageQml = dir.filePath("TestPage.qml");
    {
        QFile file(pageQml);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"(
import QtQuick
import QtCreator.Ui

AspectPage {
    BoolDelegate { aspect: aspects.TheFlag }
}
)");
    }
    page.setQmlSource(QUrl::fromLocalFile(pageQml));

    const std::unique_ptr<QWidget> form(QtcQuick::createAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    if (!quickWidget->rootObject()) {
        const QStringList errors = Utils::transform(quickWidget->errors(), &QQmlError::toString);
        QFAIL(qPrintable(errors.join("; ")));
    }

    // The page's own component is the root, not the generic AspectForm. Without
    // this the generic fallback satisfies everything below.
    QVERIFY(QString::fromLatin1(quickWidget->rootObject()->metaObject()->className())
                .startsWith("TestPage"));

    // The page named the aspect, so the delegate is bound to the real one.
    QQuickItem *delegate = findQmlComponent(quickWidget->rootObject(), "BoolDelegate");
    QVERIFY(delegate);
    QCOMPARE(delegate->property("checked").toBool(), true);
    QCOMPARE(delegate->property("text").toString(), QString("The flag"));
}

void QuickUiTest::testLabelChangeReachesTheControl()
{
    Utils::AspectContainer page;
    Utils::BoolAspect flag(&page);
    flag.setLabelText("Before");

    const std::unique_ptr<QWidget> form(QtcQuick::createAspectForm(&page));
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "BoolDelegate"));
    QCOMPARE(delegate->property("text").toString(), QString("Before"));

    // The delegates read the label from the aspect's Q_PROPERTY, so a change
    // follows its NOTIFY signal. It used to come from a model role, and the
    // model emits no dataChanged, so this never updated.
    flag.setLabelText("After");
    QCOMPARE(delegate->property("text").toString(), QString("After"));

    flag.setVisible(false);
    QCOMPARE(delegate->property("visible").toBool(), false);
}

// The shape EncodingSelectionAspect has: a ComboBox whose value is the id of
// the choice, not its index.
class IdValuedSelection final : public Utils::ByteArrayAspect
{
public:
    using Utils::ByteArrayAspect::ByteArrayAspect;

    Utils::AspectPresentation presentation() const override
    {
        Utils::AspectPresentation p = Utils::ByteArrayAspect::presentation();
        p.control = Utils::AspectControls::ComboBox;
        p.valueIsChoiceId = true;
        p.choices.append({"First", {}, true, QByteArray("first")});
        p.choices.append({"Second", {}, true, QByteArray("second")});
        return p;
    }
};

void QuickUiTest::testIdValuedSelectionRoundTrips()
{
    Utils::AspectContainer page;
    IdValuedSelection selection(&page);
    selection.setValue("second");

    const std::unique_ptr<QWidget> form(QtcQuick::createAspectForm(&page));
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *combo = nullptr;
    QTRY_VERIFY(combo = findQmlComponent(quickWidget->rootObject(), "ComboBox"));

    // The id selects the row, rather than being read as an index.
    QCOMPARE(combo->property("currentIndex").toInt(), 1);

    // And a pick writes the id back, not the index.
    QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 0));
    QCOMPARE(selection.volatileValue(), QByteArray("first"));
}

QObject *createQuickUiTest()
{
    return new QuickUiTest;
}

} // namespace QuickUi::Internal

#include "quickui_test.moc"
