// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "quickui_test.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/secretaspect.h>

#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>

#include <qtcquick/aspectmodels.h>
#include <qtcquick/qtciconprovider.h>
#include <qtcquick/qtcquickengine.h>
#include <qtcquick/aspectcontainermodel.h>
#include <qtcquick/aspectform.h>
#include <qtcquick/namedaspects.h>

#include <utils/algorithm.h>
#include <utils/aspectlist.h>
#include <utils/aspects.h>
#include <utils/groupedlistaspect.h>
#include <utils/groupedmodel.h>
#include <utils/pathvalidation.h>
#include <utils/treemodel.h>
#include <utils/layoutbuilder.h>

#include <QAbstractButton>
#include <QAction>
#include <QAbstractItemView>
#include <QItemSelectionModel>
#include <QCheckBox>
#include <QMenu>
#include <QFile>
#include <QQmlError>
#include <QStandardItem>
#include <QFont>
#include <QIcon>
#include <QPixmap>
#include <QPromise>
#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QSignalSpy>
#include <QMetaEnum>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QMimeData>
#include <QScopeGuard>
#include <QQuickWidget>
#include <QLineEdit>
#include <QScrollArea>
#include <QVBoxLayout>
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
    void testAContainerCanReadAsOneRow();
    void testQmlNameIsDerivedFromTheSettingsKey();
    void testPageQmlReachesItsAspectsByName();
    void testLabelChangeReachesTheControl();
    void testLabelDropsItsAcceleratorForQuick();
    void testPathAspectOffersSomewhereToBrowseFrom();
    void testAChoiceCanBeThereWithoutBeingOffered();
    void testIdValuedSelectionRoundTrips();
    void testARefilledListKeepsWhatWasPicked();
    void testStringListEditorAddsRemovesAndEdits();
    void testStringSelectionOffersItsChoices();
    void testAspectListAddsRemovesAndShowsDetails();
    void testTextWithActionShowsSummaryAndActs();
    void testAspectListLabelArrivingLate();
    void testPageWithoutItsOwnQmlIsDeclined();
    void testPasswordAspectDoesNotEchoItsValue();
    void testAspectListOffersItsExtraButtons();
    void testAnOrderedListMovesTheCurrentItem();
    void testAnUnorderedListOffersNoMoveButtons();
    void testQmlOnlyContainerStillLaysOutInAWidgetLayout();
    void testAspectVisibilityReachesTheDrawnControl();
    void testAspectQmlNamesAreUsableAndUnique();
    void testActionAspectIsAButtonOnEitherRenderer();
    void testAButtonCanOfferAMenuInsteadOfActing();
    void testTextDisplayShowsItsMessage();
    void testTextDisplaySaysHowToReadItsMessage();
    void testRadioStyledBoolIsARadioButton();
    void testATriStateSettingCanSayNeither();
    void testSpinBoxDrawsItsPrefixAndSuffix();
    void testPageQmlReachesANestedContainersAspects();
    void testAnAspectCanHandOutAContainerToDraw();
    void testMultiLineStringGetsATextArea();
    void testSecretIsFetchedBeforeItCanBeEdited();
    void testTableAspectDrawsWhatItsModelOffers();
    void testATableWithNoColumnNamesHasNoHeader();
    void testTableAspectAddsAndRemovesRows();
    void testTableAspectRemovesEverySelectedRow();
    void testTableAspectFiltersItsRows();
    void testTableCellReadsInItsOwnColours();
    void testListRowShowsWhatTheListSaysAboutTheItem();
    void testGroupedListShowsItsGroupsAndActsOnTheCurrentItem();
    void testTreeShowsWhatTheAspectHandsOut();
    void testATreeCellIsWrittenToWhereItsModelSaysSo();
    void testAReorderableTreeMovesARowThroughItsModel();
    void testFieldSaysWhatIsWrongAndKeepsItOut();
    void testFieldWaitsForAnAnswerItHasToFetch();
    void testAFieldCompletesAgainstWhatTheAspectOffers();
    void testSeveralLinesCompleteTheWordTheCursorIsIn();
    void testColourOffersToGoBackToItsDefault();
    void testColourWithNoResetHasNoButton();
    void testACodeEditorScrollsInsteadOfGrowingThePage();
    void testTabTypesAnIndentInACodeEditor();
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
    QStringList unsupported;

    // A QML warning is not a failure anywhere: the engine reports it and
    // carries on, so a broken binding shows up as a page that looks nearly
    // right - a delegate that draws nothing, a property left at its default.
    // Collected here because this is where every page is built; a second test
    // that builds them again sees nothing, the warnings having already been
    // reported for the instances made below.
    //
    // Binding loops are not among what this catches: they need the Preferences
    // dialog's own layout negotiation to be reported at all, and a page built
    // here settles its width in one pass. See the note on AspectGroupBox in the
    // migration plan for how those are measured.
    QStringList qmlWarnings;
    const QMetaObject::Connection warningConnection = QObject::connect(
        QtcQuick::engine(), &QQmlEngine::warnings, QtcQuick::engine(),
        [&qmlWarnings](const QList<QQmlError> &errors) {
            for (const QQmlError &error : errors)
                qmlWarnings << error.toString();
        });
    const QScopeGuard disconnectWarnings([warningConnection] {
        QObject::disconnect(warningConnection);
    });

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
            declined << page->displayName() + " [" + page->id().toString() + "]";
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
                        if (kind == QtcQuick::AspectContainerModel::Invisible)
                            continue;
                        // An aspect no renderer knows is a hole in the page:
                        // it is not drawn and nothing else says so, because
                        // the walk below skips what it cannot name. That is
                        // how AspectList's inline style went unnoticed - it
                        // answered Custom, so every page using it drew nothing
                        // where the list should be. An aspect that means to
                        // show nothing says Invisible instead.
                        if (kind == QtcQuick::AspectContainerModel::Unsupported) {
                            unsupported << page->displayName() + ": "
                                               + QString::fromLatin1(
                                                   aspect->metaObject()->className())
                                               + " (" + aspect->qmlName() + ")";
                        }

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
                    {"RadioGroupDelegate", {Kind::RadioGroup}},
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
                    {"TriStateDelegate", {Kind::TriStateBool}},
                    {"KeySequenceDelegate", {Kind::KeySequence}},
                    {"AspectInlineListDelegate", {Kind::AspectInlineList}},
                    {"GroupedListDelegate", {Kind::GroupedList}},
                    {"InlineGroupDelegate", {Kind::InlineGroup}},
                    {"TreeDelegate", {Kind::Tree}},
                    {"FontFamilyDelegate", {Kind::FontFamily}},
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

                // A button takes its label from the aspect's descriptor, so an
                // aspect that has not said what its action is called gets a
                // nameless button. Naming the delegate in a page's QML does not
                // make the aspect describe itself.
                // A button may say what it does with a picture instead - the
                // kit icon - but then the tool tip is all there is to read.
                // As above, a delegate that is not shown is not a page of
                // nameless buttons: the aspect is not there on this platform,
                // or nothing is selected for it to act on.
                if ((drawn.startsWith("TextWithActionDelegate")
                     || drawn.startsWith("ButtonDelegate"))
                    && delegate->property("aspectVisible").toBool()) {
                    const QVariantMap pres = delegate->property("pres").toMap();
                    const bool named = !pres.value("actionText").toString().isEmpty();
                    const bool pictured = !pres.value("actionIcon").toString().isEmpty()
                                          && !delegate->property("toolTip").toString().isEmpty();
                    QVERIFY2(named || pictured,
                             qPrintable(page->displayName() + ": " + drawn
                                        + "'s button has neither a label nor an explained icon"));
                }
            }
        }
        ++renderedWithQuick;
    }

    QVERIFY2(qmlWarnings.isEmpty(), qPrintable("\n" + qmlWarnings.join("\n")));

    qInfo().noquote() << "aspect-driven pages:" << aspectDriven
                      << "rendered with Qt Quick:" << renderedWithQuick
                      << "\n  of the" << declined.size() << "still on widgets,"
                      << genericWouldDo << "have only aspects the generic form knows"
                      << "\n  still on widgets:" << declined.join(", ");

    QVERIFY(aspectDriven > 0);
    QCOMPARE(renderedWithQuick, withQml);

    // Nothing on a rendered page may be a control no renderer knows.
    QVERIFY2(unsupported.isEmpty(), qPrintable("no renderer draws these: "
                                               + unsupported.join(", ")));

    // Every aspect-driven page names a form, so a declined one is a page that
    // went back to widgets rather than one waiting its turn. Nothing else here
    // says so: a page without QML and no QQuickWidget agrees with itself.
    //
    // The one page that may be declined is C++'s, and only where ClangFormat
    // is built: it replaces the C++ factory with one whose editor lays out its
    // own selector and applies outside the preferences, and it has no form.
    QStringList unported = declined;
    if (Utils::anyOf(ExtensionSystem::PluginManager::plugins(),
                     [](ExtensionSystem::PluginSpec *spec) {
                         return spec->name() == "ClangFormat"
                                && spec->state() == ExtensionSystem::PluginSpec::Running;
                     })) {
        // CppEditor::Constants::CPP_CODE_STYLE_SETTINGS_ID, spelled out rather
        // than reached for: QuickUi does not depend on CppEditor. A rename
        // fails safe, leaving the page named below.
        unported.removeOne("Code Style [A.Cpp.Code Style]");
    }
    QVERIFY2(unported.isEmpty(),
             qPrintable("pages back on widgets: " + unported.join(", ")));

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
    described.setDisplayStyle(Utils::SelectionAspect::DisplayStyle::ComboBox);
    described.addOption("One");
    described.addOption("Two");

    // The same aspect, asking to be read at a glance instead. Which control it
    // gets is its own answer, the way the widget renderer has always taken it.
    Utils::SelectionAspect asRadioButtons(&page);
    asRadioButtons.setDisplayStyle(Utils::SelectionAspect::DisplayStyle::RadioButtons);
    asRadioButtons.addOption("One");
    asRadioButtons.addOption("Two");

    // No fill callback, so it has no choices to describe and no way to say
    // which of them is current - its value is a choice id.
    Utils::StringSelectionAspect undescribed(&page);

    QtcQuick::AspectContainerModel model(&page);
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.index(0, 0).data(QtcQuick::AspectContainerModel::KindRole).toInt(),
             int(QtcQuick::AspectContainerModel::Selection));
    QCOMPARE(model.index(1, 0).data(QtcQuick::AspectContainerModel::KindRole).toInt(),
             int(QtcQuick::AspectContainerModel::RadioGroup));
    QCOMPARE(model.index(2, 0).data(QtcQuick::AspectContainerModel::KindRole).toInt(),
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

// A list whose items are named, so that a move can be read off the rows.
static std::shared_ptr<Utils::BaseAspect> makeNamedItem(const QString &name)
{
    auto item = std::make_shared<Utils::AspectContainer>();
    auto label = new Utils::StringAspect(item.get());
    label->setDisplayStyle(Utils::StringAspect::LineEditDisplay);
    label->setLabelText("Name");
    label->setValue(name);
    return item;
}

static QStringList rowLabels(QQuickItem *delegate)
{
    QStringList labels;
    for (QQuickItem *label : findQmlNamed(delegate, "aspectListRowLabel"))
        labels << label->property("text").toString();
    return labels;
}

static Utils::AspectList *orderedList(Utils::AspectContainer *page)
{
    auto list = new Utils::AspectList(page);
    list->setLabelText("Mappings");
    list->setDisplayStyle(Utils::AspectList::DisplayStyle::ListViewWithDetails);
    list->setOrdered(true);
    list->setCreateItemFunction([] { return makeNamedItem("new"); });
    list->listViewDataCallback = [](Utils::BaseAspect *item, int role) -> QVariant {
        if (role != Qt::DisplayRole)
            return {};
        auto container = static_cast<Utils::AspectContainer *>(item);
        return container->aspects().first()->variantValue();
    };
    for (const QString &name : {QString("first"), QString("second"), QString("third")})
        list->addItem(makeNamedItem(name));
    list->apply();
    return list;
}

void QuickUiTest::testAnOrderedListMovesTheCurrentItem()
{
    // Some lists are tried in order - Axivion's path mappings are - and the
    // order is the user's. What may move where is the aspect's answer, so that
    // the widget list and the Quick one agree; the view only says which item
    // is current.
    Utils::AspectContainer page;
    page.setAutoApply(false);
    Utils::AspectList *list = orderedList(&page);

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "AspectListDelegate"));
    QTRY_COMPARE(rowLabels(delegate), QStringList({"first", "second", "third"}));

    QQuickItem *up = findQmlNamed(delegate, "aspectListMoveUpButton").value(0);
    QQuickItem *down = findQmlNamed(delegate, "aspectListMoveDownButton").value(0);
    QVERIFY(up);
    QVERIFY(down);

    // Nothing is current, so there is nothing to move.
    QVERIFY(!up->property("enabled").toBool());
    QVERIFY(!down->property("enabled").toBool());

    // The view says which item is current; the aspect answers what may be done
    // to it. The first item cannot go up.
    QQuickItem *view = findQmlComponent(delegate, "QQuickListView");
    QVERIFY(view);
    view->setProperty("currentIndex", 0);
    QTRY_COMPARE(list->currentIndex(), 0);
    QVERIFY(!up->property("enabled").toBool());
    QVERIFY(down->property("enabled").toBool());

    QMetaObject::invokeMethod(down, "clicked");
    QCOMPARE(list->currentIndex(), 1);
    QTRY_COMPARE(rowLabels(delegate), QStringList({"second", "first", "third"}));
    // The view followed the item rather than staying where it was.
    QCOMPARE(view->property("currentIndex").toInt(), 1);

    QMetaObject::invokeMethod(up, "clicked");
    QCOMPARE(list->currentIndex(), 0);
    QTRY_COMPARE(rowLabels(delegate), QStringList({"first", "second", "third"}));

    // The order is what Apply keeps.
    view->setProperty("currentIndex", 2);
    QTRY_COMPARE(list->currentIndex(), 2);
    QMetaObject::invokeMethod(up, "clicked");
    page.apply();
    QStringList applied;
    for (const std::shared_ptr<Utils::BaseAspect> &item : list->items()) {
        auto container = static_cast<Utils::AspectContainer *>(item.get());
        applied << container->aspects().first()->variantValue().toString();
    }
    QCOMPARE(applied, QStringList({"first", "third", "second"}));
}

