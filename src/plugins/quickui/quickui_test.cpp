// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "quickui_test.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <qtcquick/aspectcontainermodel.h>
#include <qtcquick/aspectform.h>

#include <utils/algorithm.h>
#include <utils/aspectlist.h>
#include <utils/aspects.h>

#include <QFile>
#include <QQmlError>
#include <QStandardItem>
#include <QQuickItem>
#include <QQuickWidget>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace QuickUi::Internal {

// Runs inside a fully initialised Qt Creator, so it exercises the real
// registered options pages rather than a synthetic container.
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

// Every item in the visual tree whose type name starts with \a component.
static QList<QQuickItem *> findQmlComponents(QQuickItem *root, const QString &component)
{
    QList<QQuickItem *> found;
    if (QString::fromLatin1(root->metaObject()->className()).startsWith(component))
        found << root;
    const QList<QQuickItem *> children = root->childItems();
    for (QQuickItem *child : children)
        found << findQmlComponents(child, component);
    return found;
}

// The button in \a root whose text is \a text.
static QQuickItem *findButton(QQuickItem *root, const QString &text)
{
    const QList<QQuickItem *> buttons = findQmlComponents(root, "Button");
    return Utils::findOr(buttons, nullptr, [&text](QQuickItem *b) {
        return b->property("text").toString() == text;
    });
}

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
    void testStringListEditorAddsRemovesAndEdits();
    void testStringSelectionOffersItsChoices();
    void testAspectListAddsRemovesAndShowsDetails();
    void testTextWithActionShowsSummaryAndActs();
};

void QuickUiTest::testAspectDrivenPagesRenderWithQuick()
{
    Core::setAspectFormFactory([](Utils::AspectContainer *container) {
        return QtcQuick::createAspectForm(container);
    });

    int aspectDriven = 0;
    int renderable = 0;
    int renderedWithQuick = 0;
    QStringList declined;

    for (Core::IOptionsPage *page : Core::IOptionsPage::allOptionsPages()) {
        const std::optional<Utils::AspectContainer *> aspects = page->aspects();
        if (!aspects || !*aspects)
            continue;
        ++aspectDriven;

        Core::IOptionsPageWidget *widget = page->createWidget();
        if (!widget)
            continue;
        auto quickWidget = widget->findChild<QQuickWidget *>();

        // The form takes every page it can show fully and declines the rest,
        // which keep their widget layout. Neither outcome may be arbitrary.
        const bool pageIsRenderable
            = QtcQuick::AspectContainerModel::isFullyRenderable(*aspects)
              || !(*aspects)->qmlSource().isEmpty();
        renderable += pageIsRenderable ? 1 : 0;
        QCOMPARE(bool(quickWidget), pageIsRenderable);

        if (!quickWidget) {
            declined << page->displayName();
            continue;
        }
        // A QQuickWidget whose component failed to load has no root object,
        // and a page showing nothing is not a page rendered with Quick.
        if (!quickWidget->rootObject()) {
            const QStringList errors
                = Utils::transform(quickWidget->errors(), &QQmlError::toString);
            QFAIL(qPrintable(page->displayName() + ": " + errors.join("; ")));
        }
        // The point of declining a page is that an accepted one shows every
        // control. Checked against the rendered tree rather than against the
        // same predicate the factory used to decide.
        QVERIFY2(!findQmlComponent(quickWidget->rootObject(), "UnsupportedDelegate"),
                 qPrintable(page->displayName() + " renders a placeholder"));

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

    qInfo().noquote() << "aspect-driven pages:" << aspectDriven
                      << "rendered with Qt Quick:" << renderedWithQuick
                      << "\n  still on widgets:" << declined.join(", ");

    // How many pages exist, and how many of them the form can show, depends on
    // which plugins this run loads - QuickUi's dependency closure unless -load
    // is given, and every page in that closure happens to be declined. So the
    // count to assert is that the form took every page it could, not that it
    // took any particular number.
    QVERIFY(aspectDriven > 0);
    QCOMPARE(renderedWithQuick, renderable);

    Core::setAspectFormFactory({});
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

    // No fill callback, so it has no choices to describe and no way to say
    // which of them is current - its value is a choice id.
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

void QuickUiTest::testStringListEditorAddsRemovesAndEdits()
{
    Utils::AspectContainer page;
    Utils::StringListAspect list(&page);
    list.setLabelText("Entries");
    list.setValue({"alpha", "beta"});

    const std::unique_ptr<QWidget> form(QtcQuick::createAspectForm(&page));
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *editor = nullptr;
    QTRY_VERIFY(editor = findQmlComponent(quickWidget->rootObject(), "StringListEditorDelegate"));

    QQuickItem *view = findQmlComponent(editor, "QQuickListView");
    QVERIFY(view);
    QCOMPARE(view->property("count").toInt(), 2);

    // Nothing is selected yet, so there is nothing to remove.
    QQuickItem *remove = findButton(editor, "Remove");
    QVERIFY(remove);
    QVERIFY(!remove->property("enabled").toBool());

    // Add appends an empty row, which is what the widget editor does before
    // starting the edit on it.
    QQuickItem *add = findButton(editor, "Add");
    QVERIFY(add);
    QMetaObject::invokeMethod(add, "clicked");
    QCOMPARE(list.volatileValue(), QStringList({"alpha", "beta", ""}));

    // Editing a row writes the whole list back.
    QList<QQuickItem *> fields;
    QTRY_COMPARE((fields = findQmlComponents(editor, "TextField")).size(), 3);
    fields.at(2)->setProperty("text", "gamma");
    QMetaObject::invokeMethod(fields.at(2), "editingFinished");
    QCOMPARE(list.volatileValue(), QStringList({"alpha", "beta", "gamma"}));

    // Remove takes out the current row. Add left the new row current.
    QVERIFY(remove->property("enabled").toBool());
    view->setProperty("currentIndex", 0);
    QVERIFY(remove->property("enabled").toBool());
    QMetaObject::invokeMethod(remove, "clicked");
    QCOMPARE(list.volatileValue(), QStringList({"beta", "gamma"}));
}

void QuickUiTest::testStringSelectionOffersItsChoices()
{
    Utils::AspectContainer page;
    Utils::StringSelectionAspect selection(&page);
    selection.setLabelText("Pick");
    selection.setFillCallback([](const Utils::StringSelectionAspect::ResultCallback &cb) {
        const auto item = [](const QString &display, const QString &id) {
            auto i = new QStandardItem(display);
            i->setData(id);
            return i;
        };
        cb({item("First", "first"), item("Second", "second")});
    });
    selection.setValue("second");

    // The choices come from a fill callback, so the aspect has to run it itself
    // for a form that never builds a widget.
    const Utils::AspectPresentation p = selection.presentation();
    QCOMPARE(p.choices.size(), 2);
    QVERIFY(p.valueIsChoiceId);
    QCOMPARE(p.choices.at(1).display, QString("Second"));
    QCOMPARE(p.choices.at(1).id.toString(), QString("second"));

    const std::unique_ptr<QWidget> form(QtcQuick::createAspectForm(&page));
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *combo = nullptr;
    QTRY_VERIFY(combo = findQmlComponent(quickWidget->rootObject(), "ComboBox"));
    QCOMPARE(combo->property("count").toInt(), 2);
    QCOMPARE(combo->property("currentIndex").toInt(), 1);

    QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 0));
    QCOMPARE(selection.volatileValue(), QString("first"));
}

