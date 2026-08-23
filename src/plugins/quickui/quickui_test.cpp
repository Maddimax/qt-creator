// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "quickui_test.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/secretaspect.h>

#include <qtcquick/aspectmodels.h>
#include <qtcquick/aspectcontainermodel.h>
#include <qtcquick/aspectform.h>
#include <qtcquick/namedaspects.h>

#include <utils/algorithm.h>
#include <utils/aspectlist.h>
#include <utils/aspects.h>
#include <utils/layoutbuilder.h>

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QFile>
#include <QQmlError>
#include <QStandardItem>
#include <QFont>
#include <QPromise>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QMetaEnum>
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

// Every item in the visual tree that declares an "aspect" property, which is
// what makes an item one of our delegates.
static QList<QQuickItem *> findAspectDelegates(QQuickItem *root)
{
    QList<QQuickItem *> found;
    if (root->metaObject()->indexOfProperty("aspect") >= 0)
        found << root;
    const QList<QQuickItem *> children = root->childItems();
    for (QQuickItem *child : children)
        found << findAspectDelegates(child);
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
    void testAspectListLabelArrivingLate();
    void testPageWithoutItsOwnQmlIsDeclined();
    void testPasswordAspectDoesNotEchoItsValue();
    void testAspectListOffersItsExtraButtons();
    void testQmlOnlyContainerStillLaysOutInAWidgetLayout();
    void testAspectVisibilityReachesTheDrawnControl();
    void testAspectQmlNamesAreUsableAndUnique();
    void testActionAspectIsAButtonOnEitherRenderer();
    void testTextDisplayShowsItsMessage();
    void testRadioStyledBoolIsARadioButton();
    void testSpinBoxDrawsItsPrefixAndSuffix();
    void testPageQmlReachesANestedContainersAspects();
    void testMultiLineStringGetsATextArea();
    void testSecretIsNotOfferedAsAPlainPasswordField();
};

void QuickUiTest::testAspectDrivenPagesRenderWithQuick()
{
    Core::setAspectFormFactory([](Utils::AspectContainer *container) {
        return QtcQuick::createAspectForm(container);
    });

    int aspectDriven = 0;
    int withQml = 0;
    int renderedWithQuick = 0;
    int genericWouldDo = 0;
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

        // The factory takes every page that names its own QML and declines the
        // rest, which keep their widget layout. Neither outcome may be
        // arbitrary. How many of the declined ones the generic form could
        // nonetheless show all the aspects of is the porting backlog, reported
        // below: those are the cheap ports, not a promise about what renders.
        const bool pageHasQml = !(*aspects)->qmlSource().isEmpty();
        withQml += pageHasQml ? 1 : 0;
        QCOMPARE(bool(quickWidget), pageHasQml);
        if (!pageHasQml && QtcQuick::AspectContainerModel::isFullyRenderable(*aspects))
            ++genericWouldDo;

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

            // A page QML names its aspects, and a name that does not exist is
            // undefined in QML rather than an error: the delegate is built and
            // shows nothing. So every delegate must have found its aspect, and
            // there must be at least one. Delegates are recognised by declaring
            // an "aspect" property, which is what makes them one.
            const QList<QQuickItem *> delegates = findAspectDelegates(
                quickWidget->rootObject());
            QVERIFY2(!delegates.isEmpty(),
                     qPrintable(page->displayName() + " renders no aspect at all"));
            for (QQuickItem *delegate : delegates) {
                QVERIFY2(!delegate->property("aspect").isNull(),
                         qPrintable(page->displayName() + ": "
                                    + QString::fromLatin1(delegate->metaObject()->className())
                                    + " has no aspect"));
            }
        }
        ++renderedWithQuick;
    }

    qInfo().noquote() << "aspect-driven pages:" << aspectDriven
                      << "rendered with Qt Quick:" << renderedWithQuick
                      << "\n  of the" << declined.size() << "still on widgets,"
                      << genericWouldDo << "have only aspects the generic form knows"
                      << "\n  still on widgets:" << declined.join(", ");

    // How many pages exist, and how many of them the form can show, depends on
    // which plugins this run loads - QuickUi's dependency closure unless -load
    // is given, and every page in that closure happens to be declined. So the
    // count to assert is that the form took every page it could, not that it
    // took any particular number.
    QVERIFY(aspectDriven > 0);
    QCOMPARE(renderedWithQuick, withQml);

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
    QtcQuick::AspectModels models;
    QtcQuick::AspectContainerModel *child = models.container(&group);
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

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
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