void QuickUiTest::testAnUnorderedListOffersNoMoveButtons()
{
    // Most lists mean nothing by their order, and two buttons that would do
    // nothing are worse than none.
    Utils::AspectContainer page;
    page.setAutoApply(false);
    Utils::AspectList servers(&page);
    servers.setDisplayStyle(Utils::AspectList::DisplayStyle::ListViewWithDetails);
    servers.setCreateItemFunction([] { return makeNamedItem("new"); });

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "AspectListDelegate"));

    QQuickItem *up = findQmlNamed(delegate, "aspectListMoveUpButton").value(0);
    QVERIFY(up);
    QVERIFY(!up->isVisible());
    QVERIFY(!findQmlNamed(delegate, "aspectListMoveDownButton").value(0)->isVisible());
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

void QuickUiTest::testAButtonCanOfferAMenuInsteadOfActing()
{
    // Adding a toolchain is picking a kind, so Add offers rather than does.
    // The choices are the aspect's, so both renderers show the same menu and
    // hand back the same id - what a QMenu built beside a closure could not.
    QVariant picked;
    int acted = 0;
    Utils::AspectContainer page;
    Utils::ActionAspect add(&page);
    add.setActionText("Add");
    add.setQmlName("Add");
    add.setAction([&acted] { ++acted; });
    add.setChoices({{"GCC", {}, true, "ProjectExplorer.ToolChain.Gcc"},
                    {"Clang", {}, true, "ProjectExplorer.ToolChain.Clang"},
                    {"MSVC", {}, false, "ProjectExplorer.ToolChain.Msvc"}});
    add.setOnChoice([&picked](const QVariant &id) { picked = id; });

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *button = nullptr;
    QTRY_VERIFY(button = findButton(quickWidget->rootObject(), "Add"));

    // A button with choices does not act on its own: clicking it opens the
    // menu. Checked because the delegate has to tell the two apart, and a
    // plain click would otherwise run the action nobody asked for.
    QMetaObject::invokeMethod(button, "clicked");
    QCOMPARE(acted, 0);

    // The menu's items are the popup's, not the page's, so they are asked of
    // the Menu rather than found in the item tree.
    QQuickItem * const delegate = findQmlComponent(quickWidget->rootObject(), "ButtonDelegate");
    QVERIFY(delegate);
    QObject * const menuObject = delegate->property("menu").value<QObject *>();
    QVERIFY(menuObject);
    QCOMPARE(menuObject->property("count").toInt(), 3);
    QList<QQuickItem *> items;
    for (int i = 0; i < 3; ++i) {
        QQuickItem *item = nullptr;
        QMetaObject::invokeMethod(menuObject, "itemAt", Q_RETURN_ARG(QQuickItem *, item),
                                  Q_ARG(int, i));
        QVERIFY(item);
        items << item;
    }
    QCOMPARE(items.at(0)->property("text").toString(), QString("GCC"));
    // A kind that cannot be created is offered but not enabled.
    QCOMPARE(items.at(2)->property("enabled").toBool(), false);

    QMetaObject::invokeMethod(items.at(1), "triggered");
    QCOMPARE(picked.toString(), QString("ProjectExplorer.ToolChain.Clang"));
    QCOMPARE(acted, 0);

    // And the same aspect in a widget layout, where the choices are a QMenu on
    // the push button rather than its clicked() signal.
    Layouting::Column column{&add};
    const std::unique_ptr<QWidget> widget(column.emerge());
    QVERIFY(widget);
    QAbstractButton *pushButton = nullptr;
    for (QAbstractButton *candidate : widget->findChildren<QAbstractButton *>()) {
        if (candidate->text() == "Add")
            pushButton = candidate;
    }
    QVERIFY(pushButton);
    QMenu * const menu = pushButton->findChild<QMenu *>();
    QVERIFY(menu);
    const QList<QAction *> actions = menu->actions();
    QCOMPARE(actions.size(), 3);
    QCOMPARE(actions.at(0)->text(), QString("GCC"));
    QVERIFY(!actions.at(2)->isEnabled());
    picked = {};
    actions.at(1)->trigger();
    QCOMPARE(picked.toString(), QString("ProjectExplorer.ToolChain.Clang"));
    QCOMPARE(acted, 0);

    // A button with no choices still does what it is for.
    Utils::AspectContainer plainPage;
    Utils::ActionAspect plain(&plainPage);
    plain.setActionText("Re-detect");
    plain.setQmlName("Redetect");
    plain.setAction([&acted] { ++acted; });
    const std::unique_ptr<QWidget> plainForm(QtcQuick::createGenericAspectForm(&plainPage));
    auto plainQuick = plainForm->findChild<QQuickWidget *>();
    QVERIFY(plainQuick);
    QQuickItem *plainButton = nullptr;
    QTRY_VERIFY(plainButton = findButton(plainQuick->rootObject(), "Re-detect"));
    QMetaObject::invokeMethod(plainButton, "clicked");
    QCOMPARE(acted, 1);
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

void QuickUiTest::testATriStateSettingCanSayNeither()
{
    // Some settings have a third state - a plugin the language server has not
    // been told about, a project leaving a setting to the global one - and a
    // choice of three named options is a poor way to say it. The check box
    // shows "neither" without being able to be clicked into it.
    Utils::AspectContainer page;
    page.setAutoApply(false);
    Utils::TriStateAspect plugin(&page);
    plugin.setUseCheckBox(true);
    plugin.setLabelText("pyflakes");
    plugin.setValue(Utils::TriState::Default);

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "TriStateDelegate"));
    QCOMPARE(delegate->property("text").toString(), QString("pyflakes"));
    QCOMPARE(delegate->property("checkState").toInt(), int(Qt::PartiallyChecked));

    // Clicking decides, and decides on one of the two states a click can mean.
    QMetaObject::invokeMethod(delegate, "toggle");
    QMetaObject::invokeMethod(delegate, "toggled");
    QCOMPARE(delegate->property("checkState").toInt(), int(Qt::Checked));
    QCOMPARE(plugin.volatileValue(), int(Utils::TriState::EnabledValue));

    QMetaObject::invokeMethod(delegate, "toggle");
    QMetaObject::invokeMethod(delegate, "toggled");
    QCOMPARE(delegate->property("checkState").toInt(), int(Qt::Unchecked));
    QCOMPARE(plugin.volatileValue(), int(Utils::TriState::DisabledValue));

    // And the aspect can still say "neither" from C++.
    plugin.setValue(Utils::TriState::Default);
    QTRY_COMPARE(delegate->property("checkState").toInt(), int(Qt::PartiallyChecked));
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