void QuickUiTest::testAspectListAddsRemovesAndShowsDetails()
{
    Utils::AspectContainer page;
    Utils::AspectList servers(&page);
    servers.setLabelText("Servers");
    servers.setDisplayStyle(Utils::AspectList::DisplayStyle::ListViewWithDetails);
    servers.listViewDataCallback = [](Utils::BaseAspect *item, int) -> QVariant {
        return item->displayName();
    };
    servers.setCreateItemFunction([] {
        auto item = std::make_shared<Utils::AspectContainer>();
        item->setDisplayName("A server");
        auto flag = new Utils::BoolAspect(item.get());
        flag->setLabelText("Enabled");
        return item;
    });

    const std::unique_ptr<QWidget> form(QtcQuick::createAspectForm(&page));
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "AspectListDelegate"));
    QQuickItem *view = findQmlComponent(delegate, "QQuickListView");
    QVERIFY(view);
    QCOMPARE(view->property("count").toInt(), 0);

    // Add creates an item through the aspect and selects it.
    QQuickItem *add = findButton(delegate, "Add");
    QVERIFY(add);
    QMetaObject::invokeMethod(add, "clicked");
    QCOMPARE(servers.volatileItems().size(), 1);
    QTRY_COMPARE(view->property("count").toInt(), 1);
    QCOMPARE(view->property("currentIndex").toInt(), 0);

    // The details pane shows the selected item's own aspects.
    QTRY_VERIFY(findQmlComponent(delegate, "BoolDelegate"));

    QQuickItem *remove = findButton(delegate, "Remove");
    QVERIFY(remove);
    QMetaObject::invokeMethod(remove, "clicked");
    QCOMPARE(servers.volatileItems().size(), 0);
    QTRY_COMPARE(view->property("count").toInt(), 0);
    QVERIFY(!findQmlComponent(delegate, "BoolDelegate"));
}

// The shape EnvironmentChangesAspect has: a summary of the value and one
// button, because it is edited through a dialog.
class SummaryWithAction final : public Utils::StringAspect
{
public:
    using Utils::StringAspect::StringAspect;

    int actions = 0;

    Utils::AspectPresentation presentation() const override
    {
        Utils::AspectPresentation p = Utils::StringAspect::presentation();
        p.control = Utils::AspectControls::TextWithAction;
        p.actionText = "Change...";
        return p;
    }

    QString displayText() const override { return "summary of " + volatileValue(); }
    void triggerAction() override { ++actions; }
};

void QuickUiTest::testTextWithActionShowsSummaryAndActs()
{
    Utils::AspectContainer page;
    SummaryWithAction aspect(&page);
    aspect.setLabelText("Environment");
    aspect.setValue("one change");

    const std::unique_ptr<QWidget> form(QtcQuick::createAspectForm(&page));
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "TextWithActionDelegate"));

    // The summary comes from the aspect, not from a model snapshot, so it
    // follows the value.
    const QList<QQuickItem *> labels = findQmlComponents(delegate, "Label");
    QVERIFY(Utils::anyOf(labels, [](QQuickItem *l) {
        return l->property("text").toString() == "summary of one change";
    }));

    QQuickItem *button = findButton(delegate, "Change...");
    QVERIFY(button);
    QMetaObject::invokeMethod(button, "clicked");
    QCOMPARE(aspect.actions, 1);
}

QObject *createQuickUiTest()
{
    return new QuickUiTest;
}

} // namespace QuickUi::Internal

#include "quickui_test.moc"