void QuickUiTest::testPageWithoutItsOwnQmlIsDeclined()
{
    Utils::AspectContainer page;
    Utils::BoolAspect flag(&page);
    flag.setSettingsKey("Test/Flag");
    flag.setLabelText("Flag");

    // A page is rendered with Quick only once it has been given its own QML,
    // even when every aspect on it is one the generic form can show: what a
    // page shows is the layouter's choice, and the generic form does not know
    // that choice. So this one is declined and keeps its widget layout.
    QVERIFY(QtcQuick::AspectContainerModel::isFullyRenderable(&page));
    QVERIFY(!QtcQuick::createAspectForm(&page));

    // The generic form is still what an unported page looks like on request.
    const std::unique_ptr<QWidget> generic(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(generic);
    auto quickWidget = generic->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());
    QVERIFY(findQmlComponent(quickWidget->rootObject(), "BoolDelegate"));
}

void QuickUiTest::testPasswordAspectDoesNotEchoItsValue()
{
    Utils::AspectContainer page;
    Utils::StringAspect secret(&page);
    secret.setDisplayStyle(Utils::StringAspect::PasswordLineEditDisplay);
    secret.setLabelText("Password");
    secret.setValue("hunter2");
    Utils::StringAspect plain(&page);
    plain.setDisplayStyle(Utils::StringAspect::LineEditDisplay);
    plain.setLabelText("Name");

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QQuickItem *rootItem = quickWidget->rootObject();
    QVERIFY(rootItem);

    // A password shares the String kind, and so the delegate, with an ordinary
    // line edit. It must still not draw what it holds.
    QList<QQuickItem *> delegates;
    QTRY_COMPARE((delegates = findQmlComponents(rootItem, "StringDelegate")).size(), 2);
    QQuickItem *secretField = findQmlComponent(delegates.at(0), "TextField");
    QQuickItem *plainField = findQmlComponent(delegates.at(1), "TextField");
    QVERIFY(secretField);
    QVERIFY(plainField);
    const QMetaObject *mo = secretField->metaObject();
    const QMetaEnum echoModes = mo->property(mo->indexOfProperty("echoMode")).enumerator();
    const int password = echoModes.keyToValue("Password");
    const int normal = echoModes.keyToValue("Normal");
    QVERIFY(password != -1 && normal != -1);
    QCOMPARE(secretField->property("echoMode").toInt(), password);
    QCOMPARE(secretField->property("text").toString(), QString("hunter2"));
    QCOMPARE(plainField->property("echoMode").toInt(), normal);
}

void QuickUiTest::testAspectListOffersItsExtraButtons()
{
    Utils::AspectContainer page;
    page.setAutoApply(false);
    Utils::AspectList servers(&page);
    servers.setLabelText("Servers");
    servers.setDisplayStyle(Utils::AspectList::DisplayStyle::ListViewWithDetails);
    servers.setCreateItemFunction([] { return std::make_shared<Utils::AspectContainer>(); });
    int fromRegistry = 0;
    servers.addExtraButton("Add From Registry...", [&fromRegistry] { ++fromRegistry; });

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "AspectListDelegate"));

    // A list can offer more than Add and Remove - the MCP servers page adds
    // one that fills an item in from a registry - and a page ported to QML
    // would lose them silently.
    QQuickItem *extra = nullptr;
    QTRY_VERIFY(extra = findButton(delegate, "Add From Registry..."));
    QMetaObject::invokeMethod(extra, "clicked");
    QCOMPARE(fromRegistry, 1);
}