void QuickUiTest::testAnAspectCanHandOutAContainerToDraw()
{
    Utils::AspectContainer page;
    Utils::ContainerAspect extra(&page);
    extra.setQmlName("Extra");

    Utils::AspectContainer handedOver;
    Utils::BoolAspect flag(&handedOver);
    flag.setLabelText("The flag");
    flag.setValue(true);
    extra.setContainer(&handedOver);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pageQml = dir.filePath("HandOverPage.qml");
    {
        QFile file(pageQml);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"(
import QtQuick
import QtQuick.Layouts
import QtCreator.Ui

AspectPage {
    id: root
    AspectItems {
        Layout.fillWidth: true
        model: root.aspects.Extra.container
               ? AspectModels.container(root.aspects.Extra.container) : null
    }
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

    // The aspects of the handed-over container are drawn, not just reached: a
    // name QML cannot resolve is undefined rather than an error, and the page
    // then shows nothing and says nothing.
    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(rootItem, "BoolDelegate"));
    QCOMPARE(delegate->property("aspect").value<Utils::BaseAspect *>(), &flag);
    QCOMPARE(delegate->property("text").toString(), QString("The flag"));
    QCOMPARE(delegate->property("checked").toBool(), true);
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

void QuickUiTest::testAContainerCanReadAsOneRow()
{
    // Six choices that read as one ABI are one row, not six settings in a box.
    // Which of the two it is, is the container's to say: a page that draws
    // whatever a toolchain hands it cannot know.
    const auto build = [](Utils::AspectContainer &page, bool inRow) {
        auto group = new Utils::AspectContainer(&page);
        group->setLabelText("ABI:");
        group->setInlineRow(inRow);
        for (const char *name : {"Architecture", "Os", "Format"}) {
            auto part = new Utils::SelectionAspect(group);
            part->setQmlName(QString::fromLatin1(name));
            part->setDisplayStyle(Utils::SelectionAspect::DisplayStyle::ComboBox);
            part->addOption("first");
            part->addOption("second");
        }
    };

    Utils::AspectContainer stacked;
    build(stacked, false);
    const std::unique_ptr<QWidget> stackedForm(QtcQuick::createGenericAspectForm(&stacked));
    auto stackedQuick = stackedForm->findChild<QQuickWidget *>();
    QVERIFY(stackedQuick);
    QVERIFY(stackedQuick->rootObject());
    stackedForm->resize(800, 600);
    stackedForm->show();
    QVERIFY(QTest::qWaitForWindowExposed(stackedForm.get()));

    // A group box, and the parts one under the other.
    QVERIFY(findQmlComponent(stackedQuick->rootObject(), "GroupDelegate"));
    QVERIFY(!findQmlComponent(stackedQuick->rootObject(), "InlineGroupDelegate"));
    const QList<QQuickItem *> stackedParts
        = findQmlComponents(stackedQuick->rootObject(), "SelectionDelegate");
    QCOMPARE(stackedParts.size(), 3);
    QVERIFY(stackedParts.at(1)->y() > stackedParts.at(0)->y());
    QCOMPARE(stackedParts.at(1)->x(), stackedParts.at(0)->x());

    Utils::AspectContainer inline_;
    build(inline_, true);
    const std::unique_ptr<QWidget> inlineForm(QtcQuick::createGenericAspectForm(&inline_));
    auto inlineQuick = inlineForm->findChild<QQuickWidget *>();
    QVERIFY(inlineQuick);
    QVERIFY(inlineQuick->rootObject());
    inlineForm->resize(800, 600);
    inlineForm->show();
    QVERIFY(QTest::qWaitForWindowExposed(inlineForm.get()));

    // No group box, and the parts side by side.
    QVERIFY(!findQmlComponent(inlineQuick->rootObject(), "GroupDelegate"));
    QQuickItem * const row = findQmlComponent(inlineQuick->rootObject(), "InlineGroupDelegate");
    QVERIFY(row);
    QCOMPARE(row->property("labelText").toString(), QString("ABI:"));
    const QList<QQuickItem *> inlineParts
        = findQmlComponents(inlineQuick->rootObject(), "SelectionDelegate");
    QCOMPARE(inlineParts.size(), 3);
    QVERIFY(inlineParts.at(1)->x() > inlineParts.at(0)->x());
    QCOMPARE(inlineParts.at(1)->y(), inlineParts.at(0)->y());

    // And it fits: a row of form-width controls is shrunk rather than pushing
    // the page wider than it is.
    QVERIFY(row->width() <= inlineQuick->rootObject()->width());
    for (QQuickItem * const part : inlineParts)
        QVERIFY(part->width() > 0);
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

// Labels are written for the widget renderer, which turns "&" into a keyboard
// accelerator. Qt Quick Controls has no mnemonics, so a label drawn as written
// shows the ampersand - which is what every ported page did.
void QuickUiTest::testLabelDropsItsAcceleratorForQuick()
{
    Utils::AspectContainer page;
    Utils::BoolAspect flag(&page);
    flag.setLabelText("Ta&b size:");
    Utils::IntegerAspect number(&page);
    number.setLabelText("Bells && whistles:");

    // The aspect still says what it was given: the widget renderer needs it.
    QCOMPARE(flag.labelText(), QString("Ta&b size:"));

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *box = nullptr;
    QTRY_VERIFY(box = findQmlComponent(quickWidget->rootObject(), "BoolDelegate"));
    QCOMPARE(box->property("text").toString(), QString("Tab size:"));

    // And an ampersand that was meant to be one survives as one.
    QQuickItem *spin = nullptr;
    QTRY_VERIFY(spin = findQmlComponent(quickWidget->rootObject(), "IntegerDelegate"));
    QCOMPARE(spin->property("labelText").toString(), QString("Bells & whistles:"));
}

// A path aspect shares its delegate with a plain string, and shared it whole:
// the field was there and the way to browse for a path was not.
void QuickUiTest::testPathAspectOffersSomewhereToBrowseFrom()
{
    Utils::AspectContainer page;
    Utils::FilePathAspect path(&page);
    path.setLabelText("A path");
    path.setExpectedKind(Utils::PathChooserKind::ExistingCommand);
    Utils::StringAspect text(&page);
    text.setLabelText("A string");
    text.setDisplayStyle(Utils::StringAspect::LineEditDisplay);

    // Not shown: an item's visibility does not need a window, and a window
    // shown here takes the focus a later test is checking for.
    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QList<QQuickItem *> buttons;
    QTRY_VERIFY((buttons = findQmlNamed(quickWidget->rootObject(), "browseButton")).size() == 2);
    QCOMPARE(buttons.size(), 2);

    // One for each StringDelegate, but only the one drawing a path shows it.
    int shown = 0;
    for (QQuickItem *button : buttons)
        shown += button->isVisible() ? 1 : 0;
    QCOMPARE(shown, 1);
}

// A choice can be present and not selectable - an external diff where there is
// no "diff" on the PATH. AspectPresentation::Choice has said so all along and
// the Quick delegates were not reading it.
void QuickUiTest::testAChoiceCanBeThereWithoutBeingOffered()
{
    Utils::AspectContainer page;
    Utils::SelectionAspect choice(&page);
    choice.setDisplayStyle(Utils::SelectionAspect::DisplayStyle::RadioButtons);
    choice.setLabelText("Pick one");
    choice.addOption("Available");
    Utils::SelectionAspect::Option unavailable("Unavailable", {}, {});
    unavailable.enabled = false;
    choice.addOption(unavailable);

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *group = nullptr;
    QTRY_VERIFY(group = findQmlComponent(quickWidget->rootObject(), "RadioGroupDelegate"));

    QList<QQuickItem *> buttons;
    QTRY_VERIFY((buttons = findQmlComponents(group, "RadioButton")).size() == 2);
    QVERIFY(buttons.at(0)->isEnabled());
    QVERIFY(!buttons.at(1)->isEnabled());
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

void QuickUiTest::testARefilledListKeepsWhatWasPicked()
{
    // A list that is refilled while the page is open - the kit's device type,
    // a toolchain's ABI - has the same thing picked afterwards. A Qt Quick
    // ComboBox puts currentIndex back to 0 when its model changes, and the
    // binding to the aspect only runs again when the aspect's value changes,
    // which refilling does not do. So the control quietly showed the first
    // entry while the aspect held the right one.
    Utils::AspectContainer page;
    Utils::SelectionAspect choice(&page);
    choice.setQmlName("Choice");
    choice.setLabelText("Device type:");
    choice.setDisplayStyle(Utils::SelectionAspect::DisplayStyle::ComboBox);
    const auto fill = [&choice] {
        choice.clearOptions();
        for (const char *name : {"Android Device", "Boot2Qt Device", "Desktop"})
            choice.addOption(QLatin1String(name));
    };
    fill();
    choice.setValue(2);

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *combo = nullptr;
    QTRY_VERIFY(combo = findQmlComponent(quickWidget->rootObject(), "ComboBox"));
    QTRY_COMPARE(combo->property("currentIndex").toInt(), 2);

    // Refilled with the same entry current: the aspect's value never changes,
    // so nothing tells the control to look at it again.
    fill();
    choice.setValue(2);
    QTRY_COMPARE(combo->property("currentIndex").toInt(), 2);
    QCOMPARE(combo->property("count").toInt(), 3);

    // And picking from the refilled list still writes back.
    QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 0));
    QCOMPARE(choice.volatileValue(), 0);
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
    const QList<QQuickItem *> rowTexts = findQmlNamed(row, "aspectListRowLabel");
    QCOMPARE(rowTexts.size(), 1);
    QVERIFY(rowTexts.first()->property("font").value<QFont>().strikeOut());

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
        case Qt::ForegroundRole:
            return index.column() == ColumnLocked ? QVariant(QColor(Qt::red)) : QVariant();
        case Qt::BackgroundRole:
            return index.column() == ColumnLocked ? QVariant(QColor(Qt::yellow)) : QVariant();
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

// One column and nothing to call it, which is what a plain list of items is -
// Python's interpreters, a list of servers.
class NamelessColumnModel : public QAbstractTableModel
{
public:
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : 2;
    }
    int columnCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : 1;
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role != Qt::DisplayRole)
            return {};
        return index.row() == 0 ? QString("first") : QString("second");
    }
    // As Utils::TreeModel answers for a model that was never given a header,
    // rather than QAbstractItemModel's own default of the column number.
    QVariant headerData(int, Qt::Orientation, int) const override { return {}; }
};

