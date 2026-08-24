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
#include <QItemSelectionModel>
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

#include <functional>
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

// A form on screen. A TableView only creates its cells once it has laid out,
// which needs a window: in a form that is never shown the cells exist for a
// moment and then come away from the model, so anything read off them is
// whatever the abandoned layout pass left behind.
static QWidget *showForm(Utils::AspectContainer *page)
{
    QWidget *form = QtcQuick::createGenericAspectForm(page);
    if (!form)
        return nullptr;
    // Wide enough that the last column is stretched rather than sized to its
    // contents, which is where the header stopped showing its text.
    form->resize(1400, 400);
    form->show();
    if (!QTest::qWaitForWindowExposed(form)) {
        delete form;
        return nullptr;
    }
    return form;
}

// Every item in the visual tree with this objectName. Cell controls are found
// this way rather than by type: a non-editable ComboBox's content item is
// itself a TextField, so counting types counts a style's internals too.
static QList<QQuickItem *> findQmlNamed(QQuickItem *root, const QString &objectName)
{
    QList<QQuickItem *> found;
    if (root->objectName() == objectName)
        found << root;
    const QList<QQuickItem *> children = root->childItems();
    for (QQuickItem *child : children)
        found << findQmlNamed(child, objectName);
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
    void testTextDisplaySaysHowToReadItsMessage();
    void testRadioStyledBoolIsARadioButton();
    void testSpinBoxDrawsItsPrefixAndSuffix();
    void testPageQmlReachesANestedContainersAspects();
    void testMultiLineStringGetsATextArea();
    void testSecretIsFetchedBeforeItCanBeEdited();
    void testTableAspectDrawsWhatItsModelOffers();
    void testTableAspectAddsAndRemovesRows();
    void testTableAspectFiltersItsRows();
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

            // A hand-written page lists what it shows, so it can leave a
            // setting out and nothing says so - the mirror image of the generic
            // form showing everything. Every visible aspect the container holds
            // has to appear somewhere on the page.
            QSet<const Utils::BaseAspect *> drawn;
            for (QQuickItem *delegate : delegates) {
                if (auto aspect = delegate->property("aspect").value<Utils::BaseAspect *>())
                    drawn.insert(aspect);
            }
            // A group's check box is drawn by AspectGroupBox, not by a delegate.
            for (QQuickItem *group : findQmlComponents(quickWidget->rootObject(),
                                                       "AspectGroupBox")) {
                if (auto aspect = group->property("checkAspect").value<Utils::BaseAspect *>())
                    drawn.insert(aspect);
            }
            QStringList missing;
            std::function<void(const Utils::AspectContainer *)> walk =
                [&](const Utils::AspectContainer *container) {
                    for (Utils::BaseAspect *aspect : container->aspects()) {
                        if (!aspect->isVisible())
                            continue;
                        if (auto nested = qobject_cast<Utils::AspectContainer *>(aspect)) {
                            // Unless a delegate drew the container itself - a
                            // table's rows are its aspects, and one delegate
                            // draws all of them.
                            if (!drawn.contains(aspect))
                                walk(nested);
                            continue;
                        }
                        const auto kind = QtcQuick::AspectContainerModel::kindOf(aspect);
                        if (kind == QtcQuick::AspectContainerModel::Invisible
                            || kind == QtcQuick::AspectContainerModel::Unsupported)
                            continue;
                        // An aspect with no label was never meant for a form:
                        // it is stored settings, or it is driven from some other
                        // part of the UI. The closures did not draw these
                        // either.
                        if (aspect->labelText().isEmpty())
                            continue;
                        if (!drawn.contains(aspect)) {
                            missing << Utils::stringFromKey(aspect->settingsKey())
                                       + "/" + aspect->qmlName();
                        }
                    }
                };
            walk(*aspects);
            // Reported rather than asserted: a page legitimately leaves out
            // settings that are stored but edited elsewhere - GDB's throw and
            // catch breakpoints live in the Breakpoints view, Valgrind's cycle
            // detection in the Callgrind toolbar. Each line below was checked
            // against the closure the page replaced. A new line is worth
            // checking the same way.
            if (!missing.isEmpty()) {
                qInfo().noquote() << page->displayName() << "does not draw:"
                                  << missing.join(", ");
            }
            QVERIFY2(!delegates.isEmpty(),
                     qPrintable(page->displayName() + " renders no aspect at all"));
            for (QQuickItem *delegate : delegates) {
                QVERIFY2(!delegate->property("aspect").isNull(),
                         qPrintable(page->displayName() + ": "
                                    + QString::fromLatin1(delegate->metaObject()->className())
                                    + " has no aspect"));

                // A delegate that draws a button takes its label from the
                // aspect's descriptor, so an aspect that has not said what its
                // action is called gets a nameless button. Naming the delegate
                // in a page's QML does not make the aspect describe itself.
                const QString drawn = QString::fromLatin1(delegate->metaObject()->className());

                // A page naming the wrong delegate for an aspect is not an
                // error anywhere: a check box bound to a string aspect draws
                // unchecked and writes true into it. So the delegate a page
                // named has to be the one the aspect asked for.
                // Which kinds each delegate serves, mirroring the choices in
                // AspectItems.qml - StringDelegate draws a path as well as a
                // string, for one.
                using Kind = QtcQuick::AspectContainerModel;
                static const QHash<QString, QList<int>> serves{
                    {"BoolDelegate", {Kind::Bool}},
                    {"RadioDelegate", {Kind::Radio}},
                    {"StringDelegate", {Kind::String, Kind::FilePath}},
                    {"TextAreaDelegate", {Kind::Text}},
                    {"SecretDelegate", {Kind::Secret}},
                    {"IntegerDelegate", {Kind::Integer}},
                    {"DoubleDelegate", {Kind::Double}},
                    {"SelectionDelegate", {Kind::Selection}},
                    {"ColorDelegate", {Kind::Color}},
                    {"FontFamilyDelegate", {Kind::FontFamily}},
                    {"TextDisplayDelegate", {Kind::TextDisplay}},
                    {"TextWithActionDelegate", {Kind::TextWithAction}},
                    {"ButtonDelegate", {Kind::Button}},
                    {"StringListDelegate", {Kind::StringList}},
                    {"StringListEditorDelegate", {Kind::StringListEditor}},
                    {"AspectListDelegate", {Kind::AspectList}},
                    {"MultiSelectionDelegate", {Kind::MultiSelection}},
                    {"FilePathListDelegate", {Kind::FilePathList}},
                    {"TableDelegate", {Kind::Table}},
                };
                for (auto it = serves.cbegin(); it != serves.cend(); ++it) {
                    if (!drawn.startsWith(it.key()))
                        continue;
                    auto aspect = delegate->property("aspect").value<Utils::BaseAspect *>();
                    QVERIFY(aspect);
                    const int kind = int(QtcQuick::AspectContainerModel::kindOf(aspect));
                    QVERIFY2(it.value().contains(kind),
                             qPrintable(page->displayName() + ": " + drawn + " drew "
                                        + Utils::stringFromKey(aspect->settingsKey())
                                        + ", which asked for kind " + QString::number(kind)));
                }

                // A check box carries its own text, so an aspect that never
                // said what it is called is drawn as a box with nothing beside
                // it. That is what a page of nameless check boxes looks like,
                // and it is invisible to every other assertion here.
                // An aspect that is not shown has nothing beside it because it
                // is not there - a page names its aspects on every platform,
                // and some of them only exist on one.
                if ((drawn.startsWith("BoolDelegate") || drawn.startsWith("RadioDelegate"))
                    && delegate->property("aspectVisible").toBool()) {
                    QVERIFY2(!delegate->property("text").toString().isEmpty(),
                             qPrintable(page->displayName() + ": " + drawn
                                        + " has no text beside it"));
                }

                if (drawn.startsWith("TextWithActionDelegate")
                    || drawn.startsWith("ButtonDelegate")) {
                    const QVariantMap pres = delegate->property("pres").toMap();
                    QVERIFY2(!pres.value("actionText").toString().isEmpty(),
                             qPrintable(page->displayName() + ": " + drawn
                                        + "'s button has no label"));
                }
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
    QVERIFY(button);
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

    // An action's label can be its state - Copilot's button says "Sign In",
    // then "Signing in ...", then "Sign out <user>" - so changing it has to
    // reach what was drawn. The descriptor is read once when the delegate is
    // built, so the aspect has to say it changed.
    reset.setActionText("Signing out...");
    QTRY_COMPARE(button->property("text").toString(), QString("Signing out..."));

    // An action whose label reports state is told when it is first drawn, so
    // that finding the state out costs nothing until someone looks. Copilot
    // starts a language server there.
    int shown = 0;
    Utils::AspectContainer lazyPage;
    Utils::ActionAspect lazy(&lazyPage);
    lazy.setActionText("Sign In");
    lazy.setQmlName("Lazy");
    lazy.setAction([] {});
    lazy.setOnShown([&shown, &lazy] {
        ++shown;
        lazy.setActionText("Checking status...");
    });
    QCOMPARE(shown, 0);

    const std::unique_ptr<QWidget> lazyForm(QtcQuick::createGenericAspectForm(&lazyPage));
    QVERIFY(lazyForm);
    auto lazyWidget = lazyForm->findChild<QQuickWidget *>();
    QVERIFY(lazyWidget);
    QVERIFY(lazyWidget->rootObject());
    QTRY_COMPARE(shown, 1);
    QQuickItem *lazyButton = findButton(lazyWidget->rootObject(), "Checking status...");
    QVERIFY(lazyButton);
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

void QuickUiTest::testTextDisplaySaysHowToReadItsMessage()
{
    // Markdown is never detected from the text, so a message written in it read
    // out as its own source: the MCP server's address showed the brackets and
    // parentheses of the link rather than a link. The layouter used to set the
    // format on the label it built, which is exactly the kind of thing that
    // goes missing when the layouter does.
    Utils::AspectContainer page;
    Utils::TextDisplay message(&page);
    message.setText("Listening on [127.0.0.1:1234](127.0.0.1:1234).");
    message.setTextFormat(Utils::AspectControls::TextFormat::MarkdownText);

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "TextDisplayDelegate"));
    QQuickItem *drawn = nullptr;
    for (QQuickItem *label : findQmlComponents(delegate, "Label")) {
        if (label->property("text").toString().startsWith("Listening"))
            drawn = label;
    }
    QVERIFY(drawn);
    QCOMPARE(drawn->property("textFormat").toInt(), int(Qt::MarkdownText));

    // The default is left to detection, which handles the rich text an aspect
    // writes as HTML.
    Utils::TextDisplay plain(&page);
    plain.setText("Nothing special.");
    QCOMPARE(plain.presentation().textFormat, Utils::AspectControls::TextFormat::AutoText);
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

void QuickUiTest::testSecretIsFetchedBeforeItCanBeEdited()
{
    // A secret is not kept in the aspect: it has to be fetched, and it arrives
    // later. So the field is read-only until it does - typing before then would
    // overwrite what is stored with nothing - and it is read through
    // displayText() rather than the value property, which a secret has none of.
    Utils::AspectContainer page;
    Core::SecretAspect secret(&page);
    secret.setSettingsKey("Test.Secret");
    secret.setLabelText("Password:");

    QCOMPARE(int(QtcQuick::AspectContainerModel::kindOf(&secret)),
             int(QtcQuick::AspectContainerModel::Secret));

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QQuickItem *rootItem = quickWidget->rootObject();
    QVERIFY(rootItem);

    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(rootItem, "SecretDelegate"));
    QQuickItem *field = findQmlComponent(delegate, "TextField");
    QVERIFY(field);

    // The delegate asks on completion. Whether the keychain answers at all
    // depends on the machine, so wait for the answer either way.
    QTRY_VERIFY(delegate->property("arrived").toBool());
    QVERIFY(!field->property("readOnly").toBool());

    // Not echoed until asked for.
    const QMetaObject *mo = field->metaObject();
    const QMetaEnum echoModes = mo->property(mo->indexOfProperty("echoMode")).enumerator();
    QCOMPARE(field->property("echoMode").toInt(), echoModes.keyToValue("Password"));

    // Written through the value property, which is all a delegate has.
    field->setProperty("text", "hunter2");
    QMetaObject::invokeMethod(field, "editingFinished");
    QCOMPARE(secret.displayText(), QString("hunter2"));
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

// A table of the shape AspectTable describes, to check that a view builds the
// cell each column asks for: a check box, a choice, a field with a pattern, and
// text that cannot be written to.
class TestTableModel : public QAbstractTableModel
{
public:
    struct Row
    {
        bool enabled = true;
        QString colour;
        QString word;
    };
    QList<Row> rows{{true, "red", "one"}, {false, "blue", "two"}};
    // Writing the same value back is invisible in rows, so count the writes.
    int writes = 0;

    enum Column { ColumnEnabled, ColumnColour, ColumnWord, ColumnLocked, ColumnCount };

    int rowCount(const QModelIndex &parent) const override
    {
        return parent.isValid() ? 0 : rows.size();
    }
    int columnCount(const QModelIndex &parent) const override
    {
        return parent.isValid() ? 0 : ColumnCount;
    }

    static QVariantList colours()
    {
        return {QVariantMap{{"display", "red"}, {"id", "red"}},
                QVariantMap{{"display", "blue"}, {"id", "blue"}}};
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        const Row &row = rows.at(index.row());
        switch (role) {
        case Qt::DisplayRole:
        case Qt::EditRole:
            switch (index.column()) {
            case ColumnColour: return row.colour;
            case ColumnWord:   return row.word;
            case ColumnLocked:
                // Long on purpose: a column sized to this would be wider than
                // the table, which used to leave its header unnamed.
                return QString("locked, and long enough that a column sized to it "
                               "is wider than the whole table - which used to "
                               "scroll the centred header label out of the "
                               "clipped header and leave the column looking "
                               "unnamed, even though the model named it");
            default:           return {};
            }
        case Qt::CheckStateRole:
            if (index.column() != ColumnEnabled)
                return {};
            return row.enabled ? Qt::Checked : Qt::Unchecked;
        case Utils::AspectTable::ChoicesRole:
            return index.column() == ColumnColour ? colours() : QVariantList();
        case Utils::AspectTable::ValidatorRole:
            return index.column() == ColumnWord ? QString("[a-z]*") : QString();
        case Utils::AspectTable::EditableRole:
            return Utils::AspectTable::isWritable(flags(index));
        case Utils::AspectTable::CheckableRole:
            return flags(index).testFlag(Qt::ItemIsUserCheckable);
        default:
            return {};
        }
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override
    {
        if (role == Qt::CheckStateRole && index.column() == ColumnEnabled) {
            rows[index.row()].enabled = value.toInt() == Qt::Checked;
        } else if (role == Qt::EditRole && index.column() == ColumnColour) {
            rows[index.row()].colour = value.toString();
        } else if (role == Qt::EditRole && index.column() == ColumnWord) {
            rows[index.row()].word = value.toString();
        } else {
            return false;
        }
        ++writes;
        emit dataChanged(index, index);
        return true;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation == Qt::Vertical || role != Qt::DisplayRole)
            return {};
        switch (section) {
        case ColumnEnabled: return QString("On");
        case ColumnColour:  return QString("Colour");
        case ColumnWord:    return QString("Word");
        default:            return QString("Locked");
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return Utils::AspectTable::withRoleNames(QAbstractTableModel::roleNames());
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        const Qt::ItemFlags flags = QAbstractTableModel::flags(index);
        switch (index.column()) {
        case ColumnEnabled: return flags | Qt::ItemIsUserCheckable;
        case ColumnColour:
        case ColumnWord:    return flags | Qt::ItemIsEditable;
        default:            return flags;
        }
    }

    bool insertRows(int row, int count, const QModelIndex &parent) override
    {
        beginInsertRows(parent, row, row + count - 1);
        for (int i = 0; i < count; ++i)
            rows.insert(row, {true, "red", "new"});
        endInsertRows();
        return true;
    }

    bool removeRows(int row, int count, const QModelIndex &parent) override
    {
        beginRemoveRows(parent, row, row + count - 1);
        rows.remove(row, count);
        endRemoveRows();
        return true;
    }

    QStringList words() const
    {
        return Utils::transform(rows, [](const Row &row) {
            return QString("%1/%2/%3").arg(row.enabled ? "on" : "off", row.colour, row.word);
        });
    }
};

class TestTableAspect : public Utils::StringListAspect
{
public:
    using StringListAspect::StringListAspect;

    QAbstractItemModel *tableModel() override { return &m_model; }

    Utils::AspectPresentation presentation() const override
    {
        Utils::AspectPresentation p = StringListAspect::presentation();
        p.control = Utils::AspectControls::Table;
        p.filterPlaceholderText = m_filterPlaceholderText;
        return p;
    }

    TestTableModel m_model;
    QString m_filterPlaceholderText;
};

// The TableView the delegate builds, and the model it actually shows - the
// filter proxy, not the aspect's own.
static QQuickItem *tableViewOf(QQuickItem *root)
{
    QQuickItem *delegate = findQmlComponent(root, "TableDelegate");
    return delegate ? findQmlComponent(delegate, "QQuickTableView") : nullptr;
}

void QuickUiTest::testTableAspectDrawsWhatItsModelOffers()
{
    Utils::AspectContainer page;
    TestTableAspect table(&page);
    table.setLabelText("Rows");

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();

    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "TableDelegate"));
    QQuickItem *view = nullptr;
    QTRY_VERIFY(view = tableViewOf(quickWidget->rootObject()));
    QTRY_COMPARE(view->property("rows").toInt(), 2);
    QCOMPARE(view->property("columns").toInt(), TestTableModel::ColumnCount);

    // Every column says what it is. The header reads that off the model, and
    // the last column is the one that gets forgotten - it is the one a
    // stretched width is computed for.
    QQuickItem *header = nullptr;
    QTRY_VERIFY(header = findQmlComponent(delegate, "HorizontalHeaderView"));
    QList<QQuickItem *> headings;
    QTRY_COMPARE((headings = findQmlComponents(header, "HorizontalHeaderViewDelegate")).size(),
                 TestTableModel::ColumnCount);
    QStringList headingTexts;
    for (QQuickItem *heading : headings) {
        // The heading's text is in its content item, not on the delegate.
        const QList<QQuickItem *> labels = findQmlComponents(heading, "Label");
        headingTexts << (labels.isEmpty() ? QString("<none>")
                                         : labels.first()->property("text").toString());
    }
    QCOMPARE(headingTexts, QStringList({"On", "Colour", "Word", "Locked"}));

    // A checkable cell is a check box showing the cell's state.
    QList<QQuickItem *> checks;
    QTRY_COMPARE((checks = findQmlNamed(view, "tableCellCheckBox")).size(), 2);
    QCOMPARE(checks.at(0)->property("checked").toBool(), true);
    QCOMPARE(checks.at(1)->property("checked").toBool(), false);
    // A check box the user cannot reach is not a writable cell. Ticking one
    // from a test works whether or not it is enabled, so say so here: this is
    // what tells a checkable cell apart from a closed one.
    QVERIFY(checks.at(0)->property("enabled").toBool());

    // A cell that offers a choice gets a combo box showing the cell's value.
    QList<QQuickItem *> combos;
    QTRY_COMPARE((combos = findQmlNamed(view, "tableCellComboBox")).size(), 2);
    QCOMPARE(combos.at(0)->property("count").toInt(), 2);
    QCOMPARE(combos.at(0)->property("currentText").toString(), QString("red"));

    // A writable cell with no choices is a field; one the model closed is text
    // to read, not a field that refuses to be typed in.
    QList<QQuickItem *> fields;
    QTRY_COMPARE((fields = findQmlNamed(view, "tableCellField")).size(), 2);
    QCOMPARE(fields.at(0)->property("text").toString(), QString("one"));
    const QList<QQuickItem *> labels = findQmlNamed(view, "tableCellLabel");
    QCOMPARE(labels.size(), 2);
    QVERIFY(labels.at(0)->property("text").toString().startsWith("locked"));

    // Ticking a check box writes through Qt::CheckStateRole.
    checks.at(1)->setProperty("checked", true);
    QMetaObject::invokeMethod(checks.at(1), "toggled");
    QCOMPARE(table.m_model.rows.at(1).enabled, true);

    // Choosing writes the choice's id back through the model.
    QMetaObject::invokeMethod(combos.at(0), "activated", Q_ARG(int, 1));
    QCOMPARE(table.m_model.rows.at(0).colour, QString("blue"));

    // A field writes its text back, and only when it changed. A field that
    // lost focus untouched must not write at all: by then the row it was built
    // for may be gone, and the write would land on whichever row took its
    // place.
    // Re-found: the edits above changed the model, and a cell is not promised
    // to be the same item afterwards.
    QTRY_COMPARE((fields = findQmlNamed(view, "tableCellField")).size(), 2);
    QQuickItem *word = fields.at(0);
    QCOMPARE(word->property("text").toString(), QString("one"));

    const int writes = table.m_model.writes;
    QMetaObject::invokeMethod(word, "editingFinished");
    QCOMPARE(table.m_model.writes, writes);

    word->setProperty("text", "three");
    QMetaObject::invokeMethod(word, "editingFinished");
    QCOMPARE(table.m_model.writes, writes + 1);
    QCOMPARE(table.m_model.rows.at(0).word, QString("three"));
}