void QuickUiTest::testQmlOnlyContainerStillLaysOutInAWidgetLayout()
{
    // An AspectList item is a container, and the details pane of the widget
    // editor calls its layouter. A container given QML instead of a layouter
    // has none, and calling an empty std::function aborts the process.
    Utils::AspectContainer page;
    page.setAutoApply(false);
    Utils::AspectList servers(&page);
    servers.setDisplayStyle(Utils::AspectList::DisplayStyle::ListViewWithDetails);
    servers.setCreateItemFunction([] {
        auto item = std::make_shared<Utils::AspectContainer>();
        item->setQmlSource(QUrl("qrc:/nothing/AtAll.qml"));
        auto flag = new Utils::BoolAspect(item.get());
        flag->setLabelText("Enabled");
        return item;
    });

    Layouting::Column column{&servers};
    const std::unique_ptr<QWidget> widget(column.emerge());
    QVERIFY(widget);
    auto view = widget->findChild<QAbstractItemView *>();
    QVERIFY(view);

    // Adding selects the new item, which is what makes the details pane lay it
    // out. Its aspects have to appear even though it named no layouter.
    QAbstractButton *add = nullptr;
    for (QAbstractButton *button : widget->findChildren<QAbstractButton *>()) {
        if (button->text() == "Add")
            add = button;
    }
    QVERIFY(add);
    add->click();
    QCOMPARE(servers.volatileItems().size(), 1);
    const QList<QCheckBox *> boxes = widget->findChildren<QCheckBox *>();
    QCOMPARE(boxes.size(), 1);
    QCOMPARE(boxes.first()->text(), QString("Enabled"));
}

void QuickUiTest::testAspectVisibilityReachesTheDrawnControl()
{
    // Hiding an aspect is how a container keeps something out of its form
    // without a layouter choosing what to list: an internal id that is never
    // edited, or a field that only applies to one connection type. The MCP
    // servers page needs both.
    Utils::AspectContainer page;
    Utils::BoolAspect shown(&page);
    shown.setLabelText("Shown");
    Utils::BoolAspect hidden(&page);
    hidden.setLabelText("Hidden");
    hidden.setVisible(false);

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QQuickItem *rootItem = quickWidget->rootObject();
    QVERIFY(rootItem);

    QList<QQuickItem *> delegates;
    QTRY_COMPARE((delegates = findQmlComponents(rootItem, "BoolDelegate")).size(), 2);
    QCOMPARE(delegates.at(0)->property("visible").toBool(), true);
    QCOMPARE(delegates.at(1)->property("visible").toBool(), false);

    // And it follows a change, which is what the connection-type switch does.
    shown.setVisible(false);
    hidden.setVisible(true);
    QTRY_COMPARE(delegates.at(0)->property("visible").toBool(), false);
    QCOMPARE(delegates.at(1)->property("visible").toBool(), true);
}

// A name a page QML can write after "aspects.". Anything else leaves the aspect
// unreachable, and a nonexistent name is undefined in QML rather than an error.
static bool isUsableQmlName(const QString &name)
{
    static const QRegularExpression identifier("^[A-Za-z_][A-Za-z0-9_]*$");
    return identifier.match(name).hasMatch();
}

static void collectQmlNameProblems(
    const Utils::AspectContainer *container, const QString &page, QStringList *problems)
{
    QHash<QString, Utils::BaseAspect *> seen;
    for (Utils::BaseAspect *aspect : container->aspects()) {
        const QString name = aspect->qmlName();
        if (name.isEmpty())
            continue;
        if (!isUsableQmlName(name)) {
            *problems << QString("%1: \"%2\" is not a name QML can write")
                             .arg(page, name);
        } else if (Utils::BaseAspect *other = seen.value(name)) {
            *problems << QString("%1: \"%2\" names both %3 and %4")
                             .arg(page,
                                  name,
                                  Utils::stringFromKey(other->settingsKey()),
                                  Utils::stringFromKey(aspect->settingsKey()));
        } else {
            seen.insert(name, aspect);
        }
        if (auto nested = qobject_cast<Utils::AspectContainer *>(aspect))
            collectQmlNameProblems(nested, page, problems);
    }
}