class NamelessTableAspect : public Utils::StringListAspect
{
public:
    using StringListAspect::StringListAspect;

    QAbstractItemModel *tableModel() override { return &m_model; }

    Utils::AspectPresentation presentation() const override
    {
        Utils::AspectPresentation p = StringListAspect::presentation();
        p.control = Utils::AspectControls::Table;
        return p;
    }

    NamelessColumnModel m_model;
};

void QuickUiTest::testATableWithNoColumnNamesHasNoHeader()
{
    // A header bar with nothing in it is not what the widget view showed, and
    // the style's own heading assigns the missing name to its label - which the
    // engine warns about and nothing else notices.
    Utils::AspectContainer page;
    NamelessTableAspect table(&page);
    table.setLabelText("Rows");

    QStringList warnings;
    const QMetaObject::Connection connection = QObject::connect(
        QtcQuick::engine(), &QQmlEngine::warnings, QtcQuick::engine(),
        [&warnings](const QList<QQmlError> &errors) {
            for (const QQmlError &error : errors)
                warnings << error.toString();
        });
    const QScopeGuard disconnect([connection] { QObject::disconnect(connection); });

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QQuickItem *view = nullptr;
    QTRY_VERIFY(view = tableViewOf(quickWidget->rootObject()));
    QTRY_COMPARE(view->property("rows").toInt(), 2);

    QVERIFY(!findQmlComponent(quickWidget->rootObject(), "HorizontalHeaderView"));
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join("; ")));
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