void QuickUiTest::testTableAspectAddsAndRemovesRows()
{
    Utils::AspectContainer page;
    TestTableAspect table(&page);
    table.setLabelText("Rows");

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();

    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "TableDelegate"));
    QQuickItem *view = nullptr;
    QTRY_VERIFY(view = tableViewOf(quickWidget->rootObject()));

    QQuickItem *add = findButton(delegate, "Add");
    QVERIFY(add);
    QMetaObject::invokeMethod(add, "clicked");
    QCOMPARE(table.m_model.words(),
             QStringList({"on/red/one", "off/blue/two", "on/red/new"}));

    // Nothing is selected, so there is nothing to remove yet.
    QQuickItem *remove = findButton(delegate, "Remove");
    QVERIFY(remove);
    QVERIFY(!remove->property("enabled").toBool());

    // The view shows the filter proxy, so a row is selected through that and
    // not through the aspect's own model.
    auto shown = view->property("model").value<QAbstractItemModel *>();
    QVERIFY(shown);
    auto selection = view->property("selectionModel").value<QItemSelectionModel *>();
    QVERIFY(selection);
    selection->setCurrentIndex(shown->index(1, 0), QItemSelectionModel::SelectCurrent);
    QTRY_VERIFY(remove->property("enabled").toBool());

    QMetaObject::invokeMethod(remove, "clicked");
    QCOMPARE(table.m_model.words(), QStringList({"on/red/one", "on/red/new"}));
}