void QuickUiTest::testAspectQmlNamesAreUsableAndUnique()
{
    // A page QML reaches its aspects by name, and the names are derived from
    // settings keys. Two keys ending in the same component - Memcheck.Arguments
    // and Callgrind.Arguments - derive the same name, which leaves one aspect
    // unreachable with nothing to see in the rendered page. So does a key with
    // a space in it. Both need setQmlName(), and both are invisible until
    // someone writes the QML, so check every page whether it is ported or not.
    QStringList problems;
    for (Core::IOptionsPage *page : Core::IOptionsPage::allOptionsPages()) {
        const std::optional<Utils::AspectContainer *> aspects = page->aspects();
        if (!aspects || !*aspects)
            continue;
        collectQmlNameProblems(*aspects, page->displayName(), &problems);
    }
    if (!problems.isEmpty())
        QFAIL(qPrintable("\n  " + problems.join("\n  ")));
}

void QuickUiTest::testActionAspectIsAButtonOnEitherRenderer()
{
    // A page action used to be a PushButton a Layouting closure built, which
    // no other renderer could see. As an aspect it is reachable by name from a
    // page's QML and drawn by both renderers.
    int triggered = 0;
    Utils::AspectContainer page;
    Utils::ActionAspect reset(&page);
    reset.setActionText("Reset Version Control Cache");
    reset.setQmlName("ResetCache");
    reset.setAction([&triggered] { ++triggered; });

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *button = nullptr;
    QTRY_VERIFY(button = findButton(quickWidget->rootObject(), "Reset Version Control Cache"));
    QMetaObject::invokeMethod(button, "clicked");
    QCOMPARE(triggered, 1);

    // And the same aspect in a widget layout, which is what an unported page
    // containing one still gets.
    Layouting::Column column{&reset};
    const std::unique_ptr<QWidget> widget(column.emerge());
    QVERIFY(widget);
    QAbstractButton *pushButton = nullptr;
    for (QAbstractButton *candidate : widget->findChildren<QAbstractButton *>()) {
        if (candidate->text() == "Reset Version Control Cache")
            pushButton = candidate;
    }
    QVERIFY(pushButton);
    pushButton->click();
    QCOMPARE(triggered, 2);
}

void QuickUiTest::testTextDisplayShowsItsMessage()
{
    // A TextDisplay keeps its message where only a cast to TextDisplay could
    // read it, so the Quick delegate - which has a BaseAspect and nothing else
    // - drew an empty label. The Coco page's error message was invisible. The
    // message goes through displayText() now, like every other renderer-visible
    // string.
    Utils::AspectContainer page;
    Utils::TextDisplay message(&page);
    message.setText("Coco installation directory not found.");
    message.setIconType(Utils::InfoType::Error);

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QQuickItem *rootItem = quickWidget->rootObject();
    QVERIFY(rootItem);

    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(rootItem, "TextDisplayDelegate"));
    // The style has its own Label.qml, so the class name is the file's.
    const QList<QQuickItem *> labels = findQmlComponents(delegate, "Label");
    QString drawn;
    for (QQuickItem *label : labels) {
        if (label->property("visible").toBool())
            drawn += label->property("text").toString();
    }
    QCOMPARE(drawn, QString("Coco installation directory not found."));

    // A later message replaces it: setText has to say so.
    message.setText("Something else went wrong.");
    QTRY_COMPARE(delegate->property("displayText").toString(),
                 QString("Something else went wrong."));

    // A StringAspect drawn as a label shares the delegate, and shows its value
    // rather than its label text - with the display filter applied, as the
    // widget renderer does.
    Utils::AspectContainer other;
    Utils::StringAspect version(&other);
    version.setDisplayStyle(Utils::StringAspect::LabelDisplay);
    version.setLabelText("Qbs version:");
    version.setValue("2.6.1");
    version.setDisplayFilter([](const QString &v) { return "qbs " + v; });

    const std::unique_ptr<QWidget> labelForm(QtcQuick::createGenericAspectForm(&other));
    QVERIFY(labelForm);
    auto labelWidget = labelForm->findChild<QQuickWidget *>();
    QVERIFY(labelWidget);
    QVERIFY(labelWidget->rootObject());
    QQuickItem *labelDelegate = nullptr;
    QTRY_VERIFY(labelDelegate = findQmlComponent(labelWidget->rootObject(), "TextDisplayDelegate"));
    QCOMPARE(labelDelegate->property("labelText").toString(), QString("Qbs version:"));
    QCOMPARE(labelDelegate->property("displayText").toString(), QString("qbs 2.6.1"));

    // A message can carry a link, and only the aspect knows what one means -
    // an external URL for one page, a help topic for another. The delegate
    // reports it and the aspect decides.
    QSignalSpy links(&message, &Utils::TextDisplay::linkActivated);
    message.activateLink("http://download.qt.io/official_releases/jom/");
    QCOMPARE(links.size(), 1);
    QCOMPARE(links.first().first().toString(),
             QString("http://download.qt.io/official_releases/jom/"));
}