// The widget tables these replace used ExtendedSelection, and the pages that
// act on a selection - export these parsers, remove those kits - were written
// for more than one row at a time.
void QuickUiTest::testTableAspectRemovesEverySelectedRow()
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

    // Three rows, so that removing two leaves something behind.
    QQuickItem *add = findButton(delegate, "Add");
    QVERIFY(add);
    QMetaObject::invokeMethod(add, "clicked");
    QCOMPARE(table.m_model.words().size(), 3);

    auto shown = view->property("model").value<QAbstractItemModel *>();
    QVERIFY(shown);
    auto selection = view->property("selectionModel").value<QItemSelectionModel *>();
    QVERIFY(selection);
    selection->select(shown->index(0, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
    selection->select(shown->index(2, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);

    // Both of them, and the delegate reports them in the aspect's own order.
    QTRY_COMPARE(delegate->property("selectedRows").toList().size(), 2);
    QCOMPARE(delegate->property("selectedRows").toList().first().toInt(), 0);

    QQuickItem *remove = findButton(delegate, "Remove");
    QVERIFY(remove);
    QVERIFY(remove->property("enabled").toBool());
    QMetaObject::invokeMethod(remove, "clicked");

    // The one that was not selected, and it is the one that was in the middle:
    // removing from the top would have shifted the others out from under it.
    QCOMPARE(table.m_model.words(), QStringList({"off/blue/two"}));
}

void QuickUiTest::testFieldSaysWhatIsWrongAndKeepsItOut()
{
    // An aspect's validation function is the widget renderer's to read, so a
    // page that set one had no validation at all once it moved to Qt Quick.
    Utils::AspectContainer page;
    page.setAutoApply(false);
    Utils::StringAspect keyword(&page);
    keyword.setLabelText("Keyword");
    keyword.setDisplayStyle(Utils::StringAspect::LineEditDisplay);
    keyword.setValue("TODO");
    keyword.setValidationFunction([](const QString &text) -> Utils::Result<> {
        if (text.contains(' '))
            return Utils::ResultError(QString("No spaces, please."));
        return Utils::ResultOk;
    });

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *field = nullptr;
    QTRY_VERIFY(field = findQmlComponent(quickWidget->rootObject(), "TextField"));
    const QList<QQuickItem *> messages = findQmlNamed(quickWidget->rootObject(),
                                                      "validationMessage");
    QCOMPARE(messages.size(), 1);
    QQuickItem *message = messages.first();

    // Nothing wrong with what is in it, so nothing is said.
    QCOMPARE(field->property("text").toString(), QString("TODO"));
    QVERIFY(!message->property("visible").toBool());

    // A value the aspect rejects is shown as rejected, and does not reach it.
    field->setProperty("text", "TO DO");
    QTRY_VERIFY(message->property("visible").toBool());
    QCOMPARE(message->property("text").toString(), QString("No spaces, please."));
    QMetaObject::invokeMethod(field, "editingFinished");
    QCOMPARE(keyword.volatileValue(), QString("TODO"));

    // And one it accepts does, with nothing left over from the last complaint.
    field->setProperty("text", "FIXME");
    QMetaObject::invokeMethod(field, "editingFinished");
    QCOMPARE(keyword.volatileValue(), QString("FIXME"));
    QTRY_VERIFY(!message->property("visible").toBool());
}

namespace {

class GroupedTool
{
public:
    friend bool operator==(const GroupedTool &, const GroupedTool &) = default;

    QString name;
    bool autoDetected = false;
};

class GroupedToolsModel : public Utils::TypedGroupedModel<GroupedTool>
{
public:
    explicit GroupedToolsModel(bool withDefault)
    {
        setShowDefault(withDefault);
        setHeader({"Name"});
        setFilters("Auto-detected", {{"Manual", [this](int row) {
                                          return !item(row).autoDetected;
                                      }}});
        appendItem({"found", true});
        appendItem({"mine", false});
        if (withDefault)
            setDefaultRow(0);
    }

    int cloneRow(int row) override
    {
        return appendVolatileItem({item(row).name + " (copy)", false});
    }

private:
    QVariant variantData(int row, int column, int role) const override
    {
        if (role == Qt::DisplayRole && column == 0)
            return item(row).name;
        return {};
    }
};

} // namespace

void QuickUiTest::testGroupedListShowsItsGroupsAndActsOnTheCurrentItem()
{
    Utils::AspectContainer page;
    page.setAutoApply(false);
    GroupedToolsModel model(/*withDefault=*/true);
    Utils::GroupedListAspect tools(&page);
    tools.setLabelText("Tools");
    tools.setModel(&model);
    tools.setShowsDefault(true);
    tools.setCanRemoveRow([&model](int row) { return !model.item(row).autoDetected; });

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);

    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "GroupedListDelegate"));

    // The tree is over the groups, with the items under them - not a flat list
    // of the model's rows.
    QAbstractItemModel *tree = tools.displayModel();
    QVERIFY(tree);
    QCOMPARE(tree->rowCount({}), 2);
    QCOMPARE(tree->rowCount(tree->index(0, 0)), 1);
    QCOMPARE(tree->rowCount(tree->index(1, 0)), 1);
    // A group heading is not an item, so nothing acts on it.
    QCOMPARE(tools.rowForIndex(tree->index(0, 0)), -1);
    QVERIFY(tools.rowForIndex(tree->index(0, 0, tree->index(0, 0))) >= 0);

    QQuickItem *clone = findQmlNamed(delegate, "groupedListCloneButton").value(0);
    QQuickItem *remove = findQmlNamed(delegate, "groupedListRemoveButton").value(0);
    QQuickItem *makeDefault = findQmlNamed(delegate, "groupedListMakeDefaultButton").value(0);
    QVERIFY(clone && remove && makeDefault);

    // Nothing current: nothing to act on.
    QVERIFY(!clone->property("enabled").toBool());
    QVERIFY(!remove->property("enabled").toBool());
    QVERIFY(!makeDefault->property("enabled").toBool());

    // The auto-detected one is not the user's to remove, and it is already the
    // default; it can still be copied.
    const int found = model.item(0).autoDetected ? 0 : 1;
    const int mine = 1 - found;
    tools.setCurrentRow(found);
    QTRY_VERIFY(clone->property("enabled").toBool());
    QVERIFY(!remove->property("enabled").toBool());
    QVERIFY(!makeDefault->property("enabled").toBool());

    tools.setCurrentRow(mine);
    QTRY_VERIFY(remove->property("enabled").toBool());
    QVERIFY(makeDefault->property("enabled").toBool());
    QCOMPARE(remove->property("text").toString(), QString("Remove"));

    // Removing is not applied yet, so the button offers to take it back.
    QMetaObject::invokeMethod(remove, "clicked");
    QVERIFY(model.isRemoved(mine));
    tools.setCurrentRow(mine);
    QTRY_COMPARE(remove->property("text").toString(), QString("Restore"));
    QVERIFY(remove->property("enabled").toBool());
    QVERIFY(!clone->property("enabled").toBool());

    // On the cells, not just the aspect: a delegate that fails to build leaves
    // the view saying it has rows and drawing none of them.
    QStringList drawn;
    for (QQuickItem *cell : findQmlComponents(delegate, "TreeViewDelegate"))
        drawn << cell->property("text").toString();
    QVERIFY2(!drawn.isEmpty(), "the list drew no cells at all");
    // The two group headings and the two items under them. The default one
    // says so in its name, which is the model's doing.
    QVERIFY2(drawn.contains("Auto-detected") && drawn.contains("Manual"),
             qPrintable("drawn: " + drawn.join(", ")));
    QVERIFY2(Utils::anyOf(drawn, [](const QString &t) { return t.startsWith("found"); })
                 && drawn.contains("mine"),
             qPrintable("drawn: " + drawn.join(", ")));

    // Cloning selects the copy, which the view follows.
    tools.setCurrentRow(found);
    QMetaObject::invokeMethod(clone, "clicked");
    QCOMPARE(model.itemCount(), 3);
    QCOMPARE(tools.currentRow(), 2);
}

void QuickUiTest::testFieldWaitsForAnAnswerItHasToFetch()
{
    // Some checks cannot be made on the spot: whether the path a field holds
    // is really a debugger is decided by running it. The aspect answers
    // nothing until it knows, and says so when it does.
    Utils::AspectContainer page;
    page.setAutoApply(false);
    Utils::FilePathAspect binary(&page);
    binary.setLabelText("Path");
    binary.setValue(QString("/good"));

    QList<QPromise<Utils::AsyncValidationResult> *> promises;
    binary.setValidationFunction(
        Utils::AsyncValidationFunction([&promises](const QString &)
                                       -> Utils::AsyncValidationFuture {
            auto p = new QPromise<Utils::AsyncValidationResult>;
            promises.append(p);
            p->start();
            return p->future();
        }));
    const QScopeGuard deletePromises([&promises] { qDeleteAll(promises); });

    std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *field = nullptr;
    QTRY_VERIFY(field = findQmlComponent(quickWidget->rootObject(), "TextField"));
    const QList<QQuickItem *> messages = findQmlNamed(quickWidget->rootObject(),
                                                      "validationMessage");
    QCOMPARE(messages.size(), 1);
    QQuickItem *message = messages.first();

    // Asked, and nothing said yet: a field that showed the last answer would
    // be saying it about text that is no longer there.
    field->setProperty("text", "/bad");
    QTRY_VERIFY(!promises.isEmpty());
    QVERIFY(!message->property("visible").toBool());

    // The answer arrives, and the field says so without being asked again.
    promises.last()->addResult(Utils::make_unexpected(QString("Not a debugger.")));
    promises.last()->finish();
    QTRY_VERIFY(message->property("visible").toBool());
    QCOMPARE(message->property("text").toString(), QString("Not a debugger."));

    // And a value the aspect has called wrong does not reach it.
    QMetaObject::invokeMethod(field, "editingFinished");
    QCOMPARE(binary.volatileValue(), QString("/good"));

    // The rest is driven directly rather than through the field: how often a
    // binding happens to re-evaluate is not the point being made. The form
    // goes first - the aspect remembers one candidate, the one being asked
    // about, and a field still on screen asks about its own text.

    // Asked about something else, the aspect must forget what it knew - until
    // the new answer lands there is nothing to say about it - and asking twice
    // must not send it looking twice.
    form.reset();

    const int checksBefore = promises.size();
    QCOMPARE(binary.validationMessage("/other"), QString());
    QCOMPARE(binary.validationMessage("/other"), QString());
    QCOMPARE(promises.size(), checksBefore + 1);

    // An answer about a value that is no longer there is dropped. Both are
    // finished here, oldest first, so waiting for the newer one's effect is
    // proof that the older one has already been delivered - there is no moment
    // to wait through and nothing to time out on.
    QSignalSpy validated(&binary, &Utils::BaseAspect::validationMessageChanged);
    const int inFlight = promises.size();
    QCOMPARE(binary.validationMessage("/first"), QString());
    QCOMPARE(binary.validationMessage("/second"), QString());
    QCOMPARE(promises.size(), inFlight + 2);

    promises.at(inFlight)->addResult(Utils::make_unexpected(QString("About the old text.")));
    promises.at(inFlight)->finish();
    promises.at(inFlight + 1)->addResult(Utils::make_unexpected(QString("About the new text.")));
    promises.at(inFlight + 1)->finish();

    QTRY_COMPARE(binary.validationMessage("/second"), QString("About the new text."));
    QCOMPARE(validated.count(), 1);
}

namespace {

// An aspect that reports a tree of values rather than letting one be set - a
// qbs profile's properties are the first of these.
class TreeReportingAspect : public Utils::BaseAspect
{
public:
    TreeReportingAspect()
    {
        m_model.setHeader({"Key", "Value"});
        auto branch = new Utils::StaticTreeItem(QStringList{"cpp", QString()});
        branch->appendChild(new Utils::StaticTreeItem(QStringList{"cxxLanguageVersion", "c++20"}));
        branch->appendChild(new Utils::StaticTreeItem(QStringList{"debugInformation", "true"}));
        m_model.rootItem()->appendChild(branch);
        m_model.rootItem()->appendChild(new Utils::StaticTreeItem(QStringList{"qbs", "3.0"}));
    }

    Utils::AspectPresentation presentation() const override
    {
        Utils::AspectPresentation p = BaseAspect::presentation();
        p.control = Utils::AspectControls::Tree;
        return p;
    }

    QAbstractItemModel *tableModel() override { return &m_model; }

private:
    Utils::TreeModel<Utils::TreeItem, Utils::StaticTreeItem> m_model{this};
};

} // namespace