void QuickUiTest::testTableAspectFiltersItsRows()
{
    Utils::AspectContainer page;
    TestTableAspect table(&page);
    table.setLabelText("Rows");
    table.m_filterPlaceholderText = "Filter...";

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();

    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "TableDelegate"));
    QQuickItem *view = nullptr;
    QTRY_VERIFY(view = tableViewOf(quickWidget->rootObject()));
    QTRY_COMPARE(view->property("rows").toInt(), 2);

    const QList<QQuickItem *> filters = findQmlNamed(delegate, "tableFilterField");
    QCOMPARE(filters.size(), 1);
    QQuickItem *filter = filters.first();
    QVERIFY(filter->isVisible());
    QCOMPARE(filter->property("placeholderText").toString(), QString("Filter..."));

    // Matching happens on every column, so the word narrows the rows even
    // though the filter knows nothing about which column holds it.
    // What the view is given, rather than TableView's own row count: nothing
    // paints in a form that was never shown, so the view lays out once and its
    // count does not follow the model afterwards.
    auto shown = view->property("model").value<QAbstractItemModel *>();
    QVERIFY(shown);
    QCOMPARE(shown->rowCount({}), 2);

    filter->setProperty("text", "two");
    QTRY_COMPARE(shown->rowCount({}), 1);
    QCOMPARE(shown->index(0, TestTableModel::ColumnWord).data().toString(), QString("two"));

    filter->setProperty("text", "");
    QTRY_COMPARE(shown->rowCount({}), 2);

    // A page that named no placeholder shows no filter field at all.
    Utils::AspectContainer plainPage;
    TestTableAspect plain(&plainPage);
    plain.setLabelText("Rows");
    const std::unique_ptr<QWidget> plainForm(showForm(&plainPage));
    QVERIFY(plainForm);
    auto plainWidget = plainForm->findChild<QQuickWidget *>();
    QQuickItem *plainDelegate = nullptr;
    QTRY_VERIFY(plainDelegate = findQmlComponent(plainWidget->rootObject(), "TableDelegate"));
    QTRY_VERIFY(tableViewOf(plainWidget->rootObject()));
    const QList<QQuickItem *> none = findQmlNamed(plainDelegate, "tableFilterField");
    QCOMPARE(none.size(), 1);
    QVERIFY(!none.first()->isVisible());
}

QObject *createQuickUiTest()
{
    return new QuickUiTest;
}

} // namespace QuickUi::Internal

#include "quickui_test.moc"