void QuickUiTest::testRadioStyledBoolIsARadioButton()
{
    // A bool aspect can ask to be drawn as a radio button, and the Quick side
    // used to fold that into the check box - so a page offering a choice of two
    // showed two check boxes. Which of a set is on is the aspects' own
    // business, they keep each other in step, so the buttons must not group
    // themselves or they would fight it.
    Utils::AspectContainer page;
    Utils::BoolAspect current(&page);
    current.setDisplayStyle(Utils::BoolAspect::DisplayStyle::RadionButton);
    current.setLabelText("Current directory");
    current.setValue(true);
    Utils::BoolAspect chosen(&page);
    chosen.setDisplayStyle(Utils::BoolAspect::DisplayStyle::RadionButton);
    chosen.setLabelText("Directory");

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QList<QQuickItem *> radios;
    QTRY_COMPARE((radios = findQmlComponents(quickWidget->rootObject(), "RadioDelegate")).size(),
                 2);
    QVERIFY(!findQmlComponent(quickWidget->rootObject(), "BoolDelegate"));
    QCOMPARE(radios.at(0)->property("checked").toBool(), true);
    QCOMPARE(radios.at(1)->property("checked").toBool(), false);
    QCOMPARE(radios.at(0)->property("autoExclusive").toBool(), false);

    // Checking the second one leaves the first to the aspects: nothing here
    // unchecks it behind their backs.
    radios.at(1)->setProperty("checked", true);
    QMetaObject::invokeMethod(radios.at(1), "toggled");
    QCOMPARE(chosen.volatileValue(), true);
    QCOMPARE(current.volatileValue(), true);
}

void QuickUiTest::testSpinBoxDrawsItsPrefixAndSuffix()
{
    // A spin box aspect can name a unit - the version control timeouts say
    // "s", Application Output says "characters" - and Qt Quick's SpinBox has
    // no prefix or suffix of its own, so the delegate has to put them beside
    // it. It used to drop them.
    Utils::AspectContainer page;
    Utils::IntegerAspect timeout(&page);
    timeout.setLabelText("Timeout:");
    timeout.setRange(0, 100);
    timeout.setValue(30);
    timeout.setSuffix("s");

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "IntegerDelegate"));
    QStringList drawn;
    for (QQuickItem *label : findQmlComponents(delegate, "Label")) {
        if (label->property("visible").toBool())
            drawn << label->property("text").toString();
    }
    QCOMPARE(drawn, QStringList({"Timeout:", "s"}));
}