void QuickUiTest::testTreeShowsWhatTheAspectHandsOut()
{
    Utils::AspectContainer page;
    page.setAutoApply(false);
    TreeReportingAspect properties;
    properties.setLabelText("Profile properties");
    page.registerAspect(&properties);

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);

    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "TreeDelegate"));

    // The aspect's own model, not a copy: what it shows follows what the page
    // puts in it. The view sees it through the filter, which is always in the
    // chain so that there is one index space either way.
    QQuickItem *view = findQmlNamed(delegate, "aspectTree").value(0);
    QVERIFY(view);
    auto shown = view->property("model").value<QAbstractItemModel *>();
    QVERIFY(shown);
    auto filtered = qobject_cast<QSortFilterProxyModel *>(shown);
    QVERIFY(filtered);
    QCOMPARE(filtered->sourceModel(), properties.tableModel());

    // A tree, so the branch has rows under it rather than beside it.
    QAbstractItemModel *tree = properties.tableModel();
    QCOMPARE(tree->rowCount({}), 2);
    QCOMPARE(tree->rowCount(tree->index(0, 0)), 2);
    QCOMPARE(tree->columnCount({}), 2);

    // Closed to begin with: only the two top-level rows are there to draw.
    QTRY_COMPARE(view->property("rows").toInt(), 2);
    QMetaObject::invokeMethod(delegate, "expandAll");
    QTRY_COMPARE(view->property("rows").toInt(), 4);

    // On the cells, not just the row count: a delegate that fails to build
    // leaves the view saying it has rows and drawing none of them. Read off
    // what is drawn rather than off the delegate, whose own text is only the
    // accessible name.
    QStringList drawn;
    for (QQuickItem *label : findQmlNamed(view, "tableCellLabel"))
        drawn << label->property("text").toString();
    QVERIFY2(!drawn.isEmpty(), "the tree drew no cells at all");
    QVERIFY2(drawn.contains("cxxLanguageVersion"),
             qPrintable("drawn: " + drawn.join(", ")));
    QVERIFY2(drawn.contains("c++20"), qPrintable("drawn: " + drawn.join(", ")));

    QMetaObject::invokeMethod(delegate, "collapseAll");
    QTRY_COMPARE(view->property("rows").toInt(), 2);

    // Filtering a tree by its top-level rows alone would hide the branch that
    // holds what was looked for, so a branch is kept when a child matches.
    filtered->setFilterFixedString("cxxLanguage");
    QCOMPARE(filtered->rowCount({}), 1);
    QCOMPARE(filtered->rowCount(filtered->index(0, 0)), 1);
    filtered->setFilterFixedString({});
    QCOMPARE(filtered->rowCount({}), 2);
}

namespace {

// A row of a tree whose second cell may be typed in and whose third is a check
// box - the Locator's filters are this shape.
class EditableRowItem : public Utils::TreeItem
{
public:
    EditableRowItem(const QString &name, const QString &prefix, bool included)
        : m_name(name), m_prefix(prefix), m_included(included)
    {}

    QVariant data(int column, int role) const override
    {
        switch (column) {
        case 0:
            return role == Qt::DisplayRole ? m_name : QVariant();
        case 1:
            return role == Qt::DisplayRole || role == Qt::EditRole ? m_prefix : QVariant();
        case 2:
            if (role == Qt::CheckStateRole)
                return m_included ? Qt::Checked : Qt::Unchecked;
            return {};
        }
        return {};
    }

    Qt::ItemFlags flags(int column) const override
    {
        if (column == 1)
            return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;
        if (column == 2)
            return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
        return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    }

    bool setData(int column, const QVariant &value, int role) override
    {
        if (column == 1 && role == Qt::EditRole) {
            m_prefix = value.toString();
            return true;
        }
        if (column == 2 && role == Qt::CheckStateRole) {
            m_included = value.toInt() == Qt::Checked;
            return true;
        }
        return false;
    }

    QString m_name;
    QString m_prefix;
    bool m_included = false;
};

class EditableTreeModel : public Utils::TreeModel<Utils::TreeItem, EditableRowItem>
{
public:
    using TreeModel::TreeModel;

    QVariant data(const QModelIndex &index, int role) const override
    {
        switch (role) {
        case Utils::AspectTable::EditableRole:
            return Utils::AspectTable::isWritable(flags(index));
        case Utils::AspectTable::CheckableRole:
            return flags(index).testFlag(Qt::ItemIsUserCheckable);
        default:
            return TreeModel::data(index, role);
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return Utils::AspectTable::withRoleNames(TreeModel::roleNames());
    }

    // As a model that may be reordered answers: what is being moved goes into
    // the mime data, and the drop puts it where it was asked for.
    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }

    QStringList mimeTypes() const override { return {"application/x-test-row"}; }

    QMimeData *mimeData(const QModelIndexList &indexes) const override
    {
        if (indexes.isEmpty())
            return nullptr;
        auto data = new QMimeData;
        data->setData("application/x-test-row", QByteArray::number(indexes.first().row()));
        return data;
    }

    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int,
                      const QModelIndex &parent) override
    {
        if (action != Qt::MoveAction || parent.isValid())
            return false;
        const int from = data->data("application/x-test-row").toInt();
        if (from < 0 || from >= rootItem()->childCount() || row < 0)
            return false;
        if (row == from || row == from + 1)
            return false;
        beginMoveRows({}, from, from, {}, row);
        Utils::TreeItem *item = takeItem(rootItem()->childAt(from));
        rootItem()->insertChild(row > from ? row - 1 : row, item);
        endMoveRows();
        return true;
    }
};

// The same shape as TreeReportingAspect, but with rows the user may change.
class EditableTreeAspect : public Utils::BaseAspect
{
public:
    EditableTreeAspect()
    {
        m_model.setHeader({"Filter", "Prefix", "Default"});
        m_model.rootItem()->appendChild(new EditableRowItem("Files", "f", true));
        m_model.rootItem()->appendChild(new EditableRowItem("Classes", "c", false));
    }

    Utils::AspectPresentation presentation() const override
    {
        Utils::AspectPresentation p = BaseAspect::presentation();
        p.control = Utils::AspectControls::Tree;
        p.allowReordering = m_reorderable;
        return p;
    }

    void setReorderable(bool reorderable) { m_reorderable = reorderable; }

    QAbstractItemModel *tableModel() override { return &m_model; }

    EditableRowItem *row(int index) const
    {
        return static_cast<EditableRowItem *>(m_model.rootItem()->childAt(index));
    }

private:
    bool m_reorderable = false;
    mutable EditableTreeModel m_model{this};
};

} // namespace

void QuickUiTest::testATreeCellIsWrittenToWhereItsModelSaysSo()
{
    // A tree reports what a page found, until its model says a cell may be
    // written to - the Locator's prefixes are edited in the tree that lists
    // them. Which cells those are is the model's answer, the same one a
    // QTreeView reads out of flags().
    Utils::AspectContainer page;
    page.setAutoApply(false);
    EditableTreeAspect filters;
    filters.setLabelText("Filters");
    page.registerAspect(&filters);

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "TreeDelegate"));
    QQuickItem *view = findQmlNamed(delegate, "aspectTree").value(0);
    QVERIFY(view);
    QTRY_COMPARE(view->property("rows").toInt(), 2);

    // The first column reports, the second is a field, the third a check box.
    QList<QQuickItem *> labels;
    QTRY_COMPARE((labels = findQmlNamed(view, "tableCellLabel")).size(), 2);
    QCOMPARE(labels.at(0)->property("text").toString(), QString("Files"));

    QList<QQuickItem *> fields;
    QTRY_COMPARE((fields = findQmlNamed(view, "tableCellField")).size(), 2);
    QCOMPARE(fields.at(0)->property("text").toString(), QString("f"));

    QList<QQuickItem *> checks;
    QTRY_COMPARE((checks = findQmlNamed(view, "tableCellCheckBox")).size(), 2);
    QCOMPARE(checks.at(0)->property("checked").toBool(), true);
    QCOMPARE(checks.at(1)->property("checked").toBool(), false);

    // Which row is current is answered in the aspect's own model, not the
    // filtered one: a page looks the row up in the model it owns, and an index
    // from the proxy finds nothing there.
    auto filtered = qobject_cast<QSortFilterProxyModel *>(
        view->property("model").value<QAbstractItemModel *>());
    QVERIFY(filtered);
    filtered->setFilterFixedString("Classes");
    QTRY_COMPARE(view->property("rows").toInt(), 1);
    auto selection = view->property("selectionModel").value<QItemSelectionModel *>();
    QVERIFY(selection);
    selection->setCurrentIndex(filtered->index(0, 0), QItemSelectionModel::ClearAndSelect);
    const auto current = delegate->property("currentIndex").value<QModelIndex>();
    QVERIFY(current.isValid());
    QCOMPARE(current.model(), filters.tableModel());
    QCOMPARE(current.data().toString(), QString("Classes"));
    filtered->setFilterFixedString({});
    QTRY_COMPARE(view->property("rows").toInt(), 2);

    // And what is typed and ticked reaches the model. Asked for again:
    // filtering destroys the cells and builds them anew, so the ones from
    // before it are gone.
    QTRY_COMPARE((fields = findQmlNamed(view, "tableCellField")).size(), 2);
    QTRY_COMPARE((checks = findQmlNamed(view, "tableCellCheckBox")).size(), 2);
    fields.at(0)->setProperty("text", "fi");
    QMetaObject::invokeMethod(fields.at(0), "editingFinished");
    QCOMPARE(filters.row(0)->m_prefix, QString("fi"));

    QMetaObject::invokeMethod(checks.at(1), "toggle");
    QMetaObject::invokeMethod(checks.at(1), "toggled");
    QCOMPARE(filters.row(1)->m_included, true);
}

void QuickUiTest::testAReorderableTreeMovesARowThroughItsModel()
{
    // Some trees are in the order the user put them in - External Tools is -
    // and a row is dragged somewhere else. The move goes through the model's
    // own mimeData()/dropMimeData(), which is how a QTreeView does an internal
    // move and where a model puts whatever else it needs to know.
    Utils::AspectContainer page;
    page.setAutoApply(false);
    EditableTreeAspect filters;
    filters.setReorderable(true);
    filters.setLabelText("Filters");
    page.registerAspect(&filters);

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "TreeDelegate"));
    QVERIFY2(delegate->property("reorderable").toBool(),
             "the tree was not told its order is the user's");

    QAbstractItemModel *model = filters.tableModel();
    QCOMPARE(model->index(0, 0).data().toString(), QString("Files"));
    QCOMPARE(model->index(1, 0).data().toString(), QString("Classes"));

    // The gesture is not what is measured here - it needs a window and a
    // pointer - but what it ends up calling is.
    QtcQuick::AspectModels models;
    QVERIFY(models.moveRow(model, model->index(0, 0), {}, 2));
    QCOMPARE(model->index(0, 0).data().toString(), QString("Classes"));
    QCOMPARE(model->index(1, 0).data().toString(), QString("Files"));

    // A tree that says nothing about its order does not offer to move a row.
    Utils::AspectContainer plain;
    plain.setAutoApply(false);
    EditableTreeAspect fixed;
    fixed.setLabelText("Filters");
    plain.registerAspect(&fixed);
    const std::unique_ptr<QWidget> plainForm(showForm(&plain));
    QVERIFY(plainForm);
    QQuickItem *plainDelegate = nullptr;
    QTRY_VERIFY(plainDelegate = findQmlComponent(
                    plainForm->findChild<QQuickWidget *>()->rootObject(), "TreeDelegate"));
    QVERIFY(!plainDelegate->property("reorderable").toBool());
}

void QuickUiTest::testAFieldCompletesAgainstWhatTheAspectOffers()
{
    // QtQuick.Controls has no completer, so the aspect's completions were
    // simply not offered on a Quick page.
    Utils::AspectContainer page;
    page.setAutoApply(false);
    Utils::StringAspect option(&page);
    option.setLabelText("Option");
    option.setDisplayStyle(Utils::StringAspect::LineEditDisplay);
    option.setCompletions({"indent", "indent-classes", "indent-switches", "pad-oper"});

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);

    QQuickItem *field = nullptr;
    QTRY_VERIFY(field = findQmlComponent(quickWidget->rootObject(), "TextField"));
    // A Popup is a QObject child of the field rather than an item in the
    // scene, so it is not where findQmlNamed() looks.
    QObject *popup = field->findChild<QObject *>("completionPopup");
    QVERIFY(popup);

    // Nothing typed, nothing offered: a popup over an empty field would be a
    // list of everything.
    QVERIFY(!popup->property("visible").toBool());
    QCOMPARE(popup->property("matches").toStringList().size(), 0);

    // What was typed narrows it, and what is already typed in full is not
    // worth offering again.
    popup->setProperty("prefix", "indent");
    QCOMPARE(popup->property("matches").toStringList(),
             QStringList({"indent-classes", "indent-switches"}));
    popup->setProperty("prefix", "pad");
    QCOMPARE(popup->property("matches").toStringList(), QStringList({"pad-oper"}));
    popup->setProperty("prefix", "nothing-like-this");
    QCOMPARE(popup->property("matches").toStringList().size(), 0);

    // Choosing one puts it in the field whole.
    field->setProperty("text", "pad");
    popup->setProperty("prefix", "pad");
    QMetaObject::invokeMethod(popup, "offer");
    QTRY_VERIFY(popup->property("visible").toBool());
    QMetaObject::invokeMethod(popup, "acceptCurrent");
    QCOMPARE(field->property("text").toString(), QString("pad-oper"));
    QTRY_VERIFY(!popup->property("visible").toBool());
}

void QuickUiTest::testSeveralLinesCompleteTheWordTheCursorIsIn()
{
    Utils::AspectContainer page;
    page.setAutoApply(false);
    Utils::StringAspect config(&page);
    config.setLabelText("Configuration");
    config.setDisplayStyle(Utils::StringAspect::TextEditDisplay);
    config.setCompletions({"indent", "indent-classes", "pad-oper"});
    config.setValue("style=allman\nind");

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);

    // Through the ScrollView: searching for "TextArea" from further up matches
    // TextAreaDelegate itself.
    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "TextAreaDelegate"));
    QQuickItem *scroll = findQmlComponent(delegate, "ScrollView");
    QVERIFY(scroll);
    QQuickItem *area = findQmlComponent(scroll, "TextArea");
    QVERIFY(area);
    QCOMPARE(area->property("text").toString(), QString("style=allman\nind"));
    QObject *popup = area->findChild<QObject *>("completionPopup");
    QVERIFY(popup);

    // The word the cursor is in, not everything typed so far: a whole config
    // file would match nothing.
    area->setProperty("cursorPosition", area->property("text").toString().length());
    QCOMPARE(area->property("word").toString(), QString("ind"));
    QCOMPARE(popup->property("matches").toStringList(),
             QStringList({"indent", "indent-classes"}));

    // And what is chosen replaces that word, leaving the rest of the line.
    QMetaObject::invokeMethod(popup, "offer");
    QTRY_VERIFY(popup->property("visible").toBool());
    QMetaObject::invokeMethod(popup, "acceptCurrent");
    QCOMPARE(area->property("text").toString(), QString("style=allman\nindent"));
}

void QuickUiTest::testListRowShowsWhatTheListSaysAboutTheItem()
{
    // A list row is a name, and for a list where the items are told apart by
    // how they look - To-Do's keywords - an icon and a colour as well. The
    // callback answers a QIcon, which QML cannot carry; see QtcQuick::iconUrl().
    Utils::AspectContainer page;
    page.setAutoApply(false);
    Utils::AspectList keywords(&page);
    keywords.setLabelText("Keywords");
    keywords.setDisplayStyle(Utils::AspectList::DisplayStyle::ListViewWithDetails);
    keywords.setCreateItemFunction([] {
        auto item = std::make_shared<Utils::AspectContainer>();
        auto name = new Utils::StringAspect(item.get());
        name->setLabelText("Name");
        name->setDisplayStyle(Utils::StringAspect::LineEditDisplay);
        name->setValue("TODO");
        return item;
    });
    const QIcon icon = QIcon(QPixmap(16, 16));
    keywords.listViewDataCallback = [&icon](Utils::AspectContainer *item, int role) -> QVariant {
        auto name = static_cast<Utils::StringAspect *>(item->aspects().first());
        switch (role) {
        case Qt::DisplayRole:
            return name->volatileValue();
        case Qt::DecorationRole:
            return icon;
        case Qt::ForegroundRole:
            return QColor(Qt::red);
        }
        return {};
    };

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "AspectListDelegate"));
    QQuickItem *add = findButton(delegate, "Add");
    QVERIFY(add);
    QMetaObject::invokeMethod(add, "clicked");

    QQuickItem *row = nullptr;
    QTRY_VERIFY(row = findQmlComponent(delegate, "ItemDelegate"));
    QCOMPARE(row->property("label").toString(), QString("TODO"));

    // The icon reaches QML as a URL the provider serves, and the provider
    // gives back the pixmap that went in - not a placeholder, and not null.
    const QUrl decoration = row->property("decoration").toUrl();
    QVERIFY2(!decoration.isEmpty(), "the row was given no icon");
    QCOMPARE(decoration.scheme(), QString("image"));
    QCOMPARE(decoration.host(), QString::fromLatin1(QtcQuick::IconProvider::name()));
    QtcQuick::IconProvider provider;
    QSize served;
    const QPixmap pixmap = provider.requestPixmap(decoration.path().mid(1), &served, {16, 16});
    QVERIFY2(!pixmap.isNull(), "the provider did not serve the icon back");
    QCOMPARE(served, QSize(16, 16));

    QCOMPARE(row->property("itemForeground").value<QColor>(), QColor(Qt::red));

    // And the row follows the item: the details pane is where the name is
    // edited, and nothing else tells the list that it changed.
    const QList<QQuickItem *> rowTexts = findQmlNamed(row, "aspectListRowLabel");
    QCOMPARE(rowTexts.size(), 1);
    QCOMPARE(rowTexts.first()->property("text").toString(), QString("TODO"));
    auto item = static_cast<Utils::AspectContainer *>(keywords.volatileItems().first().get());
    auto name = static_cast<Utils::StringAspect *>(item->aspects().first());
    name->setVolatileValue("FIXME");
    QTRY_COMPARE(rowTexts.first()->property("text").toString(), QString("FIXME"));
}