void QuickUiTest::testPageQmlReachesANestedContainersAspects()
{
    // Some pages are several settings objects side by side - Behavior is five -
    // and "aspects" names only the page container's own. AspectModels.named()
    // gives a page the same by-name access to a nested container, so it can lay
    // the sub-aspects out itself instead of settling for the generic form of
    // each.
    Utils::AspectContainer page;
    Utils::AspectContainer nested(&page);
    nested.setQmlName("Nested");
    Utils::BoolAspect flag(&nested);
    flag.setSettingsKey("Sub/TheFlag");
    flag.setLabelText("The flag");
    flag.setValue(true);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pageQml = dir.filePath("NestedPage.qml");
    {
        QFile file(pageQml);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"(
import QtQuick
import QtCreator.Ui

AspectPage {
    id: root
    readonly property var sub: AspectModels.named(aspects.Nested)
    BoolDelegate { aspect: root.sub.TheFlag }
}
)");
    }
    page.setQmlSource(QUrl::fromLocalFile(pageQml));

    const std::unique_ptr<QWidget> form(QtcQuick::createAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QQuickItem *rootItem = quickWidget->rootObject();
    if (!rootItem) {
        const QStringList errors = Utils::transform(quickWidget->errors(), &QQmlError::toString);
        QFAIL(qPrintable(errors.join("; ")));
    }

    QQuickItem *delegate = findQmlComponent(rootItem, "BoolDelegate");
    QVERIFY(delegate);
    QVERIFY(!delegate->property("aspect").isNull());
    QCOMPARE(delegate->property("text").toString(), QString("The flag"));
    QCOMPARE(delegate->property("checked").toBool(), true);

    // One object per container, so a page and the generic form agree on it.
    QCOMPARE(nested.findChildren<QtcQuick::NamedAspects *>().size(), 1);
}

void QuickUiTest::testMultiLineStringGetsATextArea()
{
    // A string edited over several lines - GDB's extra dumper commands, the C++
    // code model's ignore pattern - shared the single-line delegate, which
    // showed one line of it and no way to reach the rest. Both of those pages
    // had already been ported when this was noticed.
    Utils::AspectContainer page;
    Utils::StringAspect commands(&page);
    commands.setDisplayStyle(Utils::StringAspect::TextEditDisplay);
    commands.setLabelText("Commands:");
    commands.setValue("first\nsecond");
    Utils::StringAspect name(&page);
    name.setDisplayStyle(Utils::StringAspect::LineEditDisplay);
    name.setLabelText("Name:");

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QQuickItem *rootItem = quickWidget->rootObject();
    QVERIFY(rootItem);

    // One of each: a line edit is still a line edit.
    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(rootItem, "TextAreaDelegate"));
    QCOMPARE(findQmlComponents(rootItem, "StringDelegate").size(), 1);

    // Looked for inside the ScrollView: searching the delegate for "TextArea"
    // would match TextAreaDelegate itself.
    QQuickItem *scroll = findQmlComponent(delegate, "ScrollView");
    QVERIFY(scroll);
    QQuickItem *area = findQmlComponent(scroll, "TextArea");
    QVERIFY(area);
    QCOMPARE(area->property("text").toString(), QString("first\nsecond"));

    // Written back when the editor loses the focus, not per keystroke: one undo
    // step per character would be unusable.
    QMetaObject::invokeMethod(area, "forceActiveFocus");
    QTRY_VERIFY(area->hasActiveFocus());
    area->setProperty("text", "first\nsecond\nthird");
    QCOMPARE(commands.volatileValue(), QString("first\nsecond"));

    // Tabbing away is what commits it.
    QQuickItem *other = findQmlComponent(rootItem, "TextField");
    QVERIFY(other);
    QMetaObject::invokeMethod(other, "forceActiveFocus");
    QTRY_COMPARE(commands.volatileValue(), QString("first\nsecond\nthird"));
}