// A page on screen at a size a preferences dialog would give it, so that what
// is laid out is what the user sees. Returns the QQuickWidget's root item.
static QQuickItem *showPage(const QString &displayName, std::unique_ptr<QWidget> &keptAlive,
                            const QSize &size = {900, 600})
{
    Core::setAspectFormFactory([](Utils::AspectContainer *container) {
        return QtcQuick::createAspectForm(container);
    });
    Core::IOptionsPage *page = nullptr;
    for (Core::IOptionsPage *candidate : Core::IOptionsPage::allOptionsPages()) {
        if (candidate->displayName() == displayName && candidate->aspects().value_or(nullptr))
            page = candidate;
    }
    if (!page)
        return nullptr;
    Core::IOptionsPageWidget *widget = page->createWidget();
    if (!widget)
        return nullptr;

    // As the preferences dialog embeds a page: inside a scroll area, with
    // other focusable widgets around it. Both matter - a page on its own gets
    // keys the real one does not, because the widget focus chain has nowhere
    // else to go.
    auto host = std::make_unique<QWidget>();
    auto layout = new QVBoxLayout(host.get());
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLineEdit);
    auto area = new QScrollArea;
    area->setFrameStyle(QFrame::NoFrame | QFrame::Plain);
    area->setWidgetResizable(true);
    area->setWidget(widget);
    layout->addWidget(area);
    layout->addWidget(new QLineEdit);
    host->resize(size);
    host->show();
    if (!QTest::qWaitForWindowExposed(host.get()))
        return nullptr;
    QCoreApplication::processEvents();
    keptAlive = std::move(host);
    auto quickWidget = keptAlive->findChild<QQuickWidget *>();
    return quickWidget ? quickWidget->rootObject() : nullptr;
}

void QuickUiTest::testACodeEditorScrollsInsteadOfGrowingThePage()
{
    // A ScrollView reports the implicit size of what it scrolls, and the frame
    // around it sizes to that - so a preview holding more code than fits grew
    // the whole page to fit it, and the page scrolled instead of the editor.
    std::unique_ptr<QWidget> host;
    QQuickItem *root = showPage("Code Style", host, {640, 400});
    if (!root)
        QSKIP("The C++ Code Style page is not available here");

    QQuickItem *edit = nullptr;
    QTRY_VERIFY(edit = findQmlNamed(root, "codeStylePreviewText").value(0));
    const QString snippet = edit->property("text").toString();
    QVERIFY(!snippet.isEmpty());
    // More code than fits, which is the case that used to grow the page.
    edit->setProperty("text", snippet.repeated(8));
    QCoreApplication::processEvents();

    // The preview holds more code than its own height: this is the case that
    // used to grow the page.
    // The editor is the thing that scrolls.
    QQuickItem *scroll = edit;
    while (scroll
           && !QString::fromLatin1(scroll->metaObject()->className()).contains("ScrollView")) {
        scroll = scroll->parentItem();
    }
    QVERIFY(scroll);
    QVERIFY2(scroll->property("contentHeight").toReal() > scroll->height(),
             "The editor grew to fit the code instead of scrolling it");

    // And the page is no taller than the space it was given, so it is the
    // editor that scrolls and not the page. Any of the Code Style pages will
    // do: all of them show the same preview.
    QVERIFY2(root->property("contentHeight").toReal() <= root->height() + 1,
             qPrintable(QString::fromLatin1(root->metaObject()->className())
                        + QString(": page content %1 in %2")
                              .arg(root->property("contentHeight").toReal())
                              .arg(root->height())));
}

void QuickUiTest::testTabTypesAnIndentInACodeEditor()
{
    // Tab in an editor types an indent, and what an indent is is the code
    // style's answer. A Qt Quick TextEdit types a tab character whatever the
    // style says, which is wrong wherever the style says spaces.
    std::unique_ptr<QWidget> host;
    QQuickItem *root = showPage("Code Style", host);
    if (!root)
        QSKIP("The C++ Code Style page is not available here");

    QQuickItem *edit = nullptr;
    QTRY_VERIFY(edit = findQmlNamed(root, "codeStylePreviewText").value(0));
    auto quickWidget = host->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);

    // The style the preview is measured against is the one the page edits.
    Utils::SelectionAspect *tabPolicy = nullptr;
    Utils::IntegerAspect *indentSize = nullptr;
    for (QQuickItem *delegate : findAspectDelegates(root)) {
        auto aspect = delegate->property("aspect").value<Utils::BaseAspect *>();
        if (!aspect)
            continue;
        if (aspect->qmlName() == "TabPolicy")
            tabPolicy = qobject_cast<Utils::SelectionAspect *>(aspect);
        else if (aspect->qmlName() == "IndentSize")
            indentSize = qobject_cast<Utils::IntegerAspect *>(aspect);
    }
    QVERIFY(tabPolicy);
    QVERIFY(indentSize);

    QString typedByTab;
    const auto typeTab = [&] {
        QMetaObject::invokeMethod(edit, "forceActiveFocus");
        QTRY_VERIFY(edit->hasActiveFocus());
        edit->setProperty("cursorPosition", 0);
        const QString before = edit->property("text").toString();
        // Sent to the QQuickWidget rather than to the scene's own window: the
        // widget focus chain is what takes the key, and delivering straight to
        // the scene steps over the thing being tested. The widget needs the
        // focus for the same reason - a QQuickWidget hands the Tab on from
        // focusNextPrevChild().
        quickWidget->setFocus();
        QTest::keyClick(quickWidget, Qt::Key_Tab);
        const QString after = edit->property("text").toString();
        typedByTab = after.left(after.size() - before.size());
    };

    // Spaces where the style says spaces, as many as it indents by.
    tabPolicy->setValue(0);
    indentSize->setValue(4);
    typeTab();
    QCOMPARE(typedByTab, QString("    "));
    QVERIFY2(edit->hasActiveFocus(), "Tab left the editor");

    // And a tab character where it says tabs. How many of what is
    // TabSettingsData's arithmetic and has its own tests; what matters here is
    // that the style is asked at all.
    tabPolicy->setValue(1);
    typeTab();
    QVERIFY2(typedByTab.contains('\t'), qPrintable("typed " + typedByTab));

    // Shift+Tab takes the indent back.
    tabPolicy->setValue(0);
    typeTab();
    QCOMPARE(typedByTab, QString("    "));
    const QString before = edit->property("text").toString();
    QTest::keyClick(quickWidget, Qt::Key_Backtab);
    QCOMPARE(edit->property("text").toString(), before.mid(typedByTab.size()));
}

void QuickUiTest::testColourOffersToGoBackToItsDefault()
{
    // ColorAspect asks for a reset button by default - it is how a syntax
    // format's colour is unset - and the Quick picker had none at all, so every
    // colour on every ported page had lost it.
    Utils::AspectContainer page;
    Utils::ColorAspect colour(&page);
    colour.setLabelText("Ink");
    colour.setDefaultValue(QColor(Qt::blue));
    colour.setValue(QColor(Qt::red));

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "ColorDelegate"));
    QQuickItem *reset = nullptr;
    QTRY_VERIFY(reset = findQmlNamed(delegate, "colorResetButton").value(0, nullptr));
    QVERIFY(reset->isVisible());

    QMetaObject::invokeMethod(reset, "clicked");
    QCOMPARE(colour.volatileValue(), QColor(Qt::blue));

}

void QuickUiTest::testColourWithNoResetHasNoButton()
{
    // Its own form: two colours in one test would leave which delegate is
    // whose to chance.
    Utils::AspectContainer page;
    Utils::ColorAspect fixed(&page);
    fixed.setLabelText("Ink");
    fixed.setWithResetButton(false);
    QVERIFY(!fixed.presentation().withResetButton);

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "ColorDelegate"));
    const QList<QQuickItem *> buttons = findQmlNamed(delegate, "colorResetButton");
    QCOMPARE(buttons.size(), 1);
    QVERIFY(!buttons.first()->isVisible());
}

void QuickUiTest::testTableCellReadsInItsOwnColours()
{
    // A list of syntax formats is meant to be read in the colours it is
    // describing, so a model that answers Qt::ForegroundRole and
    // Qt::BackgroundRole has to be believed. Neither is in the default
    // roleNames(), so without naming them QML cannot see either.
    Utils::AspectContainer page;
    TestTableAspect table(&page);
    table.setLabelText("Rows");

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();

    QQuickItem *view = nullptr;
    QTRY_VERIFY(view = tableViewOf(quickWidget->rootObject()));

    // The locked column says what it wants; the others say nothing and keep the
    // form's colours.
    QList<QQuickItem *> labels;
    QTRY_COMPARE((labels = findQmlNamed(view, "tableCellLabel")).size(), 2);
    for (QQuickItem *label : labels)
        QCOMPARE(label->property("color").value<QColor>(), QColor(Qt::red));

    const QList<QQuickItem *> fields = findQmlNamed(view, "tableCellField");
    QVERIFY(!fields.isEmpty());
    QVERIFY(fields.first()->property("color").value<QColor>() != QColor(Qt::red));

    // The background is drawn behind the cell rather than tinting its text.
    int painted = 0;
    for (QQuickItem *rect : findQmlComponents(view, "QQuickRectangle")) {
        if (rect->isVisible() && rect->property("color").value<QColor>() == QColor(Qt::yellow))
            ++painted;
    }
    QCOMPARE(painted, 2);
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