void QuickUiTest::testSecretIsNotOfferedAsAPlainPasswordField()
{
    // A SecretAspect's value only arrives through requestValue(), so it has no
    // variantValue() for a renderer to read or write: a generic password field
    // would show nothing and trip BaseAspect's check on the first edit. It says
    // Custom, and the form draws a placeholder rather than a broken field.
    Utils::AspectContainer page;
    Core::SecretAspect secret(&page);
    secret.setSettingsKey("Test.Secret");
    secret.setLabelText("Password:");

    QCOMPARE(int(QtcQuick::AspectContainerModel::kindOf(&secret)),
             int(QtcQuick::AspectContainerModel::Unsupported));

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QQuickItem *rootItem = quickWidget->rootObject();
    QVERIFY(rootItem);
    QTRY_VERIFY(findQmlComponent(rootItem, "UnsupportedDelegate"));
    QVERIFY(!findQmlComponent(rootItem, "StringDelegate"));
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

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
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

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
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

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
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

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
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
    page.setAutoApply(false);
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

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
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

    // Removing an item that was never applied takes its row with it.
    QQuickItem *remove = findButton(delegate, "Remove");
    QVERIFY(remove);
    QMetaObject::invokeMethod(remove, "clicked");
    QCOMPARE(servers.volatileItems().size(), 0);
    QTRY_COMPARE(view->property("count").toInt(), 0);
    QVERIFY(!findQmlComponent(delegate, "BoolDelegate"));

    // Removing one that was applied keeps the row, struck through, until the
    // removal is applied in turn.
    QMetaObject::invokeMethod(add, "clicked");
    servers.apply();
    QTRY_COMPARE(view->property("count").toInt(), 1);
    view->setProperty("currentIndex", 0);
    QMetaObject::invokeMethod(remove, "clicked");
    QCOMPARE(servers.volatileItems().size(), 0);
    QCOMPARE(view->property("count").toInt(), 1);
    QQuickItem *row = findQmlComponent(view, "ItemDelegate");
    QVERIFY(row);
    QVERIFY(row->property("removed").toBool());
    // On the text that is actually drawn: the style used to hard-code the font
    // on its content item, so the row's own font never reached the screen.
    auto rowText = row->property("contentItem").value<QQuickItem *>();
    QVERIFY(rowText);
    QVERIFY(rowText->property("font").value<QFont>().strikeOut());

    // And there is nothing left to remove on a row already on its way out.
    QVERIFY(!remove->property("enabled").toBool());

    // Applying the removal takes the row away. AspectList::apply() drops the
    // aspect's last reference before it says changed(), so a model holding raw
    // pointers would be left with a dangling row to crash on.
    servers.apply();
    QCOMPARE(servers.items().size(), 0);
    QTRY_COMPARE(view->property("count").toInt(), 0);
    QVERIFY(!findQmlComponent(view, "ItemDelegate"));

    // Still usable afterwards: Add works on the emptied list.
    QMetaObject::invokeMethod(add, "clicked");
    QCOMPARE(servers.volatileItems().size(), 1);
    QTRY_COMPARE(view->property("count").toInt(), 1);
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

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
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

void QuickUiTest::testAspectListLabelArrivingLate()
{
    Utils::AspectContainer page;
    Utils::AspectList servers(&page);
    servers.setDisplayStyle(Utils::AspectList::DisplayStyle::ListViewWithDetails);

    // The MCP server list names its rows asynchronously, so the callback hands
    // back a future rather than a string.
    QPromise<QVariant> promise;
    const QFuture<QVariant> pending = promise.future();
    promise.start();
    servers.listViewDataCallback = [pending](Utils::BaseAspect *, int) -> QVariant {
        return QVariant::fromValue(pending);
    };
    servers.setCreateItemFunction([] { return std::make_shared<Utils::AspectContainer>(); });
    servers.createAndAddItem();

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    // A list that is not on screen builds no rows until something asks for one.
    QQuickItem *view = findQmlComponent(quickWidget->rootObject(), "QQuickListView");
    QVERIFY(view);
    QCOMPARE(view->property("count").toInt(), 1);
    view->setProperty("currentIndex", 0);

    QQuickItem *row = nullptr;
    QTRY_VERIFY(row = findQmlComponent(view, "ItemDelegate"));
    QCOMPARE(row->property("text").toString(), QString());

    promise.addResult(QVariant("Named later"));
    promise.finish();
    QTRY_COMPARE(row->property("text").toString(), QString("Named later"));
}

QObject *createQuickUiTest()
{
    return new QuickUiTest;
}

} // namespace QuickUi::Internal

#include "quickui_test.moc"
