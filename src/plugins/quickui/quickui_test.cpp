// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "quickui_test.h"

#include "quickoutputview.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <utils/completionhistory.h>
#include <utils/datafromprocess.h>

#include <QFontDatabase>
#include <QSettings>
#include <QLocale>
#include <QElapsedTimer>
#include <QClipboard>
#include <utils/temporarydirectory.h>
#include <utils/historycompleter.h>
#include <utils/aspectwidgets.h>
#include <coreplugin/secretaspect.h>

#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>

#include <QDir>

#include <qtcquick/aspectmodels.h>
#include <qtcquick/filebrowser.h>
#include <qtcquick/qtciconprovider.h>
#include <qtcquick/qtcquickengine.h>
#include <qtcquick/aspectcontainermodel.h>
#include <qtcquick/aspectform.h>
#include <qtcquick/namedaspects.h>

#include <utils/algorithm.h>
#include <utils/aspectlist.h>
#include <utils/aspects.h>
#include <qtcquick/outputview.h>
#include <coreplugin/outputview.h>
#include <utils/outputformatter.h>
#include <QAbstractTextDocumentLayout>
#include <utils/stylehelper.h>
#include <QQuickTextDocument>
#include <QTextBlock>
#include <QTextDocument>
#include <utils/environmentmodel.h>
#include <utils/macroexpander.h>
#include <utils/variablechooser.h>
#include <utils/groupedlistaspect.h>
#include <utils/groupedmodel.h>
#include <utils/pathvalidation.h>
#include <utils/summaryaspect.h>
#include <utils/treemodel.h>
#include <utils/widgets.h>
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
#include <QFontMetrics>
#include <QIcon>
#include <QPixmap>
#include <QPromise>
#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QMetaEnum>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QMimeData>
#include <QScopeGuard>
#include <QQuickWidget>
#include <QLineEdit>
#include <QScrollArea>
#include <QQmlProperty>
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

// The same, for a page that draws itself: createAspectForm reads the
// container's own QML rather than listing its aspects generically, which is
// the path a page with setQmlSource() takes in the settings dialog.
static QWidget *showOwnPage(Utils::AspectContainer *page)
{
    QWidget *form = QtcQuick::createAspectForm(page);
    if (!form)
        return nullptr;
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

// Clicking a checkable button. It flips itself and then reports, which is the
// order a real click produces and the order its handler reads: emitting
// clicked() on its own leaves checked as it was, so a handler that passes
// `checked` on is told the opposite of what the click meant.
static void clickCheckableButton(QQuickItem *button)
{
    QVERIFY(button);
    QVERIFY2(button->property("checkable").toBool(),
             "this button does not toggle, so click it directly");
    QVERIFY(QMetaObject::invokeMethod(button, "toggle"));
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
}

// Clicking a menu entry. A checkable one flips before it reports, which is
// the order a real click produces and the order its handler reads. The
// invocations are checked: MenuItem has no trigger() - that is Action - and
// invokeMethod fails silently, so a test that guessed wrong would sit there
// asserting that nothing had changed.
static void triggerMenuItem(QObject *item)
{
    QVERIFY(item);
    if (item->property("checkable").toBool())
        QVERIFY(QMetaObject::invokeMethod(item, "toggle"));
    QVERIFY(QMetaObject::invokeMethod(item, "triggered"));
}

class QuickUiTest final : public QObject
{
    Q_OBJECT

private slots:
    void testARowOfButtonsIsDrawnAsARowOfButtons();
    void testANoteWrittenInMarkdownSaysSo();
    void testAPagesGroupsFillTheWidthTheyAreGiven();
    void testNoButtonOffersAMnemonicItCannotHave();
    void testAFieldSaysWhatBelongsInIt();
    void testAFieldDrawsTheIconItsAspectAsksFor();
    void testEachArrangementNamesTheFileWhereItBelongs();
    void testTheArrangementFollowsTheHostUnlessItIsToldOtherwise();
    void testTheDialogLooksLikeTheOneItReplaces();
    void testTheListingCanBeReadBothWaysRound();
    void testASearchBoxSaysWhatItIsWhenItIsEmpty();
    void testAFormKeepsAnAspectThatIsHiddenForNow();
    void testTheVariablesAMacroExpanderOffersAreAModel();
    void testAFileTheFilterRejectsIsShownAndNotOffered();
    void testEveryEnclosingFolderIsSomewhereToGo();
    void testASearchCanBeUndoneWithoutTheKeyboard();
    void testTheOptionsMenuKeepsSayingWhatIsOn();
    void testChoosingADirectoryStillShowsWhatIsInIt();
    void testSavingAndPickingSeveralFollowTheSameRulesAsTheWidget();
    void testAListShowsTheItemYouSelectedAndRefusesToRemoveTwice();
    void testATableGivesEachColumnTheWidthItsContentsNeed();
    void testANumberOnItsOwnSaysWhatItCounts();
    void testMakeDefaultSaysWhatItWouldDefault();
    void testALabelTooLongForItsColumnSaysItInFull();
    void testAFieldShowsTheStartOfWhatItHolds();
    void testAGroupsContentStartsAtItsTop();
    void testALabelInAContinuationRowStaysWithItsControl();
    void testACompactColourKeepsItsNumbersBehindItsSwatch();
    void testTheFormatListOpensOnAFormat();
    void testTheComponentsNoPageUsesYetStillDraw();
    void testEveryComponentInTheModuleCanBeLoaded();
    void testAspectDrivenPagesRenderWithQuick();
    void testNestedContainerIsAModelGroup();
    void testSelectionWithoutDescribedChoicesIsUnsupported();
    void testNestedContainerRendersAsGroup();
    void testAContainerCanReadAsOneRow();
    void testAnInlineListItemCanReadAsOneRow();
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
    void testACheckBoxLabelCanBeALink();
    void testAFieldCanBeAskedForTheCursorAndForAReCheck();
    void testAnAspectIsToldWhenItIsDrawn();
    void testAspectListLabelArrivingLate();
    void testPageWithoutItsOwnQmlIsDeclined();
    void testAPageCanBeDrawnByQmlThatIsNotBuiltIn();
    void testTheShippedExtensionPagesNameAspectsTheirScriptsCreate();
    void testBuildingAPageIsNotShowingIt();
    void testAFormInAWidgetLayoutAsksForItsContentHeight();
    void testAGroupCanExplainItselfTheWayAQGroupBoxCould();
    void testEveryDelegateShowsTheToolTipItsAspectCarries();
    void testAnEnablerGreysOutWhatItControlsWhileTheFormIsOpen();
    void testAReadOnlyAspectOffersNothingToTypeIn_data();
    void testAReadOnlyAspectOffersNothingToTypeIn();
    void testAPreviewShowsExactlyWhatItsAspectHolds();
    void testAShownPreviewIsColouredAndEditsWithMouseAndKeyboard();
    void testPasswordAspectDoesNotEchoItsValue();
    void testAspectListOffersItsExtraButtons();
    void testAnOrderedListMovesTheCurrentItem();
    void testAnUnorderedListOffersNoMoveButtons();
    void testQmlOnlyContainerStillLaysOutInAWidgetLayout();
    void testAspectVisibilityReachesTheDrawnControl();
    void testAspectQmlNamesAreUsableAndUnique();
    void testActionAspectIsAButtonOnEitherRenderer();
    void testAButtonCanOfferAMenuInsteadOfActing();
    void testAButtonCanActAndStillOffer();
    void testAContainerRefilledAfterTheFormIsBuiltRedrawsIt();
    void testAFieldWithADefaultOffersToGoBackToIt();
    void testASummarySaysWhichCheckFailedAndWhy();
    void testTextDisplayShowsItsMessage();
    void testTextDisplaySaysHowToReadItsMessage();
    void testRadioStyledBoolIsARadioButton();
    void testATriStateSettingCanSayNeither();
    void testSpinBoxDrawsItsPrefixAndSuffix();
    void testSpinBoxShowsTheValueScaledDown();
    void testFileChooserSplitsTheDialogFilter();
    void testPageQmlReachesANestedContainersAspects();
    void testAnAspectCanHandOutAContainerToDraw();
    void testMultiLineStringGetsATextArea();
    void testSecretIsFetchedBeforeItCanBeEdited();
    void testASecretTheAspectAlreadyHoldsNeedsNoKeychain();
    void testAControlOffersTheContextActionItsAspectDescribes();
    void testRecordingAKeySequenceIsTheAspectsToStartAndStop();
    void testTableAspectDrawsWhatItsModelOffers();
    void testATableCellShowsTheIconItsModelGives();
    void testATableWithNoColumnNamesHasNoHeader();
    void testTableAspectAddsAndRemovesRows();
    void testTableAspectRemovesEverySelectedRow();
    void testTheRowTheUserIsOnIsDrawnAsSelected();
    void testTableAspectFiltersItsRows();
    void testSelectingARowTellsThePageWhichOneItIs();
    void testEveryTableOnEveryPageReportsItsCurrentRow();
    void testEveryTreeOnEveryPageReportsItsCurrentItem();
    void testAPageRefusesAnIndexFromSomeoneElsesModel();
    void testEveryGroupedListMapsItsRowsBothWays();
    void testNoPageHasAnApplyThatCannotSaveAnything();
    void testEditingAPageMakesItDirtyAndCancelPutsItBack();
    void testTableCellReadsInItsOwnColours();
    void testATableIsReadOnTheBackgroundItsAspectNames();
    void testTheFormatListIsReadOnTheSchemesOwnBackground();
    void testListRowShowsWhatTheListSaysAboutTheItem();
    void testGroupedListShowsItsGroupsAndActsOnTheCurrentItem();
    void testTreeShowsWhatTheAspectHandsOut();
    void testAColumnMakesRoomForTheNamesInIt();
    void testATreeCellIsWrittenToWhereItsModelSaysSo();
    void testAReorderableTreeMovesARowThroughItsModel();
    void testFieldSaysWhatIsWrongAndKeepsItOut();
    void testFieldWaitsForAnAnswerItHasToFetch();
    void testAFieldCompletesAgainstWhatTheAspectOffers();
    void testAFieldOffersTheVariablesItCanBeWrittenIn();
    void testAVariableBeingDefinedIsNotOfferedForItself();
    void testASeveralLineFieldOffersVariablesToo();
    void testAnEnvironmentEditorDrawsItsTableAndItsButtons();
    void testANestedContainerIsDrawnWithTheQmlItNames();
    void testFormattedOutputCanBeDrawnByQtQuick();
    void testHidingALineIsNotHowAQuickViewFilters();
    void testTheOutputViewShowsWhatAFormatterWrites();
    void testZoomIsPointsAddedWithAFloor();
    void testTheOutputViewZoomsTheTextItDraws();
    void testCtrlWheelZoomsTheOutputViewAndPlainWheelDoesNot();
    void testTheOutputViewSeesTheLinksAFormatterWrote();
    void testCoreHandsOutAnOutputViewItCannotDrawItself();
    void testCoreHasNoOutputViewWithoutAFrontEnd();
    void testAFieldOffersWhatWasTypedIntoItBefore();
    void testBrowsingStartsWhereThePathAlreadyPointsTo();
    void testALabelSaysTheValueItCannotShowInFull();
    void testAPathToACommandSaysWhatVersionItIs();
    void testAColourOffersAnAlphaOnlyWhenItIsAllowed();
    void testAFontPickerOffersOnlyTheFamiliesTheAspectAccepts();
    void testTheFileBrowserListsWhatIsInADirectory();
    void testADirectoryIsListedInAnOrderAReaderExpects();
    void testABigDirectoryIsListedWithoutTheDialogHanging();
    void testTheFileDialogChoosesAFileWithoutAskingThePlatform();
    void testAPathOnADeviceIsBrowsedWithOurOwnDialog();
    void testAPathFieldSaysWhetherWhatItHoldsIsThere();
    void testWhichDialogAFieldOpensIsOneDecision();
    void testTheFileBrowserRemembersWhereItHasBeen();
    void testTheFileBrowserFindsFilesBelowTheDirectory();
    void testTheFileDialogOffersEachKindOfFileSeparately();
    void testTheFileDialogCanChooseSeveralFilesAtOnce();
    void testEachEntrySaysHowBigItIsAndWhatItIs();
    void testAnEntryCanBeRenamedOrBinned();
    void testTheFileDialogGetsAboutByKeyboard();
    void testFilesCanBeCopiedAndPasted();
    void testTheFileDialogShowsIconsAndCanBeAGrid();
    void testALongPasteSaysWhatItIsDoingAndCanBeStopped();
    void testSeveralLinesCompleteTheWordTheCursorIsIn();
    void testColourOffersToGoBackToItsDefault();
    void testColourWithNoResetHasNoButton();
    void testACodeEditorScrollsInsteadOfGrowingThePage();
    void testTabTypesAnIndentInACodeEditor();
    void testEditingThePreviewReachesTheAspectThatOwnsIt();
    void testAGroupPutsItsContentAtItsTop();
    void testAPageSaysAHeadingOnlyOnce();
};

// Settings a page carries but does not draw. Each was checked against the
// closure the page replaced: either the widget layout did not draw it either,
// or it is edited from somewhere else in the UI. Anything not here is a
// regression, so a new entry belongs here only with that check done.
static const QSet<QString> &knownUndrawnSettings()
{
    static const QSet<QString> settings = {
        // Thrown and caught exceptions are turned on from the Breakpoints
        // view, which is where the breakpoints they create are shown.
        "GDB: DebugMode/BreakOnThrow/BreakOnThrow",
        "GDB: DebugMode/BreakOnCatch/BreakOnCatch",

        // Toggled from the Callgrind toolbar, beside the data they change.
        "Valgrind: Analyzer.Valgrind.Callgrind.CycleDetection/CycleDetection",
        "Valgrind: Analyzer.Valgrind.Callgrind.ShortenTemplates/ShortenTemplates",

    };
    return settings;
}

// A delegate's label takes the form's label column so that the labels down a
// page line up. A row that begins with a check box is a continuation of it,
// not a row of the form: "Auto-save modified files  Interval: [5] min" is one
// sentence, and reserving a column inside it put "Interval:" 375 pixels from
// the number it names. The delegates take a compact for it; this is what says
// which rows need one.
void QuickUiTest::testALabelInAContinuationRowStaysWithItsControl()
{
    Core::setAspectFormFactory([](Utils::AspectContainer *container) {
        return QtcQuick::createAspectForm(container);
    });
    const QScopeGuard clearFactory([] { Core::setAspectFormFactory({}); });

    const auto kindOf = [](QQuickItem *item) {
        const QString name = QString::fromLatin1(item->metaObject()->className());
        return name.left(name.indexOf('_'));
    };

    QStringList adrift;
    int labels = 0;
    for (Core::IOptionsPage *page : Core::IOptionsPage::allOptionsPages()) {
        const std::optional<Utils::AspectContainer *> aspects = page->aspects();
        if (!aspects || !*aspects)
            continue;
        std::unique_ptr<Core::IOptionsPageWidget> widget(page->createWidget());
        if (!widget)
            continue;
        auto quickWidget = widget->findChild<QQuickWidget *>();
        if (!quickWidget || !quickWidget->rootObject())
            continue;

        // The rows that begin with a check box, found before the page is
        // shown so that only a page with one pays for being laid out.
        QList<QQuickItem *> rows;
        for (QQuickItem *box : findQmlComponents(quickWidget->rootObject(), "BoolDelegate")) {
            QQuickItem * const row = box->parentItem();
            if (!row || rows.contains(row))
                continue;
            if (kindOf(row) != "QQuickRowLayout" || row->childItems().value(0) != box)
                continue;
            rows << row;
        }
        if (rows.isEmpty())
            continue;

        widget->resize(1100, 620);
        widget->show();
        if (!QTest::qWaitForWindowExposed(widget.get()))
            continue;
        QTRY_VERIFY2(quickWidget->rootObject()->width() >= quickWidget->width() - 1,
                     qPrintable(page->displayName() + " never took the width it was given"));

        for (QQuickItem *row : std::as_const(rows)) {
            for (QQuickItem *item : row->childItems()) {
                if (!item->isVisible() || item->width() <= 0)
                    continue;
                // The label is the delegate's first child. A delegate with
                // no label of its own has nothing to measure.
                const QList<QQuickItem *> parts = item->childItems();
                if (parts.isEmpty())
                    continue;
                QQuickItem * const label = parts.at(0);
                // The style provides Label.qml, so an instance answers
                // "Label", not "QQuickLabel".
                if (!kindOf(label).endsWith("Label") || !label->isVisible()
                    || label->property("text").toString().isEmpty()) {
                    continue;
                }

                // The control it names, which is not simply the next child:
                // a spin box has a prefix label in front of it, and a field
                // sits in a column with the reason it is wrong under it.
                QQuickItem *control = nullptr;
                for (const QString &kind : {QString("SpinBox"),
                                            QString("TextField"),
                                            QString("ComboBox")}) {
                    for (QQuickItem *part : findQmlComponents(item, kind)) {
                        if (part->isVisible()) {
                            control = part;
                            break;
                        }
                    }
                    if (control)
                        break;
                }
                if (!control)
                    continue;
                ++labels;
                const qreal gap = control->mapToItem(item, QPointF(0, 0)).x()
                                  - (label->x() + label->implicitWidth());
                if (gap > 40) {
                    adrift << QString("%1: %2px between \"%3\" and what it names")
                                  .arg(page->displayName())
                                  .arg(gap)
                                  .arg(label->property("text").toString());
                }
            }
        }
        widget->hide();
    }

    QVERIFY2(labels > 0,
             "no page put a labelled control beside a check box, so this proves nothing");
    QVERIFY2(adrift.isEmpty(), qPrintable("\n" + adrift.join("\n")));
}

// What a group holds begins at the top of it. A column that does not fill the
// group is centred in it instead, which put the Python language server's list
// of plugins 115 pixels down an otherwise empty box.
//
// One page rather than a sweep: every page whose group holds a column of its
// own would have to be put on screen to be measured, and showing twenty pages
// costs twenty seconds and makes the keychain too slow for the secret test
// that runs after it.
void QuickUiTest::testAGroupsContentStartsAtItsTop()
{
    Core::setAspectFormFactory([](Utils::AspectContainer *container) {
        return QtcQuick::createAspectForm(container);
    });
    const QScopeGuard clearFactory([] { Core::setAspectFormFactory({}); });

    Core::IOptionsPage *page = nullptr;
    for (Core::IOptionsPage *candidate : Core::IOptionsPage::allOptionsPages()) {
        if (candidate->displayName() == "Language Server Configuration")
            page = candidate;
    }
    if (!page)
        QSKIP("no Language Server Configuration page - is the Python plugin loaded?");

    const std::unique_ptr<QWidget> widget(page->createWidget());
    QVERIFY(widget);
    widget->resize(1100, 620);
    widget->show();
    QVERIFY(QTest::qWaitForWindowExposed(widget.get()));
    auto * const quickWidget = widget->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QTRY_VERIFY(quickWidget->rootObject()
                && quickWidget->rootObject()->width() >= quickWidget->width() - 1);

    QQuickItem * const group
        = findQmlComponents(quickWidget->rootObject(), "AspectGroupBox").value(0, nullptr);
    QVERIFY(group);
    auto * const content = group->property("contentItem").value<QQuickItem *>();
    QVERIFY(content);
    QVERIFY2(content->height() > 100,
             "the group is not tall enough for its content to float in, so this "
             "proves nothing");

    QQuickItem *first = nullptr;
    for (QQuickItem *child : content->childItems()) {
        if (child->isVisible() && child->height() > 0) {
            first = child;
            break;
        }
    }
    QVERIFY(first);
    QVERIFY2(first->y() <= 8,
             qPrintable(QString("what the group holds starts %1px down it")
                            .arg(first->y())));
}

// A field shows the text around its cursor, and text set from an aspect
// leaves the cursor at the end - so a value too long for its field was drawn
// from the tail: the Quick Fixes page offered "ame === name ? ..." where the
// getter name template begins "memberName === name ? ...". The widget line
// edit shows the start of what it holds.
void QuickUiTest::testAFieldShowsTheStartOfWhatItHolds()
{
    Core::setAspectFormFactory([](Utils::AspectContainer *container) {
        return QtcQuick::createAspectForm(container);
    });
    const QScopeGuard clearFactory([] { Core::setAspectFormFactory({}); });

    QStringList fromTheTail;
    int fields = 0;
    for (Core::IOptionsPage *page : Core::IOptionsPage::allOptionsPages()) {
        const std::optional<Utils::AspectContainer *> aspects = page->aspects();
        if (!aspects || !*aspects)
            continue;
        std::unique_ptr<Core::IOptionsPageWidget> widget(page->createWidget());
        if (!widget)
            continue;
        auto quickWidget = widget->findChild<QQuickWidget *>();
        if (!quickWidget || !quickWidget->rootObject())
            continue;

        for (QQuickItem *delegate : findAspectDelegates(quickWidget->rootObject())) {
            // The delegates whose field holds a value to read. A spin box has
            // a field of its own and puts the cursor where it types; a table
            // cell is only a field while it is being edited.
            const QString kind = QString::fromLatin1(delegate->metaObject()->className());
            if (!kind.startsWith("StringDelegate") && !kind.startsWith("StringListDelegate"))
                continue;
            QQuickItem * const field = findQmlComponents(delegate, "TextField").value(0, nullptr);
            if (!field)
                continue;
            const QString text = field->property("text").toString();
            if (text.isEmpty() || field->hasActiveFocus())
                continue;
            ++fields;
            if (field->property("cursorPosition").toInt() != 0) {
                fromTheTail << QString("%1: \"%2\" is shown from character %3")
                                   .arg(page->displayName(), text.left(30))
                                   .arg(field->property("cursorPosition").toInt());
            }
        }
    }

    QVERIFY2(fields > 0, "no page drew a field holding anything, so this proves nothing");
    QVERIFY2(fromTheTail.isEmpty(), qPrintable("\n" + fromTheTail.join("\n")));
}

// The form's label column is a fixed width so that the labels down a page
// line up, so a label longer than it is cut off - the widget form grew its
// column to the widest label instead. The QML Profiler page offers "Report
// items built in a handler ab...". What was cut off is what the tooltip says.
void QuickUiTest::testALabelTooLongForItsColumnSaysItInFull()
{
    Utils::AspectContainer page;
    Utils::IntegerAspect wordy(&page);
    wordy.setLabelText("Report items built in a handler above this many milliseconds:");
    wordy.setRange(0, 1000);

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);

    QQuickItem *label = nullptr;
    QTRY_VERIFY(label = findQmlComponents(quickWidget->rootObject(), "FormLabel")
                            .value(0, nullptr));
    QCOMPARE(label->property("text").toString(), wordy.labelText());

    // Otherwise there is nothing for the tooltip to say and this proves
    // nothing: the column has to be too narrow for the label.
    QVERIFY2(label->property("truncated").toBool(),
             "the label fits in its column, so it is not being cut off");
    QCOMPARE(QQmlProperty(label, "ToolTip.text", qmlContext(label)).read().toString(),
             wordy.labelText());
}

// "Make Default" is the same button on the Kits, Compilers, Debuggers and
// Devices pages, so only the page knows what it would be defaulting. The Kits
// page said so in a tooltip its widget set on the button, and a tooltip set
// on a widget goes away with the widget.
void QuickUiTest::testMakeDefaultSaysWhatItWouldDefault()
{
    Core::setAspectFormFactory([](Utils::AspectContainer *container) {
        return QtcQuick::createAspectForm(container);
    });
    const QScopeGuard clearFactory([] { Core::setAspectFormFactory({}); });

    Core::IOptionsPage *page = nullptr;
    for (Core::IOptionsPage *candidate : Core::IOptionsPage::allOptionsPages()) {
        if (candidate->displayName() == "Kits" && candidate->aspects() && *candidate->aspects())
            page = candidate;
    }
    if (!page)
        QSKIP("no Kits page - is ProjectExplorer loaded?");

    const std::unique_ptr<Core::IOptionsPageWidget> widget(page->createWidget());
    QVERIFY(widget);
    auto quickWidget = widget->findChild<QQuickWidget *>();
    QVERIFY(quickWidget && quickWidget->rootObject());

    QQuickItem *button = nullptr;
    QTRY_VERIFY(button = findQmlNamed(quickWidget->rootObject(),
                                      "groupedListMakeDefaultButton")
                             .value(0, nullptr));
    const QString tip
        = QQmlProperty(button, "ToolTip.text", qmlContext(button)).read().toString();
    QVERIFY2(tip.contains("kit"),
             qPrintable("the button says \"" + tip + "\" rather than what it defaults"));
}

// A number that begins its row has to say what it counts. The Compile Output
// page drew a bare spin box holding 10000000: the words around it were in the
// layout, which split "Limit output to %1 characters" and put the box between
// the halves. A number that follows something - a check box, a label of its
// own - is named by that instead.
void QuickUiTest::testANumberOnItsOwnSaysWhatItCounts()
{
    Core::setAspectFormFactory([](Utils::AspectContainer *container) {
        return QtcQuick::createAspectForm(container);
    });
    const QScopeGuard clearFactory([] { Core::setAspectFormFactory({}); });

    QStringList bare;
    int numbers = 0;
    for (Core::IOptionsPage *page : Core::IOptionsPage::allOptionsPages()) {
        const std::optional<Utils::AspectContainer *> aspects = page->aspects();
        if (!aspects || !*aspects)
            continue;
        std::unique_ptr<Core::IOptionsPageWidget> widget(page->createWidget());
        if (!widget)
            continue;
        auto quickWidget = widget->findChild<QQuickWidget *>();
        if (!quickWidget || !quickWidget->rootObject())
            continue;

        for (QQuickItem *delegate : findAspectDelegates(quickWidget->rootObject())) {
            const QString kind = QString::fromLatin1(delegate->metaObject()->className());
            if (!kind.startsWith("IntegerDelegate") && !kind.startsWith("DoubleDelegate"))
                continue;
            // Only where it opens its row: anything in front of it names it.
            // In a column every child opens one; in a row only the first.
            QQuickItem * const row = delegate->parentItem();
            if (!row)
                continue;
            static const QStringList sideBySide{"QQuickRowLayout",
                                                "QQuickRow",
                                                "QQuickFlow",
                                                "QQuickGridLayout"};
            if (sideBySide.contains(QString::fromLatin1(row->metaObject()->className()))
                && row->childItems().value(0) != delegate) {
                continue;
            }
            auto *aspect = delegate->property("aspect").value<Utils::BaseAspect *>();
            if (!aspect)
                continue;
            ++numbers;
            const Utils::AspectPresentation p = aspect->presentation();
            if (aspect->plainLabelText().isEmpty() && p.prefix.isEmpty() && p.suffix.isEmpty())
                bare << page->displayName() + "/" + aspect->qmlName();
        }
    }

    QVERIFY2(numbers > 0, "no page opened a row with a number, so this proves nothing");
    QVERIFY2(bare.isEmpty(),
             qPrintable("numbers with nothing to say what they count: " + bare.join(", ")));
}

// The dialog draws one of two arrangements, which is what the widget dialog
// has always done. Classic names the file below the listing whatever the
// dialog is for and offers the kinds beside it; the other only names it when
// there is a name to give, above the listing, the way a Mac dialog does.
void QuickUiTest::testEachArrangementNamesTheFileWhereItBelongs()
{
    const auto dialogFor = [](bool classic, int mode, const QString &filter) -> QObject * {
        QQmlComponent component(QtcQuick::engine(),
                                QUrl("qrc:/qt/qml/QtCreator/Ui/QtcFileDialog.qml"));
        QTC_ASSERT(!component.isError(), qDebug() << component.errorString(); return nullptr);
        // Set here rather than after: the arrangement decides what is built.
        return component.createWithInitialProperties(
            {{"classic", classic}, {"mode", mode}, {"nameFilter", filter}});
    };
    // OpenFile and SaveFile, as QtcFileDialog numbers them.
    const int openFile = 0;
    const int saveFile = 2;
    // The dialog is a Window, so what it draws is found by name rather than
    // walked from a root item.
    const auto shows = [](QObject *dialog, const QString &name) {
        QQuickItem * const item = dialog->findChild<QQuickItem *>(name);
        return item && item->isVisible();
    };

    // Classic, opening: the file is named below the listing, and the kinds
    // are on offer even though the caller named none.
    {
        const std::unique_ptr<QObject> dialog(dialogFor(true, openFile, ""));
        QVERIFY(dialog);
        QVERIFY2(shows(dialog.get(), "nameField"), "classic does not name the file");
        QVERIFY2(!shows(dialog.get(), "saveAsRow"), "classic names it twice");
        QVERIFY2(shows(dialog.get(), "filterBox"), "classic offers no kind at all");
        const QVariantList groups = dialog->property("filterGroups").toList();
        QCOMPARE(groups.size(), 1);
        QCOMPARE(groups.first().toMap().value("patterns").toStringList(), QStringList({"*"}));
    }

    // The other, opening: the listing is the answer, so there is no field and
    // no choice of kind worth making.
    {
        const std::unique_ptr<QObject> dialog(dialogFor(false, openFile, ""));
        QVERIFY(dialog);
        QVERIFY2(!shows(dialog.get(), "nameField"), "the compact arrangement names the file anyway");
        QVERIFY2(!shows(dialog.get(), "saveAsRow"), "it asks for a name to open a file");
        QVERIFY2(!shows(dialog.get(), "filterBox"), "it offers a choice of one kind");
    }

    // The other, saving: named above the listing.
    {
        const std::unique_ptr<QObject> dialog(dialogFor(false, saveFile, ""));
        QVERIFY(dialog);
        QVERIFY2(shows(dialog.get(), "saveAsRow"), "saving asks for no name");
        QVERIFY2(!shows(dialog.get(), "nameField"), "it names the file twice");

        // And what is typed there is the answer, wherever the field is.
        dialog->setProperty("currentFolder", QDir::tempPath());
        QQuickItem * const field = dialog->findChild<QQuickItem *>("saveAsField");
        QVERIFY(field);
        field->setProperty("text", "notes.txt");
        QCOMPARE(dialog->property("typedName").toString(), QString("notes.txt"));
        QCOMPARE(dialog->property("wouldChooseAll").toStringList().size(), 1);
    }

    // Classic, saving: the one field asks, worded for saving, and the row the
    // other arrangement puts above the listing stays away. This is the only
    // combination where the two disagree about which field is which.
    {
        const std::unique_ptr<QObject> dialog(dialogFor(true, saveFile, ""));
        QVERIFY(dialog);
        QVERIFY2(shows(dialog.get(), "nameField"), "classic saving asks for no name");
        QVERIFY2(!shows(dialog.get(), "saveAsRow"), "classic names it twice when saving");
        QQuickItem * const label = dialog->findChild<QQuickItem *>("nameLabel");
        QVERIFY(label);
        QCOMPARE(label->property("text").toString(), QString("Save As:"));

        dialog->setProperty("currentFolder", QDir::tempPath());
        QQuickItem * const field = dialog->findChild<QQuickItem *>("nameField");
        QVERIFY(field);
        field->setProperty("text", "notes.txt");
        QCOMPARE(dialog->property("typedName").toString(), QString("notes.txt"));
        QCOMPARE(dialog->property("wouldChooseAll").toStringList().size(), 1);
    }

    // A choice worth making is offered by both.
    {
        const std::unique_ptr<QObject> dialog(
            dialogFor(false, openFile, "Sources (*.cpp);;All files (*)"));
        QVERIFY(dialog);
        QVERIFY2(shows(dialog.get(), "filterBox"), "two kinds and no way to pick one");
    }
}

// Which arrangement a dialog opens with follows the host - every dialog but a
// Mac one names the file below the listing - and the reader's choice, which
// the browser keeps where the widget dialog keeps it.
void QuickUiTest::testTheArrangementFollowsTheHostUnlessItIsToldOtherwise()
{
    QtcQuick::FileBrowser browser;
    QCOMPARE(browser.classicLayout(), !Utils::HostOsInfo::isMacHost());

    QQmlComponent component(QtcQuick::engine(),
                            QUrl("qrc:/qt/qml/QtCreator/Ui/QtcFileDialog.qml"));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    const std::unique_ptr<QObject> dialog(component.create());
    QVERIFY(dialog);

    // The dialog draws what the browser holds, without being told.
    auto * const its = dialog->findChild<QtcQuick::FileBrowser *>("fileBrowser");
    QVERIFY(its);
    QCOMPARE(dialog->property("classic").toBool(), its->classicLayout());

    // The menu says which one it is drawing. Triggering it would write the
    // reader's settings, which a test has no business doing, so this asks
    // what it offers rather than taking it up.
    QObject * const item = dialog->findChild<QObject *>("classicLayoutItem");
    QVERIFY(item);
    QCOMPARE(item->property("checked").toBool(), its->classicLayout());
}

// A string aspect can ask for an icon inside its field, at the right, and be
// told when it is pressed. The widget line edit has drawn it all along, and
// the settings API a Lua extension uses offers it - so a page that asked for
// one got nothing once it was drawn with Qt Quick.
void QuickUiTest::testAFieldDrawsTheIconItsAspectAsksFor()
{
    Utils::AspectContainer page;
    Utils::StringAspect withIcon(&page);
    withIcon.setLabelText("Token:");
    withIcon.setDisplayStyle(Utils::StringAspect::LineEditDisplay);
    withIcon.setRightSideIconPath(Utils::FilePath::fromString(":/utils/images/eye_open.png"));

    int pressed = 0;
    withIcon.addOnRightSideIconClicked(&page, [&pressed] { ++pressed; });

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);

    QQuickItem *icon = nullptr;
    QTRY_VERIFY(icon = findQmlNamed(quickWidget->rootObject(), "rightSideIcon")
                           .value(0, nullptr));
    QVERIFY2(icon->isVisible(), "the field draws no icon for an aspect that asks for one");

    // Pressed: the aspect is told, and what that means is the aspect's own
    // business - the widget line edit connects its button to the same signal.
    QMetaObject::invokeMethod(&withIcon, "clickRightSideIcon");
    QCOMPARE(pressed, 1);
}

// An aspect that says what its field is for says it through the presentation,
// and the widget line edit has shown it all along. A Qt Quick field that does
// not bind it is an empty box: the Display page drew one where the wrapped
// line marker goes, with nothing to say what it was.
void QuickUiTest::testAFieldSaysWhatBelongsInIt()
{
    Core::setAspectFormFactory([](Utils::AspectContainer *container) {
        return QtcQuick::createAspectForm(container);
    });
    const QScopeGuard clearFactory([] { Core::setAspectFormFactory({}); });

    QStringList silent;
    int fields = 0;
    for (Core::IOptionsPage *page : Core::IOptionsPage::allOptionsPages()) {
        const std::optional<Utils::AspectContainer *> aspects = page->aspects();
        if (!aspects || !*aspects)
            continue;
        std::unique_ptr<Core::IOptionsPageWidget> widget(page->createWidget());
        if (!widget)
            continue;
        auto quickWidget = widget->findChild<QQuickWidget *>();
        if (!quickWidget || !quickWidget->rootObject())
            continue;

        for (QQuickItem *delegate : findAspectDelegates(quickWidget->rootObject())) {
            auto *aspect = delegate->property("aspect").value<Utils::BaseAspect *>();
            if (!aspect || aspect->placeholderText().isEmpty())
                continue;
            // The delegates that draw one: a check box has no room for a hint.
            QQuickItem * const field = findQmlComponents(delegate, "TextField").value(0, nullptr);
            if (!field)
                continue;
            ++fields;
            if (field->property("placeholderText").toString() != aspect->placeholderText()) {
                silent << QString("%1/%2 wants \"%3\"")
                              .arg(page->displayName(), aspect->qmlName(),
                                   aspect->placeholderText());
            }
        }
    }

    QVERIFY2(fields > 0, "no page drew a field with something to say, so this proves nothing");
    QVERIFY2(silent.isEmpty(), qPrintable("\n" + silent.join("\n")));
}

// Button texts are written for widgets, where "&Add" underlines the A and
// Alt+A presses it. Qt Quick has no mnemonics, so an unstripped marker is
// simply drawn: the Interpreters page offered "&Add" and "&Make Default".
void QuickUiTest::testNoButtonOffersAMnemonicItCannotHave()
{
    Core::setAspectFormFactory([](Utils::AspectContainer *container) {
        return QtcQuick::createAspectForm(container);
    });
    const QScopeGuard clearFactory([] { Core::setAspectFormFactory({}); });

    // An ampersand in front of a letter. A literal one - written "&&" for a
    // widget - comes through as a single ampersand, and those stand beside a
    // space: "Font && Colors" is read as "Font & Colors".
    static const QRegularExpression marker("&[A-Za-z]");

    QStringList marked;
    int buttons = 0;
    for (Core::IOptionsPage *page : Core::IOptionsPage::allOptionsPages()) {
        const std::optional<Utils::AspectContainer *> aspects = page->aspects();
        if (!aspects || !*aspects)
            continue;
        std::unique_ptr<Core::IOptionsPageWidget> widget(page->createWidget());
        if (!widget)
            continue;
        auto quickWidget = widget->findChild<QQuickWidget *>();
        if (!quickWidget || !quickWidget->rootObject())
            continue;

        for (QQuickItem *button : findQmlComponents(quickWidget->rootObject(), "Button")) {
            const QString text = button->property("text").toString();
            if (text.isEmpty())
                continue;
            ++buttons;
            if (marker.match(text).hasMatch())
                marked << page->displayName() + ": \"" + text + "\"";
        }
    }

    QVERIFY2(buttons > 0, "no page drew a button with a text, so this proves nothing");
    QVERIFY2(marked.isEmpty(), qPrintable("\n" + marked.join("\n")));
}

// A group on a page has to be an AspectGroupBox. Qt Quick's own GroupBox puts
// what a page writes inside its contentItem, where a ColumnLayout keeps its
// implicit width - so the Docker page drew its path field as wide as the path
// in it rather than as wide as the form. AspectGroupBox *is* that layout, so
// what a page puts in it fills.
void QuickUiTest::testAPagesGroupsFillTheWidthTheyAreGiven()
{
    Core::setAspectFormFactory([](Utils::AspectContainer *container) {
        return QtcQuick::createAspectForm(container);
    });
    const QScopeGuard clearFactory([] { Core::setAspectFormFactory({}); });

    int groups = 0;
    QStringList bare;
    QStringList unfilled;
    for (Core::IOptionsPage *page : Core::IOptionsPage::allOptionsPages()) {
        const std::optional<Utils::AspectContainer *> aspects = page->aspects();
        if (!aspects || !*aspects)
            continue;
        std::unique_ptr<Core::IOptionsPageWidget> widget(page->createWidget());
        if (!widget)
            continue;
        auto quickWidget = widget->findChild<QQuickWidget *>();
        if (!quickWidget || !quickWidget->rootObject())
            continue;

        // findQmlComponents matches by prefix, and an AspectGroupBox answers
        // its own name - so this finds the plain ones only.
        if (!findQmlComponents(quickWidget->rootObject(), "GroupBox").isEmpty())
            bare << page->displayName();

        // And the same thing one level in: a group whose content item is a
        // plain Item holds its layout *inside* that item, where nothing tells
        // the layout to fill - which is the whole defect. AspectGroupBox and
        // GroupDelegate make the column their content item.
        for (const QString &kind : {QString("AspectGroupBox"), QString("GroupDelegate")}) {
            for (QQuickItem *group : findQmlComponents(quickWidget->rootObject(), kind)) {
                auto *content = group->property("contentItem").value<QQuickItem *>();
                if (!content)
                    continue;
                ++groups;
                if (!QString::fromLatin1(content->metaObject()->className()).endsWith("Layout")) {
                    unfilled << QString("%1: a %2 holds its content in a %3")
                                    .arg(page->displayName(), kind,
                                         QString::fromLatin1(
                                             content->metaObject()->className()));
                }
            }
        }
    }

    QVERIFY2(groups > 0, "no page drew a group at all, so this proves nothing");
    QVERIFY2(bare.isEmpty(),
             qPrintable("pages with a plain GroupBox rather than an AspectGroupBox: "
                        + bare.join(", ")));
    QVERIFY2(unfilled.isEmpty(), qPrintable("\n" + unfilled.join("\n")));
}

// A note written in markdown has to say so. Neither renderer guesses it -
// Qt::AutoText looks for HTML - so a note that does not is printed with its
// brackets and its links are dead. Copilot's told the user to read
// "[README.md](https://github.com/github/copilot.vim)".
void QuickUiTest::testANoteWrittenInMarkdownSaysSo()
{
    // A link with a scheme in it, which is the part that gives the intent
    // away: square brackets alone are ordinary prose.
    static const QRegularExpression markdownLink(R"(\[[^\]\n]+\]\(\w+:)");

    QStringList raw;
    int notes = 0;
    for (Core::IOptionsPage *page : Core::IOptionsPage::allOptionsPages()) {
        const std::optional<Utils::AspectContainer *> aspects = page->aspects();
        if (!aspects || !*aspects)
            continue;
        (*aspects)->forEachAspect([&](Utils::BaseAspect *aspect) {
            auto note = qobject_cast<Utils::TextDisplay *>(aspect);
            if (!note)
                return;
            ++notes;
            if (!markdownLink.match(note->text()).hasMatch())
                return;
            if (note->presentation().textFormat != Utils::AspectControls::TextFormat::MarkdownText)
                raw << page->displayName() + "/" + aspect->qmlName();
        });
    }

    QVERIFY2(notes > 0, "no page showed a note at all, so this proves nothing");
    QVERIFY2(raw.isEmpty(), qPrintable("markdown drawn as plain text: " + raw.join(", ")));
}

// Controls a page puts beside each other, on the pages themselves. A delegate
// that fills the row it is in draws its control at its own left edge and
// leaves the rest of its share empty, so what follows it starts halfway across
// the row: Terminal's Load Theme and Reset Theme were 438 pixels apart, and
// the Code Model page's "Do not index files greater than" and its number 800.
void QuickUiTest::testARowOfButtonsIsDrawnAsARowOfButtons()
{
    Core::setAspectFormFactory([](Utils::AspectContainer *container) {
        return QtcQuick::createAspectForm(container);
    });
    const QScopeGuard clearFactory([] { Core::setAspectFormFactory({}); });

    // Wider than the spacing a page asks for, so that a row which merely uses
    // a generous gap is not called a defect.
    const int gapAllowed = 40;

    // Delegates that are one control and nothing else: what they draw is their
    // implicit width, whatever width the row hands them.
    const QStringList controls{"ButtonDelegate", "BoolDelegate"};

    const auto kindOf = [](QQuickItem *item) {
        const QString name = QString::fromLatin1(item->metaObject()->className());
        return name.left(name.indexOf('_'));
    };

    QStringList spread;
    int rowsChecked = 0;
    for (Core::IOptionsPage *page : Core::IOptionsPage::allOptionsPages()) {
        const std::optional<Utils::AspectContainer *> aspects = page->aspects();
        if (!aspects || !*aspects)
            continue;
        std::unique_ptr<Core::IOptionsPageWidget> widget(page->createWidget());
        if (!widget)
            continue;
        auto quickWidget = widget->findChild<QQuickWidget *>();
        if (!quickWidget || !quickWidget->rootObject())
            continue;

        // The rows worth looking at, found before the page is shown so that
        // only a page with something to check pays for being laid out - and
        // showing a page is not free: it is what makes the aspects on it go
        // and ask the world about themselves.
        // Their C++ names: a layout is not a QML-defined type, so kindOf()
        // answers what Qt calls it rather than what a page writes.
        static const QStringList sideBySide{"QQuickRowLayout",
                                            "QQuickRow",
                                            "QQuickFlow",
                                            "QQuickGridLayout"};
        QList<QQuickItem *> rows;
        for (const QString &control : controls) {
            for (QQuickItem *item :
                 findQmlComponents(quickWidget->rootObject(), control)) {
                QQuickItem * const row = item->parentItem();
                if (row && !rows.contains(row) && row->childItems().size() > 1
                    && sideBySide.contains(kindOf(row))) {
                    rows << row;
                }
            }
        }
        if (rows.isEmpty())
            continue;

        widget->resize(1100, 620);
        widget->show();
        if (!QTest::qWaitForWindowExposed(widget.get()))
            continue;
        // Exposed is not laid out. Until the scene has taken the width of the
        // widget every row is still at its implicit width, and nothing has had
        // room to spread.
        QTRY_VERIFY2(quickWidget->rootObject()->width() >= quickWidget->width() - 1,
                     qPrintable(page->displayName() + " never took the width it was given"));

        for (QQuickItem *row : std::as_const(rows)) {
            QList<QQuickItem *> drawn;
            for (QQuickItem *item : row->childItems()) {
                if (item->isVisible() && item->width() > 0)
                    drawn << item;
            }
            std::sort(drawn.begin(), drawn.end(), [](QQuickItem *a, QQuickItem *b) {
                return a->x() < b->x();
            });
            for (int i = 1; i < drawn.size(); ++i) {
                QQuickItem * const control = drawn.at(i - 1);
                QQuickItem * const next = drawn.at(i);
                if (!controls.contains(kindOf(control)))
                    continue;
                // Beside it, not under: a column of controls is a perfectly
                // good way to draw them and says nothing about this.
                if (!qFuzzyCompare(next->y() + 1, control->y() + 1))
                    continue;
                ++rowsChecked;
                // What the control draws is its implicit width; the rest of
                // what it was given is empty.
                const qreal gap = next->x() - (control->x() + control->implicitWidth());
                if (gap > gapAllowed) {
                    spread << QString("%1: %2px after a %3")
                                  .arg(page->displayName())
                                  .arg(gap)
                                  .arg(kindOf(control));
                }
            }
        }
        widget->hide();
    }

    QVERIFY2(rowsChecked > 0,
             "no page drew two controls beside each other, so this proves nothing");
    QVERIFY2(spread.isEmpty(), qPrintable("\n" + spread.join("\n")));
}

// Sixteen palette colours do not fit beside sixteen sets of numbers, so a
// compact one keeps the numbers behind its swatch - and has to still be as
// editable as any other colour.
void QuickUiTest::testACompactColourKeepsItsNumbersBehindItsSwatch()
{
    Utils::AspectContainer page;
    Utils::ColorAspect colour(&page);
    colour.setValue(QColor(Qt::red));
    // A palette colour is put back by loading a theme, not one button at a
    // time; a Reset beside every swatch is the thing that does not fit.
    colour.setWithResetButton(false);

    QQmlComponent component(QtcQuick::engine());
    component.setData(R"(
        import QtQuick
        import QtCreator.Ui

        Item {
            id: form

            required property var colour

            width: 800
            height: 80

            ColorDelegate {
                objectName: "swatchOnly"
                aspect: form.colour
                compact: true
                width: parent.width
            }
        }
    )",
                      QUrl("qrc:/qt/qml/QtCreator/Ui/inline.qml"));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    const std::unique_ptr<QObject> object(
        component.createWithInitialProperties({{"colour", QVariant::fromValue(&colour)}}));
    QVERIFY2(object.get(), qPrintable(component.errorString()));
    auto *form = qobject_cast<QQuickItem *>(object.get());
    QVERIFY(form);

    QQuickItem *delegate = findQmlNamed(form, "swatchOnly").value(0, nullptr);
    QVERIFY(delegate);
    QQuickItem *swatch = findQmlNamed(delegate, "colorSwatch").value(0, nullptr);
    QVERIFY(swatch);
    QTRY_VERIFY(swatch->width() > 0);

    int visible = 0;
    for (QQuickItem *part : findQmlComponents(delegate, "SpinBox")) {
        if (part->isVisible())
            ++visible;
    }
    QCOMPARE(visible, 0);

    // The swatch and nothing else: what the delegate asks the row for has to
    // be a swatch's worth of width, or eight to a row is still too many.
    QVERIFY2(delegate->implicitWidth() <= swatch->width() + 24,
             qPrintable(QString("a compact colour asks for %1px, its swatch is %2px")
                            .arg(delegate->implicitWidth())
                            .arg(swatch->width())));

    // Behind the swatch, not gone: the picker holds the same four numbers and
    // they write to the aspect. Read off the popup rather than opened - a
    // popup needs a window to open into, and this form has none.
    QObject *picker = delegate->findChild<QObject *>("colorPicker");
    QVERIFY(picker);
    auto *numbers = picker->property("contentItem").value<QQuickItem *>();
    QVERIFY(numbers);
    const QList<QQuickItem *> boxes = findQmlComponents(numbers, "SpinBox");
    QCOMPARE(boxes.size(), 4);

    boxes.first()->setProperty("value", 0);
    QMetaObject::invokeMethod(boxes.first(), "valueModified");
    QCOMPARE(colour.volatileValue().red(), 0);
}

// Three of the module's components have no call site in any QML: QtcBadge,
// QtcPageIndicator and QtcProgressBar are the Qt Quick counterparts of widgets
// that Utils already has, drawn for pages that have not been written yet.
// That is what QtcLabel was when it broke - a component nothing instantiated,
// which compiled for years while being unusable. Compiling is what
// testEveryComponentInTheModuleCanBeLoaded checks; this asks whether they draw.
void QuickUiTest::testTheComponentsNoPageUsesYetStillDraw()
{
    struct Case
    {
        QString component;
        QVariantMap properties;
        QString mustShow;
    };
    const QList<Case> cases = {
        {"QtcBadge", {{"text", "7"}}, "7"},
        {"QtcPageIndicator", {{"pagesCount", 4}, {"currentPage", 2}}, {}},
        {"QtcProgressBar", {{"from", 0}, {"to", 10}, {"value", 5}}, {}},
    };

    for (const Case &c : cases) {
        QQmlComponent component(QtcQuick::engine(),
                                QUrl("qrc:/qt/qml/QtCreator/Ui/" + c.component + ".qml"));
        QVERIFY2(!component.isError(), qPrintable(c.component + ": " + component.errorString()));
        const std::unique_ptr<QObject> object(component.createWithInitialProperties(c.properties));
        QVERIFY2(object.get(), qPrintable(c.component + ": " + component.errorString()));
        auto *item = qobject_cast<QQuickItem *>(object.get());
        QVERIFY2(item, qPrintable(c.component + " is not an item"));

        // Something to see: a component whose implicit size is zero draws
        // nothing wherever it is put, which is how one rots unnoticed.
        QVERIFY2(item->implicitWidth() > 0,
                 qPrintable(QString("%1 asks for no width").arg(c.component)));
        QVERIFY2(item->implicitHeight() > 0,
                 qPrintable(QString("%1 asks for no height").arg(c.component)));

        if (c.mustShow.isEmpty())
            continue;
        QStringList drawn;
        for (QQuickItem *label : findQmlComponents(item, "Label"))
            drawn << label->property("text").toString();
        for (QQuickItem *text : findQmlComponents(item, "QQuickText"))
            drawn << text->property("text").toString();
        QVERIFY2(drawn.contains(c.mustShow),
                 qPrintable(QString("%1 does not show \"%2\": %3")
                                .arg(c.component, c.mustShow, drawn.join(", "))));
    }
}

// Every .qml in QtCreator.Ui has to at least compile. A component with no call
// site anywhere is untested by construction, and QtcLabel was exactly that: it
// assigned implicitHeight on a Text, which makes the whole type unavailable,
// and nothing noticed until the first thing tried to use it years later.
void QuickUiTest::testEveryComponentInTheModuleCanBeLoaded()
{
    const QDir module(":/qt/qml/QtCreator/Ui");
    const QStringList files = module.entryList({"*.qml"}, QDir::Files);
    QVERIFY2(!files.isEmpty(), "the module's QML is not where this looked, so this checks nothing");

    QStringList broken;
    for (const QString &file : files) {
        QQmlComponent component(QtcQuick::engine(), QUrl("qrc:/qt/qml/QtCreator/Ui/" + file));
        // Ready or Error: a component that needs required properties still
        // *compiles*, and compiling is all this asks.
        if (component.isError())
            broken << file + ": " + component.errorString().trimmed();
    }
    QVERIFY2(broken.isEmpty(), qPrintable("\n" + broken.join("\n")));
}

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
    QStringList undrawn;

    // A QML warning is not a failure anywhere: the engine reports it and
    // carries on, so a broken binding shows up as a page that looks nearly
    // right - a delegate that draws nothing, a property left at its default.
    // Collected here because this is where every page is built. Measured, not
    // assumed: a binding error is reported by QQmlEngine when the binding is
    // *evaluated*, and a component that comes back out of the cache evaluates
    // its bindings again - so a page some earlier test already built is still
    // checked here, and this collection does not depend on the order the slots
    // happen to be declared in. Clearing the component cache first changes
    // nothing, which is why it is not done.
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

    // Pages that hold no aspects at all build their own widgets, so there is
    // nothing here to draw with Qt Quick until they are aspects first. What
    // is left of the migration is that list, and it is worth reporting rather
    // than passing over in silence.
    QStringList notAspectDriven;

    for (Core::IOptionsPage *page : Core::IOptionsPage::allOptionsPages()) {
        const std::optional<Utils::AspectContainer *> aspects = page->aspects();
        if (!aspects || !*aspects) {
            notAspectDriven << page->displayName();
            continue;
        }
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
            // A page legitimately leaves out settings that are stored but
            // edited elsewhere, so the known ones are listed rather than
            // asserted away wholesale. Anything else is a page that lost a
            // setting on its way to Qt Quick, which is the whole failure mode
            // this census exists to catch.
            for (const QString &entry : std::as_const(missing)) {
                const QString line = page->displayName() + ": " + entry;
                if (!knownUndrawnSettings().contains(line))
                    undrawn << line;
            }
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
                    {"FlattenedGroupDelegate", {Kind::FlattenedGroup}},
                    {"BoolWithOwnLabelDelegate", {Kind::BoolWithOwnLabel}},
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

    notAspectDriven.sort();
    qInfo().noquote() << "pages that hold no aspects:" << notAspectDriven.size()
                      << "\n  " << notAspectDriven.join(", ");

    QVERIFY(aspectDriven > 0);
    QCOMPARE(renderedWithQuick, withQml);

    // Nothing on a rendered page may be a control no renderer knows.
    QVERIFY2(unsupported.isEmpty(), qPrintable("no renderer draws these: "
                                               + unsupported.join(", ")));
    QVERIFY2(undrawn.isEmpty(),
             qPrintable("these pages lost a setting on the way to Qt Quick: "
                        + undrawn.join(", ")
                        + ". If the widget layout did not draw it either, or it is "
                          "edited elsewhere, say so in knownUndrawnSettings()."));

    // Every aspect-driven page names a form, so a declined one is a page that
    // went back to widgets rather than one waiting its turn. Nothing else here
    // says so: a page without QML and no QQuickWidget agrees with itself.
    //
    // No exemptions. C++'s page used to be one, because ClangFormat replaced
    // the C++ factory with a widget editor and named no form; it names one now,
    // so the page it produces has to answer for itself like the rest.
    QVERIFY2(declined.isEmpty(),
             qPrintable("pages back on widgets: " + declined.join(", ")));

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

// Builds a page of `count` string delegates and returns the form's height hint.
static int formHeightHintFor(int count, QString *error)
{
    Utils::AspectContainer page;
    std::vector<std::unique_ptr<Utils::StringAspect>> aspects;
    QString body;
    for (int i = 0; i < count; ++i) {
        auto aspect = std::make_unique<Utils::StringAspect>(&page);
        aspect->setSettingsKey(Utils::Key("Field") + QByteArray::number(i));
        aspect->setLabelText(QString("Field %1:").arg(i));
        body += QString("    StringDelegate { aspect: root.aspects.Field%1 }\n").arg(i);
        aspects.push_back(std::move(aspect));
    }

    QTemporaryDir dir;
    if (!dir.isValid()) {
        *error = "no temporary directory";
        return -1;
    }
    const QString pageQml = dir.filePath("SizePage.qml");
    {
        QFile file(pageQml);
        if (!file.open(QIODevice::WriteOnly)) {
            *error = "could not write the page";
            return -1;
        }
        file.write(QString("import QtQuick\nimport QtCreator.Ui\n\n"
                           "AspectPage {\n    id: root\n%1}\n").arg(body).toUtf8());
    }
    page.setQmlSource(QUrl::fromLocalFile(pageQml));

    const std::unique_ptr<QWidget> form(QtcQuick::createAspectForm(&page));
    if (!form) {
        *error = "no form";
        return -1;
    }
    auto quickWidget = form->findChild<QQuickWidget *>();
    if (!quickWidget || !quickWidget->rootObject()) {
        *error = "the page did not load";
        return -1;
    }
    return form->sizeHint().height();
}

// The items inside \a delegate that are enabled, itself included.
static int enabledPartsOf(QQuickItem *delegate)
{
    int count = 0;
    const QList<QQuickItem *> parts = findQmlComponents(delegate, "");
    for (QQuickItem *part : parts) {
        if (part->isEnabled())
            ++count;
    }
    return count;
}

void QuickUiTest::testAnEnablerGreysOutWhatItControlsWhileTheFormIsOpen()
{
    // setEnabler() is how a page says one setting only applies when another is
    // on, and pages use it heavily. The widget renderer wires it when it builds
    // the control; Qt Quick binds to aspect.enabled instead, which only follows
    // afterwards because the property carries a NOTIFY. This is what says the
    // form really greys out, and keeps doing so while it is open.
    Utils::AspectContainer page;

    Utils::BoolAspect master(&page);
    master.setSettingsKey("Master");
    master.setLabelText("Master");

    Utils::StringAspect slave(&page);
    slave.setSettingsKey("Slave");
    slave.setLabelText("Slave");
    slave.setDisplayStyle(Utils::StringAspect::LineEditDisplay);
    slave.setEnabler(&master);

    QVERIFY2(!slave.isEnabled(), "the enabler did not apply its initial state");

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QQuickItem *rootItem = quickWidget->rootObject();
    if (!rootItem) {
        const QStringList errors = Utils::transform(quickWidget->errors(), &QQmlError::toString);
        QFAIL(qPrintable(errors.join("; ")));
    }

    QQuickItem *slaveDelegate = nullptr;
    const QList<QQuickItem *> items = findQmlComponents(rootItem, "");
    for (QQuickItem *item : items) {
        if (item->property("aspect").value<Utils::BaseAspect *>() == &slave)
            slaveDelegate = item;
    }
    QVERIFY2(slaveDelegate, "the disabled setting was not drawn at all");

    const int whileOff = enabledPartsOf(slaveDelegate);
    master.setValue(true);
    QTRY_VERIFY2(enabledPartsOf(slaveDelegate) > whileOff,
                 "turning the enabler on left the form exactly as it was");

    const int whileOn = enabledPartsOf(slaveDelegate);
    master.setValue(false);
    QTRY_VERIFY2(enabledPartsOf(slaveDelegate) < whileOn,
                 "turning the enabler off again left the form exactly as it was");
}

// The parts of \a delegate a user could type into: enabled, and not read-only.
// Items with no readOnly property of their own - labels, layouts - are not
// counted, because they are not what a value is edited through.
static int typablePartsOf(QQuickItem *delegate)
{
    int count = 0;
    const QList<QQuickItem *> parts = findQmlComponents(delegate, "");
    for (QQuickItem *part : parts) {
        const QVariant readOnly = part->property("readOnly");
        if (readOnly.isValid() && !readOnly.toBool() && part->isEnabled())
            ++count;
    }
    return count;
}

// Every aspect under \a container, however deep, whose qmlName is \a name.
static Utils::BaseAspect *aspectByQmlName(Utils::AspectContainer *container, const QString &name)
{
    for (Utils::BaseAspect *aspect : container->aspects()) {
        if (aspect->qmlName() == name)
            return aspect;
        if (auto nested = qobject_cast<Utils::AspectContainer *>(aspect)) {
            if (Utils::BaseAspect *found = aspectByQmlName(nested, name))
                return found;
        }
    }
    return nullptr;
}

void QuickUiTest::testAPreviewShowsExactlyWhatItsAspectHolds()
{
    // A code style preview indents its text as soon as it is shown, and what
    // the indenter produced is what the page displays. If that never reaches
    // the aspect the two drift apart, and then Format formats text nobody is
    // looking at and appears to do nothing - which is how this was reported.
    Core::setAspectFormFactory([](Utils::AspectContainer *container) {
        return QtcQuick::createAspectForm(container);
    });
    const QScopeGuard clearFactory([] { Core::setAspectFormFactory({}); });

    int checked = 0;
    for (Core::IOptionsPage *page : Core::IOptionsPage::allOptionsPages()) {
        const std::optional<Utils::AspectContainer *> aspects = page->aspects();
        if (!aspects || !*aspects)
            continue;
        Utils::BaseAspect * const preview = aspectByQmlName(*aspects, "Preview");
        if (!preview)
            continue;

        std::unique_ptr<Core::IOptionsPageWidget> widget(page->createWidget());
        QVERIFY(widget);
        auto quickWidget = widget->findChild<QQuickWidget *>();
        QVERIFY2(quickWidget && quickWidget->rootObject(),
                 qPrintable(page->displayName() + " did not render"));

        QObject * const buffer
            = quickWidget->rootObject()->findChild<QObject *>("codeStylePreviewBuffer");
        QVERIFY2(buffer, qPrintable(page->displayName() + " draws no preview buffer"));

        QTRY_COMPARE(buffer->property("text").toString(),
                     preview->volatileVariantValue().toString());
        ++checked;
    }
    QVERIFY2(checked > 0, "no page offered a preview, so this proves nothing");
}

// A code style preview, on screen, driven the way somebody drives it. Reported
// from a real page: the text was not coloured and a selection could not be
// deleted. Neither reproduces, and the difference between this and the tests
// that passed all along is that a page which is never shown never lays out -
// its viewport has no visible lines, so it has no colours to check and no
// characters to click on either.
void QuickUiTest::testAShownPreviewIsColouredAndEditsWithMouseAndKeyboard()
{
    Core::setAspectFormFactory([](Utils::AspectContainer *container) {
        return QtcQuick::createAspectForm(container);
    });
    const QScopeGuard clearFactory([] { Core::setAspectFormFactory({}); });

    int checked = 0;
    for (Core::IOptionsPage *page : Core::IOptionsPage::allOptionsPages()) {
        const std::optional<Utils::AspectContainer *> aspects = page->aspects();
        if (!aspects || !*aspects || !aspectByQmlName(*aspects, "Preview"))
            continue;

        std::unique_ptr<Core::IOptionsPageWidget> widget(page->createWidget());
        QVERIFY(widget);
        widget->resize(900, 600);
        widget->show();
        if (!QTest::qWaitForWindowExposed(widget.get()))
            QSKIP("the page never made it onto the screen");
        // Asked for, not waited on: the key events below reach the window
        // whether or not the platform ever makes it active, and on a machine
        // that never activates a test window the wait only turns this into a
        // skip. What has to be true is that the click focuses the viewport,
        // which is asserted where it happens.
        widget->activateWindow();

        auto quickWidget = widget->findChild<QQuickWidget *>();
        QVERIFY(quickWidget && quickWidget->rootObject());
        QObject * const buffer
            = quickWidget->rootObject()->findChild<QObject *>("codeStylePreviewBuffer");
        QVERIFY(buffer);

        QQuickItem *inner = nullptr;
        for (QQuickItem *view : findQmlNamed(quickWidget->rootObject(), "codeStylePreviewText")) {
            for (QQuickItem *part : findQmlComponents(view, "")) {
                if (QString::fromLatin1(part->metaObject()->className()).contains("TextViewport"))
                    inner = part;
            }
        }
        QVERIFY2(inner, "the preview drew no viewport");
        QTRY_VERIFY2(inner->property("visibleLineCount").toInt() > 0,
                     "the preview laid out no lines, so nothing below means anything");

        // Coloured: highlighting is a foreground colour, so one colour across
        // every format range is text that only looks highlighted.
        QSet<QRgb> foregrounds;
        const int lines = inner->property("visibleLineCount").toInt();
        for (int i = 0; i < lines; ++i) {
            QVariantMap line;
            QMetaObject::invokeMethod(inner, "visibleLine", Q_RETURN_ARG(QVariantMap, line),
                                      Q_ARG(int, i));
            const QVariantList formats = line.value("formats").toList();
            for (const QVariant &format : formats)
                foregrounds.insert(format.toMap().value("foreground").value<QColor>().rgb());
        }
        QVERIFY2(foregrounds.size() > 1,
                 qPrintable(page->displayName() + " draws its preview in one colour"));

        // Edited: press, drag, release, Delete. Not forceActiveFocus() and a
        // property write, which is what a test reaches for and not what
        // anybody does - the click has to be what takes the focus.
        const QString before = buffer->property("text").toString();
        const qreal middle = inner->property("lineHeight").toReal() / 2;
        QWindow * const window = quickWidget->quickWindow();
        QTest::mousePress(window, Qt::LeftButton, {}, inner->mapToScene({4, middle}).toPoint());
        QTest::mouseMove(window, inner->mapToScene({90, middle}).toPoint());
        QTest::mouseRelease(window, Qt::LeftButton, {}, inner->mapToScene({90, middle}).toPoint());

        QTRY_VERIFY2(inner->property("selectionEnd").toInt()
                         > inner->property("selectionStart").toInt(),
                     "dragging across the text selected nothing");
        const int selected = inner->property("selectionEnd").toInt()
                             - inner->property("selectionStart").toInt();
        QVERIFY2(inner->hasActiveFocus(), "clicking in the preview did not focus it");

        QTest::keyClick(window, Qt::Key_Delete);
        QTRY_COMPARE(buffer->property("text").toString().size(), before.size() - selected);
        ++checked;
    }
    QVERIFY2(checked > 0, "no page offered a preview, so this proves nothing");
}

void QuickUiTest::testAReadOnlyAspectOffersNothingToTypeIn_data()
{
    QTest::addColumn<int>("displayStyle");
    QTest::newRow("line edit") << int(Utils::StringAspect::LineEditDisplay);
    QTest::newRow("text edit") << int(Utils::StringAspect::TextEditDisplay);
}

void QuickUiTest::testAReadOnlyAspectOffersNothingToTypeIn()
{
    // setReadOnly() is used for values a page shows but nobody may change - a
    // device's detection log, the effective qmake call. A delegate that reads
    // enabled but not readOnly looks right and lets the value be edited.
    QFETCH(int, displayStyle);

    const auto typableParts = [displayStyle](bool readOnly) -> int {
        Utils::AspectContainer page;
        Utils::StringAspect text(&page);
        text.setSettingsKey("Text");
        text.setLabelText("Text");
        text.setDisplayStyle(Utils::StringAspect::DisplayStyle(displayStyle));
        text.setValue("something");
        text.setReadOnly(readOnly);

        const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
        if (!form)
            return -1;
        auto quickWidget = form->findChild<QQuickWidget *>();
        if (!quickWidget || !quickWidget->rootObject())
            return -1;

        const QList<QQuickItem *> items = findQmlComponents(quickWidget->rootObject(), "");
        for (QQuickItem *item : items) {
            if (item->property("aspect").value<Utils::BaseAspect *>() == &text)
                return typablePartsOf(item);
        }
        return -1;
    };

    const int editable = typableParts(false);
    QVERIFY2(editable > 0, "the editable case offers nothing to type in either, so this "
                           "would pass whatever read-only did");
    QCOMPARE(typableParts(true), 0);
}

void QuickUiTest::testEveryDelegateShowsTheToolTipItsAspectCarries()
{
    // A tool tip is the only place most settings explain themselves, and it is
    // the delegate's job to show it. Nearly every delegate does, which is what
    // makes the one that does not a trap: a page sets a tool tip, nothing
    // complains, and the text is never seen.
    Utils::AspectContainer page;

    Utils::BoolAspect flag(&page);
    flag.setSettingsKey("Flag");
    flag.setLabelText("Flag");

    Utils::StringAspect text(&page);
    text.setSettingsKey("Text");
    text.setLabelText("Text");
    text.setDisplayStyle(Utils::StringAspect::LineEditDisplay);

    Utils::IntegerAspect number(&page);
    number.setSettingsKey("Number");
    number.setLabelText("Number");

    Utils::DoubleAspect fraction(&page);
    fraction.setSettingsKey("Fraction");
    fraction.setLabelText("Fraction");

    Utils::SelectionAspect choice(&page);
    choice.setSettingsKey("Choice");
    choice.setLabelText("Choice");
    choice.setDisplayStyle(Utils::SelectionAspect::DisplayStyle::ComboBox);
    choice.addOption("One");
    choice.addOption("Two");

    Utils::MultiSelectionAspect several(&page);
    several.setSettingsKey("Several");
    several.setLabelText("Several");
    several.setAllValues({"One", "Two"});

    Utils::StringListAspect list(&page);
    list.setSettingsKey("List");
    list.setLabelText("List");

    const QList<Utils::BaseAspect *> aspects = page.aspects();
    for (Utils::BaseAspect *aspect : aspects)
        aspect->setToolTip("What " + aspect->qmlName() + " is for");

    // The generic form, so that the kind picks the delegate the way a page
    // would rather than this test naming one.
    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QQuickItem *rootItem = quickWidget->rootObject();
    if (!rootItem) {
        const QStringList errors = Utils::transform(quickWidget->errors(), &QQmlError::toString);
        QFAIL(qPrintable(errors.join("; ")));
    }

    QStringList silent;
    QSet<Utils::BaseAspect *> seen;
    // The visual tree, not findChildren(): a Repeater's items are visual
    // children of their delegate and QObject children of somewhere else.
    const QList<QQuickItem *> items = findQmlComponents(rootItem, "");
    for (QQuickItem *item : items) {
        auto aspect = item->property("aspect").value<Utils::BaseAspect *>();
        if (!aspect || !aspects.contains(aspect) || seen.contains(aspect))
            continue;
        seen.insert(aspect);
        // Anywhere in the delegate: BoolDelegate is the check box itself and
        // carries the tool tip on its root, while the delegates that wrap a
        // control in a layout put it on the control.
        bool shown = false;
        const QList<QQuickItem *> parts = findQmlComponents(item, "");
        for (QQuickItem *part : parts) {
            if (QQmlProperty(part, "ToolTip.text", qmlContext(part)).read().toString()
                == aspect->toolTip()) {
                shown = true;
                break;
            }
        }
        if (!shown) {
            silent << QString("%1 (%2)")
                          .arg(aspect->qmlName(),
                               QString::fromLatin1(item->metaObject()->className())
                                   .section('_', 0, 0));
        }
    }

    QCOMPARE(seen.size(), aspects.size());
    QVERIFY2(silent.isEmpty(),
             qPrintable("these delegates drop their aspect's tool tip: " + silent.join(", ")));
}

void QuickUiTest::testAGroupCanExplainItselfTheWayAQGroupBoxCould()
{
    // A QGroupBox could carry a tool tip and the closures set it - the Cleanups
    // Upon Saving group had one, and it was lost when the page moved. Groups
    // can say what they are for again, and this is what says the text reaches
    // the title rather than merely being a property nobody reads.
    Utils::AspectContainer page;
    Utils::BoolAspect flag(&page);
    flag.setSettingsKey("TheFlag");
    flag.setLabelText("The flag");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pageQml = dir.filePath("GroupPage.qml");
    {
        QFile file(pageQml);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"(
import QtQuick
import QtCreator.Ui

AspectPage {
    id: root
    AspectGroupBox {
        title: "A group"
        toolTip: "What the group is for"
        BoolDelegate { aspect: root.aspects.TheFlag }
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

    QObject *title = rootItem->findChild<QObject *>("groupTitle");
    QVERIFY2(title, "the group drew no title");
    QCOMPARE(QQmlProperty(title, "ToolTip.text", qmlContext(title)).read().toString(),
             QString("What the group is for"));
}

void QuickUiTest::testAFormInAWidgetLayoutAsksForItsContentHeight()
{
    // A settings page fills the dialog, so nothing depended on what a Quick
    // form asks for until one went into a column beside other widgets - the
    // profiler tool's backend panel is the first. There it has to report the
    // height of its content, or it is squeezed to nothing between whatever is
    // above it and below it.
    QString error;
    const int two = formHeightHintFor(2, &error);
    QVERIFY2(two > 0, qPrintable(error.isEmpty() ? QString("a form asked for no height") : error));

    // Asked twice rather than compared against a number, so that the assertion
    // is about the content and cannot be satisfied by a fixed default.
    const int four = formHeightHintFor(4, &error);
    QVERIFY2(four > two,
             qPrintable(QString("a form of four rows asks for %1, one of two for %2")
                            .arg(four).arg(two)));
}

void QuickUiTest::testBuildingAPageIsNotShowingIt()
{
    // Work that costs something - reloading SDK packages, re-reading what
    // another page just changed - belongs to the page being looked at, not to
    // the page being built. The census below builds every page in the
    // application and shows none of them, so a hook that fired at construction
    // would make a test run spawn an SDK manager.
    Utils::AspectContainer page;
    Utils::BoolAspect flag(&page);
    flag.setLabelText("Flag");
    flag.setQmlName("Flag");

    int shown = 0;
    QObject::connect(&page, &Utils::AspectContainer::shown, &page, [&shown] { ++shown; });

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    QCOMPARE(shown, 0);

    form->resize(400, 300);
    form->show();
    QVERIFY(QTest::qWaitForWindowExposed(form.get()));
    QCOMPARE(shown, 1);

    // The widget path says the same thing, so a page that has not been ported
    // behaves the same way.
    Utils::AspectContainer widgetPage;
    Utils::BoolAspect widgetFlag(&widgetPage);
    widgetFlag.setLabelText("Flag");
    Utils::AspectWidgets::setLayouter(&widgetPage, [&widgetFlag] {
        return Layouting::Column{&widgetFlag};
    });

    int widgetShown = 0;
    QObject::connect(&widgetPage, &Utils::AspectContainer::shown, &widgetPage,
                     [&widgetShown] { ++widgetShown; });

    // Declined by the Quick factory, so this is the widget layout.
    const std::unique_ptr<QWidget> widgetForm(Core::createAspectForm(&widgetPage));
    QVERIFY(widgetForm);
    QVERIFY(!widgetForm->findChild<QQuickWidget *>());
    QCOMPARE(widgetShown, 0);

    widgetForm->resize(400, 300);
    widgetForm->show();
    QVERIFY(QTest::qWaitForWindowExposed(widgetForm.get()));
    QCOMPARE(widgetShown, 1);
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
    // What the rows say. Every list in the product sets this; without it the
    // model answers "No listViewDataCallback set" and barks once per row per
    // repaint - noise in the log of a run that passes, which is where a real
    // complaint has to be noticed.
    servers.listViewDataCallback = [](Utils::BaseAspect *, int role) -> QVariant {
        return role == Qt::DisplayRole ? QVariant("A server") : QVariant();
    };
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

void QuickUiTest::testAButtonCanActAndStillOffer()
{
    // Adding a device runs the wizard, and the menu beside it is the shortcut
    // to one kind. The button therefore has to do both, which the toolchain
    // Add - a menu and nothing else - never needed.
    QVariant picked;
    int acted = 0;
    Utils::AspectContainer page;
    Utils::ActionAspect add(&page);
    add.setActionText("Add...");
    add.setQmlName("Add");
    add.setAction([&acted] { ++acted; });
    add.setChoices({{"Desktop", {}, true, "DesktopDevice"},
                    {"Docker", {}, true, "DockerDevice"}});
    add.setOnChoice([&picked](const QVariant &id) { picked = id; });
    add.setActionIsDefault(true);

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *button = nullptr;
    QTRY_VERIFY(button = findButton(quickWidget->rootObject(), "Add..."));

    // Having choices no longer swallows the click.
    QMetaObject::invokeMethod(button, "clicked");
    QCOMPARE(acted, 1);

    QQuickItem * const delegate = findQmlComponent(quickWidget->rootObject(), "ButtonDelegate");
    QVERIFY(delegate);
    QQuickItem * const arrow = delegate->property("arrowButton").value<QQuickItem *>();
    QVERIFY(arrow);
    QVERIFY(arrow->isVisible());
    QObject * const menuObject = delegate->property("menu").value<QObject *>();
    QVERIFY(menuObject);
    QCOMPARE(menuObject->property("count").toInt(), 2);
    QQuickItem *item = nullptr;
    QMetaObject::invokeMethod(menuObject, "itemAt", Q_RETURN_ARG(QQuickItem *, item),
                              Q_ARG(int, 1));
    QVERIFY(item);
    QMetaObject::invokeMethod(item, "triggered");
    QCOMPARE(picked.toString(), QString("DockerDevice"));
    QCOMPARE(acted, 1);

    // A button that only offers keeps its arrow to itself: there is nothing
    // beside it, because the button *is* the menu.
    Utils::AspectContainer menuOnlyPage;
    Utils::ActionAspect menuOnly(&menuOnlyPage);
    menuOnly.setActionText("Add");
    menuOnly.setQmlName("AddKind");
    // Given an action so that "clicking it does nothing" is a statement about
    // the wiring rather than about there being nothing to run.
    menuOnly.setAction([&acted] { ++acted; });
    menuOnly.setChoices({{"GCC", {}, true, "Gcc"}});
    const std::unique_ptr<QWidget> menuOnlyForm(QtcQuick::createGenericAspectForm(&menuOnlyPage));
    auto menuOnlyQuick = menuOnlyForm->findChild<QQuickWidget *>();
    QVERIFY(menuOnlyQuick);
    QTRY_VERIFY(findButton(menuOnlyQuick->rootObject(), "Add"));
    QQuickItem * const menuOnlyDelegate
        = findQmlComponent(menuOnlyQuick->rootObject(), "ButtonDelegate");
    QVERIFY(menuOnlyDelegate);
    QQuickItem * const noArrow
        = menuOnlyDelegate->property("arrowButton").value<QQuickItem *>();
    QVERIFY(noArrow);
    QVERIFY(!noArrow->isVisible());

    // And in widgets, where the split is an OptionPushButton: a plain
    // setMenu() would have eaten the click there too.
    Layouting::Column column{&add};
    const std::unique_ptr<QWidget> widget(column.emerge());
    QVERIFY(widget);
    QAbstractButton *pushButton = nullptr;
    for (QAbstractButton *candidate : widget->findChildren<QAbstractButton *>()) {
        if (candidate->text() == "Add...")
            pushButton = candidate;
    }
    QVERIFY(pushButton);
    // An OptionPushButton, which pops its menu from the indicator only. A
    // plain QPushButton with a menu drops it down on press instead, so the
    // wizard would be unreachable.
    QVERIFY(dynamic_cast<Utils::OptionPushButton *>(pushButton));
    pushButton->click();
    QCOMPARE(acted, 2);

    QMenu * const menu = pushButton->findChild<QMenu *>();
    QVERIFY(menu);
    QCOMPARE(menu->actions().size(), 2);
    picked = {};
    menu->actions().at(0)->trigger();
    QCOMPARE(picked.toString(), QString("DesktopDevice"));
    QCOMPARE(acted, 2);

    // The offer-only button is the other half of that: a plain QPushButton,
    // and clicking it runs no action because it has none to run.
    Layouting::Column menuOnlyColumn{&menuOnly};
    const std::unique_ptr<QWidget> menuOnlyWidget(menuOnlyColumn.emerge());
    QAbstractButton *menuOnlyPush = nullptr;
    for (QAbstractButton *candidate : menuOnlyWidget->findChildren<QAbstractButton *>()) {
        if (candidate->text() == "Add")
            menuOnlyPush = candidate;
    }
    QVERIFY(menuOnlyPush);
    QVERIFY(!dynamic_cast<Utils::OptionPushButton *>(menuOnlyPush));
    menuOnlyPush->click();
    QCOMPARE(acted, 2);
}

void QuickUiTest::testAContainerRefilledAfterTheFormIsBuiltRedrawsIt()
{
    // What a device asks depends on which device is selected, so its container
    // is refilled while the page is open. The model read the aspects once, at
    // construction, and the page went on drawing the list it started with.
    Utils::AspectContainer page;
    Utils::AspectContainer group(&page);
    group.setLabelText("Type Specific");
    auto first = new Utils::TextDisplay;
    first->setText("Desktop");
    group.registerAspect(first, /*takeOwnership=*/true);

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QQuickItem * const rootItem = quickWidget->rootObject();
    QVERIFY(rootItem);

    const auto drawnTexts = [rootItem] {
        QStringList texts;
        for (QQuickItem *item : findQmlComponents(rootItem, "TextDisplayDelegate"))
            texts << item->property("displayText").toString();
        return texts;
    };
    QTRY_COMPARE(drawnTexts(), QStringList{"Desktop"});

    // The same container, refilled - not a new one. A page that could only
    // swap containers would need one alive per device for the session.
    group.clear();
    auto replacement = new Utils::TextDisplay;
    replacement->setText("Docker");
    group.registerAspect(replacement, /*takeOwnership=*/true);
    auto extra = new Utils::TextDisplay;
    extra->setText("ubuntu:24.04");
    group.registerAspect(extra, /*takeOwnership=*/true);

    QTRY_COMPARE(drawnTexts(), (QStringList{"Docker", "ubuntu:24.04"}));
}

void QuickUiTest::testAFieldWithADefaultOffersToGoBackToIt()
{
    // Four settings ask for a Reset button - Build & Run's two directory
    // templates and Clangd's two index paths. The widget renderer draws one
    // and the Quick delegate did not, so on those pages there was no way back
    // to the default short of typing it out.
    Utils::AspectContainer page;
    Utils::StringAspect directory(&page);
    directory.setQmlName("Directory");
    directory.setDisplayStyle(Utils::StringAspect::LineEditDisplay);
    directory.setDefaultValue("../%{JS: ...}-build");
    directory.setUseResetButton();

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *reset = nullptr;
    QTRY_VERIFY(reset = findButton(quickWidget->rootObject(), "Reset"));
    // Nothing to go back to while the value is the default.
    QCOMPARE(directory.volatileValue(), directory.defaultValue());
    QVERIFY(!reset->isEnabled());

    directory.setValue("/somewhere/else");
    QTRY_VERIFY(reset->isEnabled());

    QMetaObject::invokeMethod(reset, "clicked");
    QTRY_COMPARE(directory.volatileValue(), directory.defaultValue());
    QVERIFY(!reset->isEnabled());

    // An aspect that never asked for one does not get one, or every field on
    // every page would grow a button.
    Utils::AspectContainer plainPage;
    Utils::StringAspect plain(&plainPage);
    plain.setQmlName("Plain");
    plain.setDisplayStyle(Utils::StringAspect::LineEditDisplay);
    plain.setDefaultValue("something");
    const std::unique_ptr<QWidget> plainForm(QtcQuick::createGenericAspectForm(&plainPage));
    auto plainQuick = plainForm->findChild<QQuickWidget *>();
    QVERIFY(plainQuick);
    QTRY_VERIFY(findQmlComponent(plainQuick->rootObject(), "StringDelegate"));
    QQuickItem * const noReset = findButton(plainQuick->rootObject(), "Reset");
    QVERIFY(!noReset || !noReset->isVisible());
}

void QuickUiTest::testASummarySaysWhichCheckFailedAndWhy()
{
    // A page that has to be set up - a JDK here, an SDK there - used to say so
    // with SummaryWidget: a line of prose and a list of check marks inside an
    // expander. The line is the container's label and each check is a row, so
    // both renderers draw it and neither needs to know it is a summary.
    enum { HasJdk = 0, HasSdk, HasTools };
    Utils::AspectContainer page;
    Utils::SummaryAspect summary(&page,
                                 {{HasJdk, "JDK path exists."},
                                  {HasSdk, "Android SDK path exists."},
                                  {HasTools, "Command-line tools installed."}},
                                 "Android settings are OK.",
                                 "Android settings have errors.");
    summary.setQmlName("Summary");

    // Nothing has been checked yet, so nothing is in order.
    QVERIFY(!summary.allRowsOk());
    QCOMPARE(summary.labelText(), QString("Android settings have errors."));
    QCOMPARE(summary.aspects().size(), 3);

    QSignalSpy okSpy(&summary, &Utils::SummaryAspect::allRowsOkChanged);
    summary.setPointValid(HasJdk, true);
    summary.setPointValid(HasSdk, false, "No such directory: /nowhere");
    summary.setPointValid(HasTools, true);
    QVERIFY(!summary.allRowsOk());
    QVERIFY(summary.rowsOk({HasJdk, HasTools}));
    QVERIFY(!summary.rowsOk({HasSdk}));
    QCOMPARE(okSpy.count(), 0);

    const auto rowAt = [&summary](int i) {
        return qobject_cast<Utils::TextDisplay *>(summary.aspects().at(i));
    };
    // A failed check reads as why it failed, not as what it was checking - the
    // whole point of the control.
    QCOMPARE(rowAt(HasSdk)->text(), QString("No such directory: /nowhere"));
    QCOMPARE(rowAt(HasSdk)->presentation().infoType, Utils::InfoType::NotOk);
    QCOMPARE(rowAt(HasJdk)->text(), QString("JDK path exists."));
    QCOMPARE(rowAt(HasJdk)->presentation().infoType, Utils::InfoType::Ok);

    // And putting it right says so once, not once per row.
    summary.setPointValid(HasSdk, true);
    QVERIFY(summary.allRowsOk());
    QCOMPARE(okSpy.count(), 1);
    QCOMPARE(okSpy.last().first().toBool(), true);
    QCOMPARE(rowAt(HasSdk)->text(), QString("Android SDK path exists."));
    QCOMPARE(summary.labelText(), QString("Android settings are OK."));

    summary.setInfoText("(SDK 34)");
    QCOMPARE(summary.labelText(), QString("Android settings are OK. (SDK 34)"));

    // While the checks are running the summary reports neither answer.
    summary.setInProgressText("Checking");
    QCOMPARE(summary.labelText(), QString("Checking..."));

    // The rows are drawn like any other, so a page needs no delegate of its own.
    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QQuickItem *rootItem = nullptr;
    QTRY_VERIFY(rootItem = quickWidget->rootObject());
    QStringList drawn;
    QTRY_COMPARE(findQmlComponents(rootItem, "TextDisplayDelegate").size(), 3);
    for (QQuickItem *item : findQmlComponents(rootItem, "TextDisplayDelegate"))
        drawn << item->property("displayText").toString();
    QCOMPARE(drawn, (QStringList{"JDK path exists.", "Android SDK path exists.",
                                 "Command-line tools installed."}));
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
    // Both kinds: the name in front is a FormLabel, which is a Label by
    // another name, and findQmlComponents matches the type rather than what
    // it derives from.
    for (const QString &kind : {QString("FormLabel"), QString("Label")}) {
        for (QQuickItem *label : findQmlComponents(delegate, kind)) {
            if (label->property("visible").toBool())
                drawn << label->property("text").toString();
        }
    }
    QCOMPARE(drawn, QStringList({"Timeout:", "s"}));
}

void QuickUiTest::testSpinBoxShowsTheValueScaledDown()
{
    // An aspect can keep a value in one unit and show it in another - the test
    // timeout is milliseconds and reads in seconds - so the box shows the
    // value, and its bounds, divided by that factor. The widget renderer has
    // always done this; nothing checked that the Qt Quick one does.
    Utils::AspectContainer page;
    Utils::IntegerAspect timeout(&page);
    timeout.setLabelText("Timeout:");
    timeout.setRange(5000, 600000);
    timeout.setValue(30000);
    timeout.setDisplayScaleFactor(1000);

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "IntegerDelegate"));
    QQuickItem *box = nullptr;
    QTRY_VERIFY(box = findQmlComponent(delegate, "SpinBox"));

    // Seconds, not milliseconds. Without the factor these read 30000/5000/600000.
    QTRY_COMPARE(box->property("value").toInt(), 30);
    QCOMPARE(box->property("from").toInt(), 5);
    QCOMPARE(box->property("to").toInt(), 600);

    // And the aspect keeps its own unit.
    QCOMPARE(timeout.value(), 30000);
}

void QuickUiTest::testFileChooserSplitsTheDialogFilter()
{
    // Qt states its file filters as one ";;"-separated string and QML's
    // FileDialog wants them one at a time, so the delegate splits. Handed the
    // string whole, the chooser offers a single filter that reads
    // "Scripts (*.script);;All files (*)" and matches nothing.
    Utils::AspectContainer page;
    Utils::FilePathAspect script(&page);
    script.setLabelText("Script:");
    script.setPromptDialogTitle("Choose Script");
    script.setPromptDialogFilter("Scripts (*.script);;All files (*)");

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "StringDelegate"));

    // A dialog is not an item, so it is not in the visual tree the other
    // lookups walk - it is a plain child of the delegate.
    QObject *dialog = nullptr;
    const QList<QObject *> children = delegate->findChildren<QObject *>();
    for (QObject *child : children) {
        if (QString::fromLatin1(child->metaObject()->className()).contains("FileDialog")) {
            dialog = child;
            break;
        }
    }
    QVERIFY2(dialog, "the delegate has no FileDialog to configure");

    QCOMPARE(dialog->property("title").toString(), QString("Choose Script"));
    QCOMPARE(dialog->property("nameFilters").toStringList(),
             QStringList({"Scripts (*.script)", "All files (*)"}));
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
    // Shown, because what this checks is what happens when the editor gains
    // and loses the focus, and an unshown window hands it to nothing - which
    // is why this failed about one run in ten.
    form->resize(400, 300);
    form->show();
    QVERIFY(QTest::qWaitForWindowExposed(form.get()));
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

    // The delegate asks on completion, and the aspect is what says the answer
    // arrived: displayTextChanged() is emitted on the way out of the fetch
    // whether the keychain gave a secret or a reason. Waiting on that rather
    // than on the field becoming writable is what makes this test say
    // something on a machine whose keychain refuses - see below.
    QVERIFY(secret.isReadOnly());
    QSignalSpy fetched(&secret, &Utils::BaseAspect::displayTextChanged);
    if (!QTest::qWaitFor([&fetched] { return fetched.count() > 0; }, 5000)) {
        // Not a failure and not something to wait longer for: on a machine
        // whose keychain does not answer there is no fetch to check. Skipping
        // here is safe precisely because the outcomes below are asserted -
        // a fetch that *does* finish and still leaves the field read-only is
        // a failure, so this cannot swallow that.
        QSKIP("the keychain did not answer, so there is no fetch to check");
    }

    // What it found decides what the field may do, and both outcomes are the
    // aspect's design rather than an accident: a secret that was read can be
    // edited, and one that could not be read leaves the field guarded with
    // the reason in it, so that typing does not overwrite a secret that is
    // still there. The fetch is finished, so this answers from the cache.
    bool wasRead = false;
    QString reason;
    secret.requestValue([&wasRead, &reason](const Utils::Result<QString> &value) {
        wasRead = bool(value);
        if (!value)
            reason = value.error();
    });

    if (!wasRead) {
        // Nothing was read - a locked keychain, or none. The guard stays up
        // and the field says why instead of pretending the secret is empty.
        QVERIFY2(secret.isReadOnly(),
                 "a secret that could not be read left its field writable");
        QVERIFY2(!reason.isEmpty(), "the field guards itself and says nothing");
        QTRY_COMPARE(field->property("placeholderText").toString(), reason);
        return;
    }

    QVERIFY2(!secret.isReadOnly(),
             "the keychain answered and the field is still read-only");
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


// The delegate's half of the secret contract, with no keychain in the way.
//
// requestValue() answers from what the aspect holds when it has been given a
// value, so the fetch finishes synchronously and every branch below runs.
// testSecretIsFetchedBeforeItCanBeEdited covers the same ground through the
// keychain and skips where the keychain does not answer - which is most runs
// on some machines, and would leave this uncovered entirely.
void QuickUiTest::testASecretTheAspectAlreadyHoldsNeedsNoKeychain()
{
    Utils::AspectContainer page;
    Core::SecretAspect secret(&page);
    secret.setSettingsKey("Test.Known");
    secret.setLabelText("Password:");
    secret.setValue("already known");

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());
    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "SecretDelegate"));
    QQuickItem * const field = findQmlComponent(delegate, "TextField");
    QVERIFY(field);

    // Nothing to wait for: the aspect has it, so the guard is down by the
    // time the field is drawn.
    QTRY_VERIFY2(!secret.isReadOnly(),
                 "a secret the aspect already holds still guards its field");
    QVERIFY2(!field->property("readOnly").toBool(),
             "the aspect stopped guarding and the field did not");

    // Shown as a secret rather than as text.
    const QMetaObject *mo = field->metaObject();
    const QMetaEnum echoModes = mo->property(mo->indexOfProperty("echoMode")).enumerator();
    QCOMPARE(field->property("echoMode").toInt(), echoModes.keyToValue("Password"));

    // And what it holds is what it shows, read through displayText() because
    // a secret has no value property for a delegate to bind to.
    QTRY_COMPARE(field->property("text").toString(), QString("already known"));

    // Written back the same way.
    field->setProperty("text", "hunter2");
    QMetaObject::invokeMethod(field, "editingFinished");
    QCOMPARE(secret.displayText(), QString("hunter2"));
}


// An aspect can hang one entry off its control's right-click menu: on the
// Kits page it is whether a setting may be changed per run configuration.
// AspectContextMenu draws it, and nothing tested that component at all - so
// the whole feature rested on three names resolving, each read with ?. and
// each silent when it does not.
class ContextActionAspect final : public Utils::SelectionAspect
{
public:
    using SelectionAspect::SelectionAspect;

    Utils::AspectPresentation presentation() const override
    {
        Utils::AspectPresentation p = SelectionAspect::presentation();
        p.contextActionText = m_text;
        p.contextActionChecked = m_checked;
        p.contextActionEnabled = m_enabled;
        return p;
    }

    void triggerContextAction(bool checked) override
    {
        ++m_triggered;
        m_checked = checked;
        // What a kit aspect does: the action is the state, so saying it
        // changed is what redraws the entry.
        emit controlConfigurationChanged();
    }

    QString m_text = "Change on Run Configuration";
    bool m_checked = false;
    bool m_enabled = true;
    int m_triggered = 0;
};

void QuickUiTest::testAControlOffersTheContextActionItsAspectDescribes()
{
    Utils::AspectContainer page;
    page.setAutoApply(false);
    ContextActionAspect mutable_(&page);
    mutable_.setLabelText("Compiler");
    // A combo box: RadioButtons is the enum's first value and so the default,
    // and only the combo hangs a context menu off itself.
    mutable_.setDisplayStyle(Utils::SelectionAspect::DisplayStyle::ComboBox);
    mutable_.addOption("gcc");
    mutable_.addOption("clang");

    // One that offers nothing, to show the menu is not simply always there.
    ContextActionAspect plain(&page);
    plain.setLabelText("Debugger");
    plain.setDisplayStyle(Utils::SelectionAspect::DisplayStyle::ComboBox);
    plain.addOption("lldb");
    plain.m_text.clear();

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QList<QQuickItem *> menus;
    QTRY_COMPARE((menus = findQmlComponents(quickWidget->rootObject(),
                                            "AspectContextMenu")).size(), 2);

    const auto menuFor = [&menus](Utils::BaseAspect *aspect) -> QQuickItem * {
        for (QQuickItem *menu : menus) {
            if (menu->property("aspect").value<Utils::BaseAspect *>() == aspect)
                return menu;
        }
        return nullptr;
    };
    QQuickItem * const offered = menuFor(&mutable_);
    QQuickItem * const none = menuFor(&plain);
    QVERIFY(offered && none);

    // Right-clicking reaches it only where there is something to offer. The
    // area is over the control, so an area that is enabled for nothing would
    // take the click and pop up an empty menu.
    // MouseArea::enabled is its own property - whether the area handles mouse
    // events - and is not QQuickItem::isEnabled(), which stays true for a
    // MouseArea that takes nothing. Reading the item's would pass either way.
    const auto takesClicks = [](QQuickItem *area) {
        return area->property("enabled").toBool();
    };
    QVERIFY2(takesClicks(offered), "an aspect offering a context action has no menu");
    QVERIFY2(!takesClicks(none), "an aspect offering nothing still takes right-clicks");

    QObject * const entry = offered->findChild<QObject *>("aspectContextAction");
    QVERIFY(entry);
    QCOMPARE(entry->property("text").toString(), QString("Change on Run Configuration"));
    QVERIFY(entry->property("checkable").toBool());
    QVERIFY(entry->property("enabled").toBool());
    QVERIFY(!entry->property("checked").toBool());

    // It says what the aspect holds, and follows it when something else
    // changes it. Checked here rather than after triggering: triggering a
    // checkable entry flips it whatever the binding says, so an entry that
    // had stopped following would pass that way round.
    mutable_.m_checked = true;
    emit mutable_.controlConfigurationChanged();
    QTRY_VERIFY2(entry->property("checked").toBool(),
                 "the entry does not say what the aspect holds");
    mutable_.m_checked = false;
    emit mutable_.controlConfigurationChanged();
    QTRY_VERIFY2(!entry->property("checked").toBool(),
                 "the entry kept saying the action was on after it went off");

    // Choosing it tells the aspect, with what it was set to.
    triggerMenuItem(entry);
    QCOMPARE(mutable_.m_triggered, 1);
    QVERIFY2(mutable_.m_checked, "the aspect was told the entry was unchecked");

    // What the aspect says about the entry is read, not assumed: an action
    // that cannot be used right now is drawn as such.
    mutable_.m_enabled = false;
    emit mutable_.controlConfigurationChanged();
    QTRY_VERIFY2(!entry->property("enabled").toBool(),
                 "an action the aspect disabled is still offered");
}


// The Record button on a key sequence. KeySequenceDelegate is deliberately
// generic - the aspect that drives it lives in coreplugin, which QtcQuick
// cannot see - so `recording` and `setRecording` are read off an untyped
// Aspect and qmllint reports both as missing. That left the button's whole
// behaviour resting on two names, exercised by nothing.
//
// The Keyboard page is not needed to check it: the delegate is chosen by
// AspectPresentation::control, so an aspect that says KeySequence and offers
// the same two members is drawn by it.
class RecordingAspect final : public Utils::StringAspect
{
    Q_OBJECT
    Q_PROPERTY(bool recording READ isRecording NOTIFY recordingChanged)

public:
    using StringAspect::StringAspect;

    Utils::AspectPresentation presentation() const override
    {
        Utils::AspectPresentation p = StringAspect::presentation();
        p.control = Utils::AspectControls::KeySequence;
        return p;
    }

    bool isRecording() const { return m_recording; }

    Q_INVOKABLE void setRecording(bool recording)
    {
        ++m_asked;
        if (m_recording == recording)
            return;
        m_recording = recording;
        emit recordingChanged();
    }

    int m_asked = 0;

signals:
    void recordingChanged();

private:
    bool m_recording = false;
};

void QuickUiTest::testRecordingAKeySequenceIsTheAspectsToStartAndStop()
{
    Utils::AspectContainer page;
    page.setAutoApply(false);
    RecordingAspect keys(&page);
    keys.setLabelText("Shortcut");
    keys.setValue("Ctrl+K");

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "KeySequenceDelegate"));
    QQuickItem * const record = findQmlNamed(delegate, "keySequenceRecordButton").value(0);
    QVERIFY2(record, "a key sequence offers no way to record one");

    // Not recording: the button offers to start, and says so.
    QCOMPARE(record->property("text").toString(), QString("Record"));
    QVERIFY(!record->property("checked").toBool());

    // The button says what the aspect is doing, not what it was last
    // clicked - checked here by starting the recording from elsewhere, which
    // is the only order that tells a binding from a button toggling itself.
    keys.setRecording(true);
    QTRY_COMPARE(record->property("text").toString(), QString("Stop Recording"));
    QVERIFY2(record->property("checked").toBool(),
             "the aspect is recording and its button says it is not");
    keys.setRecording(false);
    QTRY_COMPARE(record->property("text").toString(), QString("Record"));

    // And clicking it is what asks the aspect to start: the delegate cannot
    // record anything itself, because the keys it would record never reach a
    // control.
    const int asked = keys.m_asked;
    clickCheckableButton(record);
    QCOMPARE(keys.m_asked, asked + 1);
    QVERIFY2(keys.isRecording(), "clicking Record did not start the recording");
    QTRY_COMPARE(record->property("text").toString(), QString("Stop Recording"));

    // Clicking again stops it, which is the same button.
    clickCheckableButton(record);
    QVERIFY2(!keys.isRecording(), "clicking Stop Recording did not stop it");
    QTRY_COMPARE(record->property("text").toString(), QString("Record"));
}

void QuickUiTest::testAnInlineListItemCanReadAsOneRow()
{
    // A port mapping is four fields that mean one mapping, so an item of the
    // list is a row. An inline list draws each item's aspects itself, from the
    // item's model, so the item's own kind never reaches a delegate - the model
    // has to carry how it reads. The widget form asks the container directly
    // and has drawn it as a row all along.
    const auto build = [](Utils::AspectContainer &page, bool inRow) {
        auto list = new Utils::AspectList(&page);
        list->setLabelText("Port mappings:");
        list->setCreateItemFunction([inRow] {
            auto item = std::make_shared<Utils::AspectContainer>();
            item->setInlineRow(inRow);
            for (const char *name : {"HostPort", "ContainerPort"}) {
                auto part = new Utils::StringAspect(item.get());
                part->setQmlName(QString::fromLatin1(name));
                part->setLabelText(QString::fromLatin1(name));
                part->setDisplayStyle(Utils::StringAspect::LineEditDisplay);
            }
            return item;
        });
        list->createAndAddItem();
    };

    const auto fieldsOf = [](Utils::AspectContainer &page, std::unique_ptr<QWidget> &keep) {
        keep.reset(QtcQuick::createGenericAspectForm(&page));
        auto quick = keep->findChild<QQuickWidget *>();
        QTC_ASSERT(quick, return QList<QQuickItem *>());
        keep->resize(800, 600);
        keep->show();
        if (!QTest::qWaitForWindowExposed(keep.get()))
            return QList<QQuickItem *>();
        return findQmlComponents(quick->rootObject(), "StringDelegate");
    };

    Utils::AspectContainer stacked;
    build(stacked, false);
    std::unique_ptr<QWidget> stackedForm;
    const QList<QQuickItem *> stackedFields = fieldsOf(stacked, stackedForm);
    QCOMPARE(stackedFields.size(), 2);
    QVERIFY(stackedFields.at(1)->y() > stackedFields.at(0)->y());
    QCOMPARE(stackedFields.at(1)->x(), stackedFields.at(0)->x());

    Utils::AspectContainer inline_;
    build(inline_, true);
    std::unique_ptr<QWidget> inlineForm;
    const QList<QQuickItem *> inlineFields = fieldsOf(inline_, inlineForm);
    QCOMPARE(inlineFields.size(), 2);
    QVERIFY(inlineFields.at(1)->x() > inlineFields.at(0)->x());
    QCOMPARE(inlineFields.at(1)->y(), inlineFields.at(0)->y());
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
    servers.listViewDataCallback = [](Utils::BaseAspect *item, int role) -> QVariant {
        if (role != Qt::DisplayRole)
            return {};
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

// The shape UseGlobalAspect has: a check box whose label takes you to the page
// the setting would otherwise come from.
class CheckBoxWithLink final : public Utils::BoolAspect
{
public:
    explicit CheckBoxWithLink(Utils::AspectContainer *container)
        : Utils::BoolAspect(container)
    {
        setLabel("Use <a href=\"page\">global settings</a>",
                 Utils::BoolAspect::LabelPlacement::BesideCheckBox);
    }

    void activateLink(const QString &link) override { followed = link; }

    QString followed;
};

void QuickUiTest::testACheckBoxLabelCanBeALink()
{
    // A QtQuick CheckBox draws its own text and draws it plain, so a label
    // that is more than text is a label of its own beside the box - which is
    // a delegate of its own, so that every other check box is left alone.
    Utils::AspectContainer page;
    CheckBoxWithLink aspect(&page);
    aspect.setValue(true);

    QCOMPARE(int(QtcQuick::AspectContainerModel::kindOf(&aspect)),
             int(QtcQuick::AspectContainerModel::BoolWithOwnLabel));

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate
                = findQmlComponent(quickWidget->rootObject(), "BoolWithOwnLabelDelegate"));

    // Named as the plain delegate names them, so a page that swapped one for
    // the other reads the same.
    QVERIFY(delegate->property("checked").toBool());

    // The markup intact, and drawn as markup: shown as plain text the link
    // would be angle brackets on the page and nothing to click.
    QQuickItem * const label = findQmlComponent(delegate, "Label");
    QVERIFY(label);
    QVERIFY2(label->property("text").toString().contains("<a href="),
             qPrintable(label->property("text").toString()));
    const QMetaObject *mo = label->metaObject();
    const QMetaEnum formats = mo->property(mo->indexOfProperty("textFormat")).enumerator();
    QCOMPARE(label->property("textFormat").toInt(), formats.keyToValue("StyledText"));

    // Following it is the aspect's business.
    QMetaObject::invokeMethod(label, "linkActivated", Q_ARG(QString, "page"));
    QCOMPARE(aspect.followed, QString("page"));

    // And it is still a check box.
    QQuickItem * const box = findQmlComponent(delegate, "CheckBox");
    QVERIFY(box);
    QMetaObject::invokeMethod(box, "toggle");
    QMetaObject::invokeMethod(box, "toggled");
    QVERIFY(!aspect.volatileValue());
}

// An aspect whose complaint is whatever it was last told, so that a test can
// change what is wrong without changing what is typed.
class AspectWithAToldProblem final : public Utils::FilePathAspect
{
public:
    using Utils::FilePathAspect::FilePathAspect;

    QString problem;

    QString validationMessage(const QVariant &) const override { return problem; }
};

void QuickUiTest::testAFieldCanBeAskedForTheCursorAndForAReCheck()
{
    // Two things the widget side does on request and this side used to drop:
    // put the cursor in a field, and check the value again because something
    // outside now knows more than it did.
    Utils::AspectContainer page;
    AspectWithAToldProblem field(&page);
    field.setLabelText("Build directory:");
    field.setValue(Utils::FilePath::fromString("/tmp"));

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "StringDelegate"));
    QQuickItem * const input = findQmlComponent(delegate, "TextField");
    QVERIFY(input);
    QVERIFY(!input->property("focus").toBool());

    // "A page that has just made something for the user to name": adding a kit
    // puts the cursor in its name.
    field.setFocusToInputField();
    QTRY_VERIFY(input->property("focus").toBool());

    // The complaint is the aspect's to answer, and it can change without the
    // value changing - a build system reporting what is wrong with a
    // directory. validateInput() is how it says so.
    QCOMPARE(input->property("error").toString(), QString());
    field.problem = "The build directory is not reachable from the build device.";
    field.validateInput();
    QTRY_COMPARE(input->property("error").toString(), field.problem);
}

// The same aspect the widget renderer test uses: any control will do, because
// the point is that being told does not depend on which one.
class CountingShownAspect final : public Utils::BoolAspect
{
public:
    explicit CountingShownAspect(Utils::AspectContainer *container)
        : Utils::BoolAspect(container)
    {
        setLabel("Enabled", Utils::BoolAspect::LabelPlacement::Compact);
    }

    void requestDisplayText() override { ++shown; }

    int shown = 0;
};

void QuickUiTest::testAnAspectIsToldWhenItIsDrawn()
{
    // Two delegates used to ask and the rest did not, so an aspect whose label
    // reports something it has to go and look up worked as a button and not as
    // a check box. AspectItems asks for all of them, which is also where the
    // widget renderer asks - one place each, and the same cases.
    Utils::AspectContainer page;
    CountingShownAspect aspect(&page);
    QCOMPARE(aspect.shown, 0);

    const std::unique_ptr<QWidget> form(QtcQuick::createGenericAspectForm(&page));
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QVERIFY(quickWidget->rootObject());

    QTRY_VERIFY(findQmlComponent(quickWidget->rootObject(), "BoolDelegate"));
    QCOMPARE(aspect.shown, 1);
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

    void setRows(const QList<Row> &newRows)
    {
        beginResetModel();
        rows = newRows;
        endResetModel();
    }
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

    // Unset unless a test says otherwise, so that every other table here looks
    // as it did.
    QIcon rowIcon;

    QVariant data(const QModelIndex &index, int role) const override
    {
        const Row &row = rows.at(index.row());
        if (role == Qt::DecorationRole)
            return index.column() == ColumnWord ? QVariant::fromValue(rowIcon) : QVariant();
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
        p.rowBackground = m_rowBackground;
        return p;
    }

    TestTableModel m_model;
    QString m_filterPlaceholderText;
    QColor m_rowBackground;
};

// The TableView the delegate builds, and the model it actually shows - the
// filter proxy, not the aspect's own.
static QQuickItem *tableViewOf(QQuickItem *root)
{
    QQuickItem *delegate = findQmlComponent(root, "TableDelegate");
    return delegate ? findQmlComponent(delegate, "QQuickTableView") : nullptr;
}

void QuickUiTest::testATableCellShowsTheIconItsModelGives()
{
    // A QIcon is what a widget view wants from Qt::DecorationRole and what QML
    // cannot carry, so the cell asks AspectModels to turn it into something an
    // Image can load. A table whose model gives no icon must look exactly as it
    // did, which is the other half of this.
    const auto iconsIn = [](QQuickItem *root) {
        int shown = 0;
        for (QQuickItem * const icon : findQmlComponents(root, "QQuickImage")) {
            if (icon->objectName() == "tableCellIcon" && icon->property("visible").toBool()
                && !icon->property("source").toUrl().isEmpty()) {
                ++shown;
            }
        }
        return shown;
    };

    Utils::AspectContainer bare;
    TestTableAspect noIcons(&bare);
    noIcons.setLabelText("Rows");
    const std::unique_ptr<QWidget> bareForm(showForm(&bare));
    QVERIFY(bareForm);
    auto bareQuick = bareForm->findChild<QQuickWidget *>();
    QVERIFY(bareQuick);
    QQuickItem *bareView = nullptr;
    QTRY_VERIFY(bareView = tableViewOf(bareQuick->rootObject()));
    QTRY_COMPARE(bareView->property("rows").toInt(), 2);
    QCOMPARE(iconsIn(bareQuick->rootObject()), 0);

    Utils::AspectContainer page;
    TestTableAspect table(&page);
    table.setLabelText("Rows");
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::red);
    table.m_model.rowIcon = QIcon(pixmap);

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QQuickItem *view = nullptr;
    QTRY_VERIFY(view = tableViewOf(quickWidget->rootObject()));
    QTRY_COMPARE(view->property("rows").toInt(), 2);

    // One per row, in the column the model decorated.
    QTRY_COMPARE(iconsIn(quickWidget->rootObject()), 2);
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
// A page that shows the details of the current row, or a Remove that acts on
// it, is talking about a row the user has to be able to pick out. The widget
// view drew the selection; the Quick table drew nothing at all, so Font &&
// Colors showed the properties of a format that looked like any other.
void QuickUiTest::testTheRowTheUserIsOnIsDrawnAsSelected()
{
    Utils::AspectContainer page;
    TestTableAspect table(&page);
    table.setLabelText("Rows");

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);

    QQuickItem *view = nullptr;
    QTRY_VERIFY(view = tableViewOf(quickWidget->rootObject()));
    auto shown = view->property("model").value<QAbstractItemModel *>();
    QVERIFY(shown);
    QCOMPARE(shown->rowCount({}), 2);
    auto selection = view->property("selectionModel").value<QItemSelectionModel *>();
    QVERIFY(selection);

    // Read off what is drawn rather than off the property behind it: the row
    // is marked by a rectangle over the row's own colours, and a binding that
    // is right while nothing draws it is the bug this is here for.
    const auto marked = [view] {
        QStringList texts;
        for (QQuickItem *cell : findQmlComponents(view, "AspectTableCell")) {
            const QList<QQuickItem *> mark = findQmlNamed(cell, "tableCellHighlight");
            if (!mark.isEmpty() && mark.first()->isVisible())
                texts << cell->property("cellText").toString();
        }
        texts.sort();
        return texts;
    };

    QCOMPARE(marked(), QStringList());

    const auto textsOfRow = [shown](int row) {
        QStringList texts;
        for (int column = 0; column < shown->columnCount({}); ++column)
            texts << shown->index(row, column).data().toString();
        texts.sort();
        return texts;
    };

    selection->setCurrentIndex(shown->index(0, 0),
                               QItemSelectionModel::ClearAndSelect
                                   | QItemSelectionModel::Rows);
    QTRY_COMPARE(marked(), textsOfRow(0));

    // And it follows: a mark that is painted once and stays is worse than none.
    selection->setCurrentIndex(shown->index(1, 0),
                               QItemSelectionModel::ClearAndSelect
                                   | QItemSelectionModel::Rows);
    QTRY_COMPARE(marked(), textsOfRow(1));
}

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

namespace {

// A tree whose names are too long for a column's floor width - what a column
// has to grow for. One of them is a single word, which cannot wrap and so is
// clipped; the other is several, which wrap and so make the row taller.
class LongNamedTreeAspect : public Utils::BaseAspect
{
public:
    static QString unbreakable() { return "aRatherLongPropertyName"; }
    static QString wrappable() { return "a rather long property name"; }
    // Longer than the width a column is capped at when the table cannot hold
    // what its columns want.
    static QString longerThanTheCap()
    {
        return "a property name longer than any column is allowed to be "
               "when the table has to make them fit";
    }

    LongNamedTreeAspect()
    {
        m_model.setHeader({"Name", "Value"});
        m_model.rootItem()->appendChild(
            new Utils::StaticTreeItem(QStringList{unbreakable(), "1"}));
        m_model.rootItem()->appendChild(
            new Utils::StaticTreeItem(QStringList{wrappable(), "2"}));
        m_model.rootItem()->appendChild(
            new Utils::StaticTreeItem(QStringList{longerThanTheCap(), "3"}));
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

// A column is sized from what its cells say they want, and in a tree the
// indent and the branch handle stand in front of the cell. A column sized
// from the cell alone is short by exactly them, so the first column - the one
// holding the names - is the one that clips.
void QuickUiTest::testAColumnMakesRoomForTheNamesInIt()
{
    Utils::AspectContainer page;
    page.setAutoApply(false);
    LongNamedTreeAspect properties;
    properties.setLabelText("Profile properties");
    page.registerAspect(&properties);

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);

    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "TreeDelegate"));
    QQuickItem *view = findQmlNamed(delegate, "aspectTree").value(0);
    QVERIFY(view);
    QTRY_COMPARE(findQmlNamed(view, "tableCellLabel").size(), 6);

    // Metrics.lineEditWidth, which is the width a column falls back to when
    // its cells ask for nothing.
    const qreal columnFloor = 120;

    // How much of the label is left for the text, against what the text takes
    // on one line. Measured off the label's own font rather than a number
    // written here, which would measure the machine's fonts instead.
    const auto shortfall = [view, columnFloor](const QString &text) -> QString {
        for (QQuickItem *label : findQmlNamed(view, "tableCellLabel")) {
            if (label->property("text").toString() != text)
                continue;
            const QFontMetricsF metrics(label->property("font").value<QFont>());
            const qreal needed = metrics.horizontalAdvance(text);
            const qreal room = label->width()
                               - label->property("leftPadding").toReal()
                               - label->property("rightPadding").toReal();
            // Otherwise the answer cannot be a failure: a name that fits in
            // the floor width says nothing about whether the column grew.
            if (needed <= columnFloor)
                return QString("\"%1\" takes only %2px, which the column's "
                               "floor already holds")
                    .arg(text)
                    .arg(needed);
            // A pixel of slack: a column's width is rounded, and a quarter
            // of a pixel is not a name being cut off.
            if (room + 1 < needed)
                return QString("\"%1\" takes %2px and has %3px to draw in")
                    .arg(text)
                    .arg(needed)
                    .arg(room);
            return {};
        }
        return QString("\"%1\" is not drawn at all").arg(text);
    };

    QVERIFY2(shortfall(LongNamedTreeAspect::unbreakable()).isEmpty(),
             qPrintable(shortfall(LongNamedTreeAspect::unbreakable())));
    QVERIFY2(shortfall(LongNamedTreeAspect::wrappable()).isEmpty(),
             qPrintable(shortfall(LongNamedTreeAspect::wrappable())));

    // A column is capped so that one long description cannot make the table
    // wider than the view and scroll its own header out of sight. When the
    // table can hold what its columns want, the cap has nothing to do: the
    // widget header sized every column to its contents.
    QVERIFY2(shortfall(LongNamedTreeAspect::longerThanTheCap()).isEmpty(),
             qPrintable(shortfall(LongNamedTreeAspect::longerThanTheCap())));

    // And still after the view lays out again, which is what a page does the
    // moment it is resized: a width that only holds until something asks for
    // it a second time is not a width the user ever sees.
    QMetaObject::invokeMethod(view, "forceLayout");
    QTRY_COMPARE(findQmlNamed(view, "tableCellLabel").size(), 6);
    QVERIFY2(shortfall(LongNamedTreeAspect::unbreakable()).isEmpty(),
             qPrintable("after laying out again: "
                        + shortfall(LongNamedTreeAspect::unbreakable())));
    QVERIFY2(shortfall(LongNamedTreeAspect::wrappable()).isEmpty(),
             qPrintable("after laying out again: "
                        + shortfall(LongNamedTreeAspect::wrappable())));
}

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

// A field with a history remembers what was entered into it and offers it
// next time. The widget line edit does this through a QCompleter, which is why
// a dozen pages set a history key and then, once drawn with Qt Quick, offered
// nothing at all: only the widget renderer read that key.
void QuickUiTest::testAFieldOffersWhatWasTypedIntoItBefore()
{
    const Utils::Key key = "QuickUiTest.MakePath";
    Utils::CompletionHistory::clear(key);
    const QScopeGuard forget([key] { Utils::CompletionHistory::clear(key); });

    Utils::AspectContainer page;
    page.setAutoApply(false);
    Utils::StringAspect makePath(&page);
    makePath.setLabelText("Make path");
    makePath.setDisplayStyle(Utils::StringAspect::LineEditDisplay);
    makePath.setHistoryCompleter(key);

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);

    QQuickItem *field = nullptr;
    QTRY_VERIFY(field = findQmlComponent(quickWidget->rootObject(), "TextField"));
    QObject *popup = field->findChild<QObject *>("completionPopup");
    QVERIFY(popup);
    // Nothing has been entered here before, so there is nothing to offer.
    QVERIFY(popup->property("completions").toStringList().isEmpty());

    // Entered and finished with, which is when a field remembers.
    field->setProperty("text", "/usr/bin/make");
    QMetaObject::invokeMethod(field, "editingFinished");
    // The page waits for Apply, so this is in the field rather than in the
    // setting - and a field remembers what was typed into it either way.
    QCOMPARE(makePath.volatileValue(), QString("/usr/bin/make"));
    QVERIFY(makePath.value().isEmpty());

    // On offer now, in this form - the delegate re-reads what the aspect
    // offers when the aspect says it has changed - and in the next one.
    QTRY_COMPARE(popup->property("completions").toStringList(),
                 QStringList({"/usr/bin/make"}));
    QCOMPARE(makePath.presentation().completions, QStringList({"/usr/bin/make"}));

    // The most recent first, and each entry once however often it is used.
    field->setProperty("text", "/opt/bin/make");
    QMetaObject::invokeMethod(field, "editingFinished");
    field->setProperty("text", "/usr/bin/make");
    QMetaObject::invokeMethod(field, "editingFinished");
    QCOMPARE(makePath.presentation().completions,
             QStringList({"/usr/bin/make", "/opt/bin/make"}));

    // And the widget form reads the same history, so a field remembers the
    // same things however it happens to be drawn.
    QVERIFY2(Utils::HistoryCompleter::historyExistsFor(key),
             "the two kinds of form keep separate histories");
}

// Browse opens beside the path that is already in the field, the way the
// widget path chooser does: a dialog that opens wherever the platform was last
// makes the reader navigate back to what they can already see.
void QuickUiTest::testBrowsingStartsWhereThePathAlreadyPointsTo()
{
    Utils::TemporaryDirectory dir("quickui-browse-start");
    QVERIFY(dir.isValid());
    const Utils::FilePath sub = dir.path() / "inner";
    QVERIFY(sub.createDir());
    const Utils::FilePath file = sub / "thing.txt";
    QVERIFY(file.writeFileContents("x"));

    Utils::AspectContainer page;
    page.setAutoApply(false);
    Utils::FilePathAspect path(&page);
    path.setLabelText("Path");
    path.setExpectedKind(Utils::PathChooserKind::ExistingCommand);
    path.setBaseDirectory(dir.path());

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "StringDelegate"));

    const auto startFor = [&path](const QString &inTheField) {
        return path.browseStartDirectory(inTheField);
    };

    // A file in the field: beside the file, not at the file.
    QCOMPARE(Utils::FilePath::fromUserInput(startFor(file.toFSPathString())),
             sub);
    // A directory in the field: in it.
    QCOMPARE(Utils::FilePath::fromUserInput(startFor(sub.toFSPathString())), sub);
    // Nothing in the field: what paths here are relative to, which is what the
    // aspect was told and what only the presentation can say.
    QCOMPARE(Utils::FilePath::fromUserInput(startFor({})), dir.path());
    // Something that is not there at all: no answer rather than a made-up one,
    // and the dialog opens wherever it would have.
    QVERIFY(startFor("/no/such/place/at/all/file.txt").isEmpty()
            || Utils::FilePath::fromUserInput(startFor("/no/such/place/at/all/file.txt"))
                   == dir.path());
}

// A value shown as a label is elided when the row is too narrow for it, and
// then the tooltip is the only way to read the rest. Aspects showing a path
// ask for the value to be that tooltip; the widget renderer has always done
// it, and the Quick label carried the aspect's tooltip text and then never
// showed anything at all - ToolTip.visible was hard-coded false.
void QuickUiTest::testALabelSaysTheValueItCannotShowInFull()
{
    Utils::AspectContainer page;
    page.setAutoApply(false);

    Utils::StringAspect executable(&page);
    executable.setDisplayStyle(Utils::StringAspect::LabelDisplay);
    executable.setLabelText("Executable:");
    executable.setValue("/a/very/long/path/to/the/executable");
    executable.setToolTip("The program that runs");
    executable.setShowToolTipOnLabel(true);

    Utils::StringAspect version(&page);
    version.setDisplayStyle(Utils::StringAspect::LabelDisplay);
    version.setLabelText("Version:");
    version.setValue("2.6.1");
    version.setToolTip("What is installed");

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);

    QList<QQuickItem *> labels;
    QTRY_COMPARE((labels = findQmlComponents(quickWidget->rootObject(), "TextDisplayDelegate"))
                     .size(), 2);

    // Which delegate is which, by what it shows.
    const auto tooltipOfTheOneShowing = [&labels](const QString &displayed) {
        for (QQuickItem *label : labels) {
            if (label->property("displayText").toString() != displayed)
                continue;
            const QList<QQuickItem *> parts = findQmlComponents(label, "");
            for (QQuickItem *part : parts) {
                // Not the name in front of it: a FormLabel says its own text
                // when the column is too narrow for it, and what is asked
                // here is what the *value* says.
                if (QString::fromLatin1(part->metaObject()->className()).startsWith("FormLabel"))
                    continue;
                const QString text
                    = QQmlProperty(part, "ToolTip.text", qmlContext(part)).read().toString();
                if (!text.isEmpty())
                    return text;
            }
        }
        return QString("no tooltip anywhere in the delegate");
    };

    // The one that asked says what it is showing, which is the half that was
    // elided away.
    QCOMPARE(tooltipOfTheOneShowing("/a/very/long/path/to/the/executable"),
             QString("/a/very/long/path/to/the/executable"));
    // The one that did not says what it is for, as every other delegate does.
    QCOMPARE(tooltipOfTheOneShowing("2.6.1"), QString("What is installed"));
}

// Hovering over a path to a command shows what version it reports - what a
// compiler, a debugger or a cmake field says about itself. Fifteen aspects ask
// for it with setCommandVersionArguments(), and only the widget path chooser
// ever ran it.
//
// The hover itself is not what is tested: a hover test answers to the physical
// pointer as well as the synthetic one. This is what the hover asks for.
void QuickUiTest::testAPathToACommandSaysWhatVersionItIs()
{
    Utils::AspectContainer page;
    page.setAutoApply(false);
    Utils::FilePathAspect command(&page);
    command.setLabelText("Command");
    command.setExpectedKind(Utils::PathChooserKind::ExistingCommand);
    command.setToolTip("The tool to run");
    // A command that exists everywhere this builds and says something known.
    command.setCommandVersionArguments({"a version line"});

    // Nothing until it is asked for: it costs a process.
    QVERIFY(command.extendedToolTip().isEmpty());

    QSignalSpy answered(&command, &Utils::BaseAspect::extendedToolTipChanged);
    command.requestExtendedToolTip("/bin/echo");
    QTRY_VERIFY2(!command.extendedToolTip().isEmpty(), "the command was never run");
    QVERIFY2(command.extendedToolTip().contains("a version line"),
             qPrintable("it said: " + command.extendedToolTip()));
    QVERIFY(answered.size() > 0);

    // Asking again about the same command does not run it again.
    const int soFar = answered.size();
    command.requestExtendedToolTip("/bin/echo");
    QCOMPARE(answered.size(), soFar);

    // An aspect that asks for no version says nothing, which is what stops
    // every path field on every page from starting a process when the pointer
    // crosses it. Pointed at a command that prints something whether or not
    // it is given arguments: saying nothing then means it was never run, and
    // not merely that it had nothing to say.
    Utils::FilePathAspect plain(&page);
    plain.setLabelText("Directory");
    QSignalSpy plainAnswers(&plain, &Utils::BaseAspect::extendedToolTipChanged);
    plain.requestExtendedToolTip("/bin/pwd");

    // "Nothing happens" is not something to wait for, so wait for something
    // that must happen after it: the very command the aspect would have run,
    // run here. DataFromProcess answers these in the order they are asked
    // for, so by the time this one is back, one started before it would be
    // back too.
    bool ranItHere = false;
    Utils::DataFromProcess<QString>::Parameters params(
        Utils::CommandLine(Utils::FilePath::fromUserInput("/bin/pwd"), {}),
        [](const QString &out, const QString &) { return out; });
    params.callback = [&ranItHere](const std::optional<QString> &) { ranItHere = true; };
    Utils::DataFromProcess<QString>::provideData(params);
    QTRY_VERIFY2(ranItHere, "the command never ran here either, so this proves nothing");

    QVERIFY2(plain.extendedToolTip().isEmpty(),
             qPrintable("a field that asks for no version reported one: "
                        + plain.extendedToolTip()));
    QCOMPARE(plainAnswers.size(), 0);
}

// A colour aspect says whether its colour may be see-through. The widget
// picker asks before offering an alpha channel; the Quick delegate had red,
// green and blue and no alpha at all, so no colour's alpha could be edited and
// the two aspects that forbid one were right by accident.
void QuickUiTest::testAColourOffersAnAlphaOnlyWhenItIsAllowed()
{
    Utils::AspectContainer page;
    page.setAutoApply(false);
    Utils::ColorAspect translucent(&page);
    translucent.setLabelText("Overlay");
    translucent.setValue(QColor(10, 20, 30, 40));

    Utils::ColorAspect opaque(&page);
    opaque.setLabelText("Text");
    opaque.setValue(QColor(50, 60, 70));
    opaque.setAlphaAllowed(false);

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);

    QList<QQuickItem *> delegates;
    QTRY_COMPARE((delegates = findQmlComponents(quickWidget->rootObject(), "ColorDelegate"))
                     .size(), 2);
    const auto delegateFor = [&delegates](Utils::BaseAspect *aspect) -> QQuickItem * {
        for (QQuickItem *item : delegates) {
            if (item->property("aspect").value<Utils::BaseAspect *>() == aspect)
                return item;
        }
        return nullptr;
    };
    QQuickItem * const withAlpha = delegateFor(&translucent);
    QQuickItem * const withoutAlpha = delegateFor(&opaque);
    QVERIFY(withAlpha && withoutAlpha);
    QVERIFY(withAlpha->property("alphaAllowed").toBool());
    QVERIFY(!withoutAlpha->property("alphaAllowed").toBool());

    // Four spin boxes where alpha is allowed and three where it is not - and
    // the fourth is the one that can be seen.
    const auto visibleSpinBoxes = [](QQuickItem *delegate) {
        int count = 0;
        for (QQuickItem *part : findQmlComponents(delegate, "SpinBox")) {
            if (part->isVisible())
                ++count;
        }
        return count;
    };
    QCOMPARE(visibleSpinBoxes(withAlpha), 4);
    QCOMPARE(visibleSpinBoxes(withoutAlpha), 3);

    // And the alpha that is there is the colour's, not a default.
    QCOMPARE(qRound(withAlpha->property("alpha").toReal() * 255), 40);
}

// A font picker offers the families its aspect will accept. A terminal asks
// for monospaced ones; Qt.fontFamilies(), which the delegate used, offers
// every family there is.
void QuickUiTest::testAFontPickerOffersOnlyTheFamiliesTheAspectAccepts()
{
    Utils::AspectContainer page;
    page.setAutoApply(false);
    Utils::FontFamilyAspect anyFont(&page);
    anyFont.setLabelText("Any");

    Utils::FontFamilyAspect fixedFont(&page);
    fixedFont.setLabelText("Terminal");
    fixedFont.setFontFilters(Utils::FontFamilyAspect::MonospacedFonts);

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);

    QList<QQuickItem *> delegates;
    QTRY_COMPARE((delegates = findQmlComponents(quickWidget->rootObject(), "FontFamilyDelegate"))
                     .size(), 2);
    const auto familiesOf = [&delegates](Utils::BaseAspect *aspect) {
        for (QQuickItem *item : delegates) {
            if (item->property("aspect").value<Utils::BaseAspect *>() == aspect)
                return item->property("fontFamilies").toStringList();
        }
        return QStringList();
    };

    const QStringList all = familiesOf(&anyFont);
    const QStringList fixed = familiesOf(&fixedFont);
    QVERIFY2(!all.isEmpty(), "no font families at all");
    QVERIFY2(!fixed.isEmpty(), "no monospaced families - is there no fixed pitch font here?");
    QVERIFY2(fixed.size() < all.size(),
             "the monospaced picker offers every family, so it is not filtering");
    // Everything it offers really is fixed pitch, and something it leaves out
    // really is not: a filter that dropped an arbitrary half would pass the
    // count above.
    for (const QString &family : fixed)
        QVERIFY2(QFontDatabase::isFixedPitch(family), qPrintable(family + " is not monospaced"));
    const QStringList dropped = Utils::filtered(all, [&fixed](const QString &family) {
        return !fixed.contains(family);
    });
    QVERIFY(!dropped.isEmpty());
    for (const QString &family : dropped)
        QVERIFY2(!QFontDatabase::isFixedPitch(family), qPrintable(family + " was dropped anyway"));
}

// Browsing a filesystem without a view. The Quick file dialog is built on
// this, and it exists because QtQuick.Dialogs' FileDialog can only see the
// machine it runs on - a path aspect that allows a path from a device has
// nowhere to send the reader.
void QuickUiTest::testTheFileBrowserListsWhatIsInADirectory()
{
    Utils::TemporaryDirectory dir("quickui-filebrowser");
    QVERIFY(dir.isValid());
    const Utils::FilePath root = dir.path();
    const Utils::FilePath inner = root / "inner";
    QVERIFY(inner.createDir());
    QVERIFY((root / "one.cpp").writeFileContents("1"));
    QVERIFY((root / "two.txt").writeFileContents("2"));
    QVERIFY((root / ".hidden").writeFileContents("3"));

    QtcQuick::FileBrowser browser;
    QtcQuick::FileEntries * const entries = browser.entries();
    QVERIFY(entries);
    // The role names a Quick delegate reads. Without them the view draws
    // nothing and says nothing about why.
    const QList<QByteArray> roles = entries->roleNames().values();
    for (const QByteArray &role : {"name", "filePath", "isDir"})
        QVERIFY2(roles.contains(role), role.constData());

    browser.setDirectory(root.toUserOutput());
    QCOMPARE(browser.directory(), root.toUserOutput());

    // The listing is fetched in the background, so it arrives rather than
    // being there.
    const auto names = [entries] {
        QStringList found;
        for (int row = 0; row < entries->rowCount(); ++row)
            found << entries->data(entries->index(row, 0), QtcQuick::FileEntries::NameRole)
                         .toString();
        found.sort();
        return found;
    };
    QTRY_COMPARE(names(), QStringList({"inner", "one.cpp", "two.txt"}));

    // Hidden files are not shown until they are asked for.
    browser.setShowHiddenFiles(true);
    QTRY_COMPARE(names(), QStringList({".hidden", "inner", "one.cpp", "two.txt"}));
    browser.setShowHiddenFiles(false);
    QTRY_COMPARE(names(), QStringList({"inner", "one.cpp", "two.txt"}));

    // A name filter picks the files it names and leaves directories alone: a
    // directory is how the reader reaches a file that does match. Whether the
    // rest are hidden or listed and not offered is the reader's choice and
    // the host's default, so it is asked for here rather than assumed - see
    // testAFileTheFilterRejectsIsShownAndNotOffered.
    browser.setHideFilteredFiles(true);
    browser.setNameFilters({"*.cpp"});
    QTRY_COMPARE(names(), QStringList({"inner", "one.cpp"}));
    browser.setNameFilters({});

    // Going in and coming back out.
    QTRY_COMPARE(names(), QStringList({"inner", "one.cpp", "two.txt"}));
    int innerRow = -1;
    for (int row = 0; row < entries->rowCount(); ++row) {
        if (browser.filePathAt(row) == inner.toUserOutput())
            innerRow = row;
    }
    QVERIFY(innerRow >= 0);
    QVERIFY(browser.isDirectoryAt(innerRow));
    QVERIFY(browser.enter(innerRow));
    QCOMPARE(browser.directory(), inner.toUserOutput());
    QVERIFY(browser.canGoUp());
    browser.goUp();
    QCOMPARE(browser.directory(), root.toUserOutput());

    // A file cannot be entered - that is how a view knows to choose it
    // instead of navigating into it.
    int fileRow = -1;
    QTRY_VERIFY(entries->rowCount() == 3);
    for (int row = 0; row < entries->rowCount(); ++row) {
        if (browser.filePathAt(row).endsWith("one.cpp"))
            fileRow = row;
    }
    QVERIFY(fileRow >= 0);
    QVERIFY(!browser.isDirectoryAt(fileRow));
    QVERIFY(!browser.enter(fileRow));
    QCOMPARE(browser.directory(), root.toUserOutput());

    // What a typed name means: relative to what is being looked at, unless it
    // says otherwise.
    QCOMPARE(Utils::FilePath::fromUserInput(browser.resolve("one.cpp")), root / "one.cpp");
    QCOMPARE(Utils::FilePath::fromUserInput(browser.resolve(inner.toUserOutput())), inner);

    // And the places to start from include this machine's own.
    QVERIFY2(browser.places()->rowCount() > 0, "nowhere at all to start from");
}

// The dialog over that browser. Not shown here - what is checked is what it
// would choose, which is the part a platform dialog does for us and which
// therefore has to be right before anyone sees it.
void QuickUiTest::testTheFileDialogChoosesAFileWithoutAskingThePlatform()
{
    Utils::TemporaryDirectory dir("quickui-filedialog");
    QVERIFY(dir.isValid());
    const Utils::FilePath root = dir.path();
    const Utils::FilePath inner = root / "inner";
    QVERIFY(inner.createDir());
    QVERIFY((root / "chosen.txt").writeFileContents("x"));

    // A binding to something that is not there is a warning and nothing else:
    // the control keeps whatever it had and the dialog looks nearly right.
    // qmllint does not see through a singleton's property names, so this is
    // where a mistyped Metrics or Tokens entry is caught.
    QStringList complaints;
    const QMetaObject::Connection listening = connect(
        QtcQuick::engine(), &QQmlEngine::warnings, QtcQuick::engine(),
        [&complaints](const QList<QQmlError> &warnings) {
            for (const QQmlError &warning : warnings) {
                if (warning.url().toString().contains("QtcFileDialog.qml"))
                    complaints << warning.toString();
            }
        });
    const QScopeGuard stopListening([listening] { disconnect(listening); });

    QQmlComponent component(QtcQuick::engine(),
                            QUrl("qrc:/qt/qml/QtCreator/Ui/QtcFileDialog.qml"));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    const std::unique_ptr<QObject> dialog(component.create());
    QVERIFY(dialog);

    dialog->setProperty("currentFolder", root.toUserOutput());
    auto * const browser = dialog->findChild<QtcQuick::FileBrowser *>("fileBrowser");
    QVERIFY(browser);
    QTRY_COMPARE(browser->entries()->rowCount(), 2);

    // Nothing picked yet, so there is nothing to accept: a dialog whose Open
    // button is live with no answer would hand back an empty path.
    QVERIFY(dialog->property("wouldChooseAll").toStringList().isEmpty());

    // Picking the file by name is what typing into the field does - in the
    // arrangement that has one. The other names a file only when saving, so
    // this asks for the classic one rather than depending on the host.
    dialog->setProperty("classic", true);
    QObject * const nameField = dialog->findChild<QObject *>("nameField");
    QVERIFY(nameField);
    nameField->setProperty("text", "chosen.txt");
    QCOMPARE(dialog->property("wouldChooseAll").toStringList(),
             QStringList({(root / "chosen.txt").toUserOutput()}));

    QSignalSpy accepted(dialog.get(), SIGNAL(accepted(QStringList)));
    QMetaObject::invokeMethod(dialog.get(), "accept");
    QCOMPARE(accepted.size(), 1);
    // Always a list, even here where only one can be chosen.
    QCOMPARE(accepted.at(0).at(0).toStringList().size(), 1);
    QCOMPARE(Utils::FilePath::fromUserInput(accepted.at(0).at(0).toStringList().first()),
             root / "chosen.txt");
    QCOMPARE(Utils::FilePath::fromUserInput(dialog->property("selectedFile").toString()),
             root / "chosen.txt");

    // A dialog asking for a directory answers with the one being looked at,
    // which is how "choose this folder" works with nothing selected in it.
    const std::unique_ptr<QObject> forDirectory(component.create());
    QVERIFY(forDirectory);
    forDirectory->setProperty("mode", 1); // QtcFileDialog.OpenDirectory
    forDirectory->setProperty("currentFolder", inner.toUserOutput());
    QCOMPARE(forDirectory->property("wouldChooseAll").toStringList(),
             QStringList({inner.toUserOutput()}));

    // And cancelling says so and chooses nothing.
    QSignalSpy rejected(forDirectory.get(), SIGNAL(rejected()));
    QMetaObject::invokeMethod(forDirectory.get(), "reject");
    QCOMPARE(rejected.size(), 1);
    QVERIFY(forDirectory->property("selectedFile").toString().isEmpty());

    QVERIFY2(complaints.isEmpty(), qPrintable("\n" + complaints.join("\n")));
}

// A settings page whose QML is a file rather than a resource. This is what a
// Lua extension gets: it ships its page beside its init.lua and names it with
// PluginSpec.pluginDirectory, so the URL is file:// and the QML is read from
// disk. Nothing else in the tree does that, and the engine is shared with the
// built-in pages, so it is worth knowing the import still resolves.
void QuickUiTest::testAPageCanBeDrawnByQmlThatIsNotBuiltIn()
{
    Utils::TemporaryDirectory dir("quickui-extension-page");
    QVERIFY(dir.isValid());
    const Utils::FilePath qml = dir.path() / "Settings.qml";
    QVERIFY(qml.writeFileContents(R"(
import QtQuick
import QtCreator.Ui

AspectPage {
    id: root

    StringDelegate { aspect: root.aspects.binary }
    ButtonDelegate { aspect: root.aspects.install }
}
)"));

    Utils::AspectContainer page;
    page.setAutoApply(false);
    page.setQmlSource(QUrl::fromLocalFile(qml.toFSPathString()));

    Utils::FilePathAspect binary(&page);
    binary.setSettingsKey("Extension.Binary");
    binary.setLabelText("Binary:");
    binary.setExpectedKind(Utils::PathChooserKind::ExistingCommand);
    // The name the page reaches it by, which for a Lua aspect is the name the
    // script assigned it to rather than the tail of its settings key.
    binary.setQmlName("binary");

    int triggered = 0;
    Utils::ActionAspect install(&page);
    install.setQmlName("install");
    install.setActionText("Install it");
    install.setAction([&triggered] { ++triggered; });

    const std::unique_ptr<QWidget> form(showOwnPage(&page));
    QVERIFY2(form, "a page whose QML is a file on disk was not rendered");
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QQuickItem * const root = quickWidget->rootObject();
    QVERIFY2(root, qPrintable(Utils::transform(quickWidget->errors(),
                                               &QQmlError::toString).join("; ")));

    // Drawn by the file, not by the generic form falling back.
    QVERIFY2(QString::fromLatin1(root->metaObject()->className()).startsWith("Settings"),
             "the page did not render the QML it named");

    // Both delegates found their aspect: a name that does not exist is
    // undefined in QML rather than an error, so an unbound delegate is the
    // failure this has to catch.
    const QList<QQuickItem *> delegates = findAspectDelegates(root);
    QCOMPARE(delegates.size(), 2);
    for (QQuickItem *delegate : delegates) {
        QVERIFY2(delegate->property("aspect").value<Utils::BaseAspect *>(),
                 "a delegate on the page was given no aspect");
    }

    // And the action reaches the script's function. An ActionAspect is how a
    // page acts without a layout to hang a button off.
    QQuickItem * const button = findButton(root, "Install it");
    QVERIFY2(button, "the action aspect drew no button");
    QMetaObject::invokeMethod(button, "clicked");
    QCOMPARE(triggered, 1);
}

// The two extensions Qt Creator ships draw their settings with Qt Quick, and
// nothing else looks at them: the Lua plugin does not load its extensions in a
// test run, so they are not in the page census. What can go wrong without
// anyone noticing is a name - the QML asks for aspects.binary, the script
// assigns Settings.binary, and a mismatch is undefined in QML rather than an
// error, leaving a delegate that draws nothing.
void QuickUiTest::testTheShippedExtensionPagesNameAspectsTheirScriptsCreate()
{
    const Utils::FilePath extensions = Core::ICore::resourcePath("lua-plugins");
    QVERIFY(extensions.isDir());
    // One directory per extension, so the pages are one level down.
    const Utils::FilePaths pages = extensions.dirEntries(
        Utils::FileFilter({"Settings.qml"},
                          Utils::DirFilterFlag::Files,
                          Utils::DirIteratorFlag::Subdirectories));
    // Found by walking, so an empty walk must not pass for agreement.
    QVERIFY2(!pages.isEmpty(), "no extension ships a Qt Quick settings page");

    for (const Utils::FilePath &page : pages) {
        // It parses, and every type it names exists. A page that does not is
        // an extension whose settings are blank.
        QQmlComponent component(QtcQuick::engine(), page.toUrl());
        QVERIFY2(!component.isError(), qPrintable(page.toUserOutput() + ": "
                                                  + component.errorString()));

        const Utils::FilePath script = page.parentDir() / "init.lua";
        const Utils::Result<QByteArray> lua = script.fileContents();
        QVERIFY2(lua, qPrintable(page.toUserOutput() + " has no init.lua beside it"));
        const QString source = QString::fromUtf8(*lua);

        const Utils::Result<QByteArray> qml = page.fileContents();
        QVERIFY(qml);
        static const QRegularExpression named("aspects\\.([A-Za-z_][A-Za-z0-9_]*)");
        QRegularExpressionMatchIterator it = named.globalMatch(QString::fromUtf8(*qml));
        int checked = 0;
        while (it.hasNext()) {
            const QString name = it.next().captured(1);
            // The script assigns it into the container under that name, which
            // is what the binding turns into the aspect's QML name.
            const QRegularExpression assigned(
                "Settings\\." + QRegularExpression::escape(name) + "\\s*=");
            QVERIFY2(source.contains(assigned),
                     qPrintable(page.toUserOutput() + " draws aspects." + name
                                + ", which init.lua never creates"));
            ++checked;
        }
        QVERIFY2(checked > 0, qPrintable(page.toUserOutput() + " names no aspect at all"));
    }
}



// What happens to a file the name filters do not match. The widget dialog
// hides it or lists it greyed and unselectable, and which of the two is the
// default depends on the host - off macOS it hides them. The Quick browser
// hid them always, so on a Mac the two dialogs listed different files.
void QuickUiTest::testAFileTheFilterRejectsIsShownAndNotOffered()
{
    Utils::TemporaryDirectory dir("quickui-filtered");
    QVERIFY(dir.isValid());
    const Utils::FilePath root = dir.path();
    QVERIFY((root / "kept.txt").writeFileContents("x"));
    QVERIFY((root / "other.md").writeFileContents("x"));
    QVERIFY((root / "sub").createDir());

    QtcQuick::FileBrowser browser;
    // The default is the widget's, which is the host's.
    QCOMPARE(browser.hideFilteredFiles(), !Utils::HostOsInfo::isMacHost());

    browser.setNameFilters({"*.txt"});
    browser.setDirectory(root.toUserOutput());
    QtcQuick::FileEntries * const entries = browser.entries();
    QVERIFY(entries);

    const auto rowsNamed = [entries] {
        QStringList names;
        for (int row = 0; row < entries->rowCount(); ++row) {
            names << entries->data(entries->index(row, 0),
                                   QtcQuick::FileEntries::NameRole).toString();
        }
        names.sort();
        return names;
    };
    const auto selectable = [entries](const QString &name) {
        for (int row = 0; row < entries->rowCount(); ++row) {
            const QModelIndex index = entries->index(row, 0);
            if (index.data(QtcQuick::FileEntries::NameRole).toString() == name)
                return index.data(QtcQuick::FileEntries::SelectableRole).toBool();
        }
        return false;
    };

    // Hiding them: the listing is what matches, plus the directories that are
    // how you reach anything at all.
    browser.setHideFilteredFiles(true);
    QTRY_COMPARE(rowsNamed(), QStringList({"kept.txt", "sub"}));

    // Showing them: the file is there, and it cannot be chosen. Both halves
    // matter - listing it and offering it would let the reader pick a file
    // the caller said it does not take.
    browser.setHideFilteredFiles(false);
    QTRY_COMPARE(rowsNamed(), QStringList({"kept.txt", "other.md", "sub"}));
    QVERIFY2(!selectable("other.md"), "a file the filter rejects can be chosen");
    QVERIFY2(selectable("kept.txt"), "a file the filter accepts cannot be chosen");
    // A directory is how you reach a file, so no filter ever applies to one.
    QVERIFY2(selectable("sub"), "a directory was filtered out by a file filter");

    // And the listing draws the difference rather than only knowing it.
    QQmlComponent component(QtcQuick::engine(),
                            QUrl("qrc:/qt/qml/QtCreator/Ui/QtcFileDialog.qml"));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    const std::unique_ptr<QObject> object(component.createWithInitialProperties(
        {{"classic", true}, {"nameFilter", "Text (*.txt)"}}));
    QVERIFY(object);
    auto * const dialog = qobject_cast<QQuickWindow *>(object.get());
    QVERIFY(dialog);
    QObject * const shown = dialog->findChild<QObject *>("fileBrowser");
    QVERIFY(shown);
    shown->setProperty("hideFilteredFiles", false);
    dialog->setProperty("currentFolder", root.toUserOutput());
    dialog->resize(900, 600);
    dialog->show();
    QVERIFY(QTest::qWaitForWindowExposed(dialog));

    QQuickItem * const listing = dialog->findChild<QQuickItem *>("entryList");
    QVERIFY(listing);
    const auto rowFor = [listing](const QString &name) -> QQuickItem * {
        const QList<QQuickItem *> rows = findQmlComponents(listing, "ItemDelegate");
        return Utils::findOr(rows, nullptr, [&name](QQuickItem *row) {
            return row->property("name").toString() == name;
        });
    };
    QQuickItem *rejected = nullptr;
    QTRY_VERIFY((rejected = rowFor("other.md")));
    QVERIFY2(!rejected->isEnabled(), "a file the filter rejects is offered anyway");
    QQuickItem * const accepted = rowFor("kept.txt");
    QVERIFY(accepted);
    QVERIFY2(accepted->isEnabled(), "a file the filter accepts is not offered");

    // The menu says which it is doing, and only where there is a filter.
    QObject * const item = dialog->findChild<QObject *>("hideFilteredItem");
    QVERIFY(item);
    QVERIFY(item->property("enabled").toBool());
    QCOMPARE(item->property("checked").toBool(), false);
}


// Getting out of a directory that is several deep. Up goes one level at a
// time; the widget dialog's path is a combo box whose entries are the
// directory and everything above it, so any of them is one click away. The
// Quick dialog had a field to type a path into and nothing else.
void QuickUiTest::testEveryEnclosingFolderIsSomewhereToGo()
{
    Utils::TemporaryDirectory dir("quickui-ancestors");
    QVERIFY(dir.isValid());
    const Utils::FilePath deep = dir.path() / "one" / "two" / "three";
    QVERIFY(deep.ensureWritableDir());

    QtcQuick::FileBrowser browser;
    browser.setDirectory(deep.toUserOutput());
    QtcQuick::FileEntries * const chain = browser.ancestors();
    QVERIFY(chain);

    // Nearest first, starting with where the reader is, and every one of them
    // is a real directory above it.
    QVERIFY2(chain->rowCount() >= 4, "the chain does not reach past the directory itself");
    const auto at = [chain](int row, QtcQuick::FileEntries::Role role) {
        return chain->data(chain->index(row, 0), role).toString();
    };
    QCOMPARE(at(0, QtcQuick::FileEntries::NameRole), QString("three"));
    QCOMPARE(at(1, QtcQuick::FileEntries::NameRole), QString("two"));
    QCOMPARE(at(2, QtcQuick::FileEntries::NameRole), QString("one"));
    QCOMPARE(at(0, QtcQuick::FileEntries::FilePathRole), deep.toUserOutput());
    // The last one is a root, which has no name of its own and is shown as
    // the path it is rather than as nothing.
    const QString top = at(chain->rowCount() - 1, QtcQuick::FileEntries::NameRole);
    QVERIFY2(!top.isEmpty(), "the topmost enclosing folder is drawn as an empty entry");

    // It follows the reader about rather than being built once.
    browser.setDirectory((dir.path() / "one").toUserOutput());
    QTRY_COMPARE(at(0, QtcQuick::FileEntries::NameRole), QString("one"));

    // And the dialog offers them, by name or by path as the reader asked.
    QQmlComponent component(QtcQuick::engine(),
                            QUrl("qrc:/qt/qml/QtCreator/Ui/QtcFileDialog.qml"));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    const std::unique_ptr<QObject> object(
        component.createWithInitialProperties({{"classic", true}}));
    QVERIFY(object);
    auto * const dialog = qobject_cast<QQuickWindow *>(object.get());
    QVERIFY(dialog);
    dialog->setProperty("currentFolder", deep.toUserOutput());
    dialog->resize(900, 600);
    dialog->show();
    QVERIFY(QTest::qWaitForWindowExposed(dialog));

    QQuickItem * const button = dialog->findChild<QQuickItem *>("ancestorsButton");
    QVERIFY2(button, "nothing offers the enclosing folders");
    QVERIFY(button->isEnabled());
    QObject * const menu = dialog->findChild<QObject *>("ancestorsMenu");
    QVERIFY(menu);
    // A popup is not in the item tree, and entries a Repeater made are not
    // children of the menu either - they go into its content model. So the
    // menu is asked for them by index.
    const auto entryTexts = [menu] {
        QStringList texts;
        const int count = menu->property("count").toInt();
        for (int i = 0; i < count; ++i) {
            QQuickItem *item = nullptr;
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, item),
                                      Q_ARG(int, i));
            if (item)
                texts << item->property("text").toString();
        }
        return texts;
    };
    QStringList texts;
    QTRY_VERIFY((texts = entryTexts()).size() >= 4);
    QVERIFY2(texts.contains("three"), qPrintable("it offered: " + texts.join(", ")));
    QVERIFY2(texts.contains("one"), qPrintable("it offered: " + texts.join(", ")));

    // The same choice the widget dialog words as "Show full paths in
    // ComboBox": the folders are then listed as what they are, not what they
    // are called - two directories called "src" are otherwise one entry twice.
    dialog->setProperty("showingFullPaths", true);
    QTRY_VERIFY(entryTexts().contains(deep.toUserOutput()));
    QObject * const item = dialog->findChild<QObject *>("fullPathsItem");
    QVERIFY(item);
    QVERIFY(item->property("checked").toBool());
}


// The search field. Its magnifier was drawn at twice its size - an Image with
// no size of its own draws whatever the provider hands back - and there was
// no way at all to clear a search with the pointer. The field it replaces has
// a magnifier on one side and a clear button on the other; here they share
// the corner, since one is for an empty field and the other never is.
void QuickUiTest::testASearchCanBeUndoneWithoutTheKeyboard()
{
    QQmlComponent component(QtcQuick::engine(),
                            QUrl("qrc:/qt/qml/QtCreator/Ui/QtcFileDialog.qml"));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    const std::unique_ptr<QObject> object(
        component.createWithInitialProperties({{"classic", true}}));
    QVERIFY(object);
    auto * const dialog = qobject_cast<QQuickWindow *>(object.get());
    QVERIFY(dialog);
    dialog->setProperty("currentFolder", QDir::tempPath());
    dialog->resize(900, 600);
    dialog->show();
    QVERIFY(QTest::qWaitForWindowExposed(dialog));

    QQuickItem * const search = dialog->findChild<QQuickItem *>("searchBox");
    QVERIFY(search);
    QQuickItem * const magnifier = dialog->findChild<QQuickItem *>("searchIcon");
    QQuickItem * const clear = dialog->findChild<QQuickItem *>("clearButton");
    QVERIFY(magnifier && clear);

    // Drawn at the size the rest of the dialog draws icons at, not at the
    // size the provider happens to hand back.
    QTRY_COMPARE(magnifier->property("status").toInt(), 1);
    QCOMPARE(magnifier->property("paintedWidth").toReal(), qreal(16));

    // Empty: the magnifier says what the field is for, and there is nothing
    // to clear.
    QCOMPARE(search->property("text").toString(), QString());
    QVERIFY(magnifier->isVisible());
    QVERIFY2(!clear->isVisible(), "an empty search offers to be cleared");

    // Typed in: the field says what it holds, and offers to undo it. They
    // share a corner, so both being visible would draw one over the other.
    search->setProperty("text", "needle");
    QTRY_VERIFY(clear->isVisible());
    QVERIFY2(!magnifier->isVisible(), "the magnifier is drawn over the clear button");
    QCOMPARE(clear->property("paintedWidth").toReal(), qreal(16));

    // And it reaches the browser, which is what a search is for.
    QObject * const browser = dialog->findChild<QObject *>("fileBrowser");
    QVERIFY(browser);
    QTRY_COMPARE(browser->property("searchText").toString(), QString("needle"));

    // Clicking it puts the field back and stops the search. Clicked rather
    // than cleared from here: the point of the button is that the pointer can
    // undo a search, and setting the text would pass without it.
    const QPointF centre = clear->mapToScene(
        QPointF(clear->width() / 2, clear->height() / 2));
    QTest::mouseClick(dialog, Qt::LeftButton, Qt::NoModifier, centre.toPoint());
    QTRY_COMPARE(search->property("text").toString(), QString());
    QTRY_VERIFY(magnifier->isVisible());
    QVERIFY(!clear->isVisible());
    QTRY_COMPARE(browser->property("searchText").toString(), QString());

    // The browser also ends a search on its own when the directory changes,
    // and the box has to stop saying one is on.
    search->setProperty("text", "needle");
    QTRY_COMPARE(browser->property("searchText").toString(), QString("needle"));
    dialog->setProperty("currentFolder", QDir::homePath());
    QTRY_COMPARE(search->property("text").toString(), QString());
    QTRY_VERIFY(magnifier->isVisible());
}






// What every table on every settings page is laid out by, and what nothing
// checked: columnWidthProvider. qmllint reports it as "expected function got
// double" - it is reading the function's return paths - and behind that
// warning the rule had never been exercised at all. A provider that returned
// nothing useful would divide the width evenly, which is the very thing it
// was written to stop: a check box column as wide as a description.
void QuickUiTest::testATableGivesEachColumnTheWidthItsContentsNeed()
{
    Utils::AspectContainer page;
    TestTableAspect table(&page);
    table.setLabelText("Rows");

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QQuickItem *view = nullptr;
    QTRY_VERIFY(view = tableViewOf(quickWidget->rootObject()));
    QTRY_COMPARE(view->property("rows").toInt(), 2);
    const int columns = view->property("columns").toInt();
    QCOMPARE(columns, int(TestTableModel::ColumnCount));

    // columnWidth() is a method of TableView rather than a property, and
    // invokeMethod fails silently when a name is wrong - every width would
    // then read as the same -1 and "not evenly divided" would pass for the
    // wrong reason.
    const auto widthOf = [view](int column) {
        qreal width = -1;
        const bool asked = QMetaObject::invokeMethod(
            view, "columnWidth", Q_RETURN_ARG(qreal, width), Q_ARG(int, column));
        return asked ? width : qreal(-1);
    };

    QList<qreal> widths;
    for (int column = 0; column < columns; ++column)
        widths << widthOf(column);
    for (qreal width : std::as_const(widths))
        QVERIFY2(width > 0, "a column has no width, so nothing below means anything");

    // Not an even division. That is what a table without a provider does, and
    // it gave a check box as much room as a word.
    const qreal even = view->width() / columns;
    QVERIFY2(!Utils::allOf(widths, [even](qreal width) { return qFuzzyCompare(width, even); }),
             "every column is the same width, so the provider never ran");
    // The last one is the stretcher; the rest are the width of a control.
    // Sized to the *cell*, not to the text: an unfilled check box, choice and
    // field are all one lineEditWidth, which is why "the check box column is
    // narrower" is not the rule here - the even division is.
    QVERIFY2(widths.last() > 2 * widths.first(),
             "the last column did not take what was left");

    // The last column takes what is left, the way the widget table stretched
    // its last header section: together they are the width of the view.
    qreal total = 0;
    for (qreal width : std::as_const(widths))
        total += width;
    QVERIFY2(qAbs(total - view->width()) < 2,
             qPrintable(QString("the columns come to %1 in a table %2 wide")
                            .arg(total).arg(view->width())));

    // And a column whose contents are far too wide is capped instead of
    // pushing the table wider than itself - which scrolled the centred
    // heading out of the clipped header and left the column looking unnamed.
    table.m_model.setRows({{true, "red", QString(400, 'x')}});
    QTRY_COMPARE(view->property("rows").toInt(), 1);
    QQuickItem * const delegate = findQmlComponent(quickWidget->rootObject(), "TableDelegate");
    QVERIFY(delegate);
    QMetaObject::invokeMethod(view, "forceLayout");

    qreal wide = 0;
    QTRY_VERIFY((wide = widthOf(TestTableModel::ColumnWord)) > 0);
    // The cap is reached rather than assumed: the column asks for far more
    // than it is given, so "no wider than the cap" is a claim about the cap
    // and not about a column that was small anyway.
    qreal asked = -1;
    QVERIFY(QMetaObject::invokeMethod(view, "implicitColumnWidth", Q_RETURN_ARG(qreal, asked),
                                      Q_ARG(int, int(TestTableModel::ColumnWord))));
    QVERIFY2(asked > 320, qPrintable(QString("the column only asked for %1, so the cap "
                                             "was never tested").arg(asked)));
    QVERIFY2(wide <= 320,
             qPrintable(QString("a column of 400 characters was given %1").arg(wide)));
    // Still exactly as wide as the view: the cap is what keeps it there.
    qreal cappedTotal = 0;
    for (int column = 0; column < columns; ++column)
        cappedTotal += widthOf(column);
    QVERIFY2(qAbs(cappedTotal - view->width()) < 2,
             qPrintable(QString("a capped table comes to %1 in a view %2 wide")
                            .arg(cappedTotal).arg(view->width())));
}

// The two things a list-with-details does that qmllint cannot check, and that
// nothing else here checked either: it draws the selected item's own aspects
// underneath, and it refuses to remove an item that is already on its way
// out.
//
// Both reach through view.currentItem, which is a QQuickItem to qmllint - so
// "Member itemModel not found" and "Member removed not found" are limitations
// rather than defects. But both are read with ?., so a name that stopped
// resolving would show an empty sub-form and an always-enabled Remove, and
// say nothing. That is the shape the file dialog's dead click had.
void QuickUiTest::testAListShowsTheItemYouSelectedAndRefusesToRemoveTwice()
{
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

    const QList<QQuickItem *> views = findQmlComponents(delegate, "QQuickListView");
    QVERIFY2(!views.isEmpty(), "the list draws no rows at all");
    QQuickItem * const view = views.first();

    // The sub-form is the selected item's own aspects. Read off what it draws
    // rather than from the model: an item model that never arrives leaves the
    // loader empty and nothing complains.
    const auto shownName = [delegate, view]() -> QString {
        const QList<QQuickItem *> fields = findQmlComponents(delegate, "StringDelegate");
        for (QQuickItem *field : fields) {
            // The rows themselves hold no field; only the sub-form does.
            if (field->property("aspect").value<Utils::BaseAspect *>())
                return field->property("aspect").value<Utils::BaseAspect *>()->variantValue()
                           .toString();
        }
        Q_UNUSED(view)
        return {};
    };

    view->setProperty("currentIndex", 0);
    QTRY_COMPARE(shownName(), QString("first"));
    view->setProperty("currentIndex", 2);
    QTRY_COMPARE(shownName(), QString("third"));

    // Removing keeps the row - struck through until it is applied - so it can
    // be selected again, and then Remove has nothing left to do.
    QQuickItem *remove = nullptr;
    QTRY_VERIFY(remove = findButton(delegate, "Remove"));
    QVERIFY(remove->isEnabled());
    QMetaObject::invokeMethod(remove, "clicked");

    QTRY_COMPARE(rowLabels(delegate), QStringList({"first", "second", "third"}));
    view->setProperty("currentIndex", 2);
    QTRY_VERIFY2(!remove->isEnabled(),
                 "an item already removed can be removed a second time");
    // And an item that is not on its way out can still go.
    view->setProperty("currentIndex", 0);
    QTRY_VERIFY2(remove->isEnabled(), "an item that is still there cannot be removed");
}

// The two modes that were never compared with the widget dialog: saving, and
// picking several at once.
//
// updateAcceptButtonState() trims before deciding whether Save can be
// pressed, and the views are set to ExtendedSelection when several files may
// be picked - so Shift takes a run of them, which the Quick listing only knew
// how to do one Ctrl-click at a time.
void QuickUiTest::testSavingAndPickingSeveralFollowTheSameRulesAsTheWidget()
{
    Utils::TemporaryDirectory dir("quickui-modes");
    QVERIFY(dir.isValid());
    const Utils::FilePath root = dir.path();
    for (const QString &name : QStringList{"a.txt", "b.txt", "c.txt", "d.txt"})
        QVERIFY((root / name).writeFileContents("x"));

    const auto dialogFor = [](int mode) -> QQuickWindow * {
        QQmlComponent component(QtcQuick::engine(),
                                QUrl("qrc:/qt/qml/QtCreator/Ui/QtcFileDialog.qml"));
        QTC_ASSERT(!component.isError(), qDebug() << component.errorString(); return nullptr);
        return qobject_cast<QQuickWindow *>(component.createWithInitialProperties(
            {{"classic", true}, {"mode", mode}}));
    };

    // Saving. A name of nothing but spaces is not a name: the widget trims
    // before offering Save, and saves what was typed rather than the trim.
    {
        // SaveFile, as QtcFileDialog numbers its modes.
        const std::unique_ptr<QObject> owner(dialogFor(2));
        auto * const dialog = qobject_cast<QQuickWindow *>(owner.get());
        QVERIFY(dialog);
        dialog->setProperty("currentFolder", root.toUserOutput());
        QQuickItem * const field = dialog->findChild<QQuickItem *>("nameField");
        QVERIFY(field);

        QCOMPARE(dialog->property("wouldChooseAll").toStringList().size(), 0);
        field->setProperty("text", "   ");
        QCOMPARE(dialog->property("typedName").toString(), QString("   "));
        QVERIFY2(dialog->property("wouldChooseAll").toStringList().isEmpty(),
                 "a name of nothing but spaces would be saved");

        field->setProperty("text", " notes.txt ");
        const QStringList chosen = dialog->property("wouldChooseAll").toStringList();
        QCOMPARE(chosen.size(), 1);
        // What was typed, not what it was tested as: trimming is the test, and
        // the widget saves the text the reader gave it.
        QVERIFY2(chosen.first().endsWith(" notes.txt "),
                 qPrintable("it would save " + chosen.first()));
    }

    // Picking several. Shift takes the run between where the reader is and
    // where they clicked.
    {
        // OpenFiles.
        const std::unique_ptr<QObject> owner(dialogFor(3));
        auto * const dialog = qobject_cast<QQuickWindow *>(owner.get());
        QVERIFY(dialog);
        dialog->setProperty("currentFolder", root.toUserOutput());
        dialog->resize(900, 600);
        dialog->show();
        QVERIFY(QTest::qWaitForWindowExposed(dialog));

        QQuickItem * const listing = dialog->findChild<QQuickItem *>("entryList");
        QVERIFY(listing);
        QList<QQuickItem *> rows;
        QTRY_VERIFY((rows = findQmlComponents(listing, "ItemDelegate")).size() >= 4);
        std::sort(rows.begin(), rows.end(), [](QQuickItem *a, QQuickItem *b) {
            return a->property("index").toInt() < b->property("index").toInt();
        });
        const auto clickRow = [dialog](QQuickItem *row, Qt::KeyboardModifiers mods) {
            const QPointF centre = row->mapToScene(
                QPointF(row->width() / 2, row->height() / 2));
            QTest::mouseClick(dialog, Qt::LeftButton, mods, centre.toPoint());
        };

        clickRow(rows.at(0), Qt::NoModifier);
        QTRY_COMPARE(dialog->property("wouldChooseAll").toStringList().size(), 1);

        // Shift to the fourth: all four, not just the two ends.
        clickRow(rows.at(3), Qt::ShiftModifier);
        QStringList several;
        QTRY_COMPARE((several = dialog->property("wouldChooseAll").toStringList()).size(), 4);
        QVERIFY2(several.first() != several.last(), "the run is one file four times");

        // Ctrl still adds one at a time, and does not extend a run.
        clickRow(rows.at(0), Qt::NoModifier);
        QTRY_COMPARE(dialog->property("wouldChooseAll").toStringList().size(), 1);
        clickRow(rows.at(2), Qt::ControlModifier);
        QTRY_COMPARE(dialog->property("wouldChooseAll").toStringList().size(), 1);
    }
}

// Being asked for a directory. The widget dialog keeps listing the files -
// they are what tells one directory from another - and stops them being an
// answer: its proxy hides rows in filterAcceptsRow() and greys them in
// acceptsContent(), and only the second one knows about directory mode. The
// Quick dialog listed them as ordinary, choosable entries.
void QuickUiTest::testChoosingADirectoryStillShowsWhatIsInIt()
{
    Utils::TemporaryDirectory dir("quickui-directories");
    QVERIFY(dir.isValid());
    const Utils::FilePath root = dir.path();
    QVERIFY((root / "notes.txt").writeFileContents("x"));
    QVERIFY((root / "sub").createDir());

    QtcQuick::FileBrowser browser;
    browser.setDirectory(root.toUserOutput());
    QtcQuick::FileEntries * const entries = browser.entries();
    QVERIFY(entries);
    const auto rowsNamed = [entries] {
        QStringList names;
        for (int row = 0; row < entries->rowCount(); ++row) {
            names << entries->data(entries->index(row, 0),
                                   QtcQuick::FileEntries::NameRole).toString();
        }
        names.sort();
        return names;
    };
    const auto selectable = [entries](const QString &name) {
        for (int row = 0; row < entries->rowCount(); ++row) {
            const QModelIndex index = entries->index(row, 0);
            if (index.data(QtcQuick::FileEntries::NameRole).toString() == name)
                return index.data(QtcQuick::FileEntries::SelectableRole).toBool();
        }
        return false;
    };

    QTRY_COMPARE(rowsNamed(), QStringList({"notes.txt", "sub"}));
    QVERIFY(selectable("notes.txt"));

    // Asked for a directory: the file is still listed, and is no longer an
    // answer. Both halves matter - hiding it would take away what tells this
    // directory apart from the next one.
    browser.setDirectoriesOnly(true);
    QTRY_COMPARE(rowsNamed(), QStringList({"notes.txt", "sub"}));
    QVERIFY2(!selectable("notes.txt"), "a file can be chosen when a directory was asked for");
    QVERIFY2(selectable("sub"), "a directory cannot be chosen when one was asked for");

    // Hiding filtered files is about the filter, not about the mode: being
    // asked for a directory must not empty the listing.
    browser.setHideFilteredFiles(true);
    QTRY_COMPARE(rowsNamed(), QStringList({"notes.txt", "sub"}));

    // And the dialog asks for it when that is what it is for.
    QQmlComponent component(QtcQuick::engine(),
                            QUrl("qrc:/qt/qml/QtCreator/Ui/QtcFileDialog.qml"));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    // OpenDirectory, as QtcFileDialog numbers its modes.
    const std::unique_ptr<QObject> object(component.createWithInitialProperties(
        {{"classic", true}, {"mode", 1}}));
    QVERIFY(object);
    auto * const dialog = qobject_cast<QQuickWindow *>(object.get());
    QVERIFY(dialog);
    dialog->setProperty("currentFolder", root.toUserOutput());
    dialog->resize(900, 600);
    dialog->show();
    QVERIFY(QTest::qWaitForWindowExposed(dialog));

    QObject * const shown = dialog->findChild<QObject *>("fileBrowser");
    QVERIFY(shown);
    QVERIFY2(shown->property("directoriesOnly").toBool(),
             "a dialog asking for a directory did not say so to its browser");

    QQuickItem * const listing = dialog->findChild<QQuickItem *>("entryList");
    QVERIFY(listing);
    const auto rowFor = [listing](const QString &name) -> QQuickItem * {
        const QList<QQuickItem *> rows = findQmlComponents(listing, "ItemDelegate");
        return Utils::findOr(rows, nullptr, [&name](QQuickItem *row) {
            return row->property("name").toString() == name;
        });
    };
    QQuickItem *file = nullptr;
    QTRY_VERIFY((file = rowFor("notes.txt")));
    QVERIFY2(!file->isEnabled(), "a file is offered when a directory was asked for");
    QQuickItem * const folder = rowFor("sub");
    QVERIFY(folder);
    QVERIFY(folder->isEnabled());
}

// The options menu, which the widget dialog builds from checkable actions:
// one entry per view rather than one that rewords itself, and each says
// whether it is the one in use.
//
// The trap is that triggering a checkable MenuItem writes its own checked,
// which throws away the binding that made it follow anything. Nothing
// complains; the entry simply stops tracking, and only after it has been used
// once - which no test that opens a fresh dialog would ever see.
void QuickUiTest::testTheOptionsMenuKeepsSayingWhatIsOn()
{
    Utils::TemporaryDirectory dir("quickui-options");
    QVERIFY(dir.isValid());
    for (const QString &name : QStringList{"a.txt", "b.txt", "c.txt", "d.txt"})
        QVERIFY((dir.path() / name).writeFileContents("x"));

    QQmlComponent component(QtcQuick::engine(),
                            QUrl("qrc:/qt/qml/QtCreator/Ui/QtcFileDialog.qml"));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    const std::unique_ptr<QObject> object(
        component.createWithInitialProperties({{"classic", true}}));
    QVERIFY(object);
    auto * const dialog = qobject_cast<QQuickWindow *>(object.get());
    QVERIFY(dialog);
    dialog->setProperty("currentFolder", dir.path().toUserOutput());
    dialog->resize(900, 600);
    dialog->show();
    QVERIFY(QTest::qWaitForWindowExposed(dialog));

    const auto item = [dialog](const QString &name) {
        return dialog->findChild<QObject *>(name);
    };
    QObject * const icons = item("iconsViewItem");
    QObject * const list = item("listViewItem");
    QVERIFY(icons && list);
    QVERIFY(icons->property("checkable").toBool());
    QVERIFY(list->property("checkable").toBool());

    // Exactly one of them is on, and it is the view being shown.
    QVERIFY(!icons->property("checked").toBool());
    QVERIFY(list->property("checked").toBool());
    dialog->setProperty("showingIcons", true);
    QTRY_VERIFY(icons->property("checked").toBool());
    QVERIFY(!list->property("checked").toBool());

    // Choosing the other one switches, which is the ordinary case.
    triggerMenuItem(list);
    QTRY_VERIFY(!dialog->property("showingIcons").toBool());
    QVERIFY(list->property("checked").toBool());
    QVERIFY2(!icons->property("checked").toBool(),
             "both views are marked as the one in use");

    // And choosing the one already in use leaves it marked. A checkable entry
    // flips itself on every click and the handler then writes a value that
    // has not changed, so nothing re-evaluates what marks it: the entry ends
    // up saying the opposite of what is true, while the view is unmoved.
    triggerMenuItem(list);
    QVERIFY2(!dialog->property("showingIcons").toBool(),
             "choosing the view already in use switched away from it");
    QVERIFY2(list->property("checked").toBool(),
             "choosing the view already in use unmarked it");

    // Hidden files: the widget's wording, checkable rather than reworded, and
    // its eye.
    QObject * const hidden = item("hiddenItem");
    QVERIFY(hidden);
    QCOMPARE(hidden->property("text").toString(), QString("Show hidden files"));
    QVERIFY(hidden->property("checkable").toBool());
    QVERIFY2(!QQmlProperty::read(hidden, "icon.source").toUrl().isEmpty(),
             "the entry the widget dialog draws an eye beside has no icon");
    triggerMenuItem(hidden);
    QObject * const browser = dialog->findChild<QObject *>("fileBrowser");
    QVERIFY(browser);
    QTRY_VERIFY(browser->property("showHiddenFiles").toBool());
    // And follows the setting when something else changes it - what a plain
    // toggle writes always changes, so it needs no help to stay in step.
    browser->setProperty("showHiddenFiles", false);
    QTRY_VERIFY2(!hidden->property("checked").toBool(),
                 "the entry stopped following the setting it stands for");

    // The listing shades every other row, as the widget's view does. Read off
    // the rows rather than from the rule: a stripe the same colour as the
    // background is not a stripe.
    dialog->setProperty("showingIcons", false);
    QQuickItem * const listing = dialog->findChild<QQuickItem *>("entryList");
    QVERIFY(listing);
    QList<QQuickItem *> rows;
    QTRY_VERIFY((rows = findQmlComponents(listing, "ItemDelegate")).size() >= 2);
    std::sort(rows.begin(), rows.end(), [](QQuickItem *a, QQuickItem *b) {
        return a->property("index").toInt() < b->property("index").toInt();
    });
    const auto shade = [](QQuickItem *row) {
        const QList<QQuickItem *> parts = findQmlComponents(row, "QQuickRectangle");
        return parts.isEmpty() ? QColor() : parts.first()->property("color").value<QColor>();
    };
    QVERIFY2(shade(rows.at(0)) != shade(rows.at(1)),
             "the listing draws every row the same shade");
}

// Fails unless \a item really draws an icon: an Image under it that has
// loaded something with a size. Reading the iconSource property instead
// passes for an item that was handed an icon and drops it, which is the
// mistake this guards.
static void drawsAnIcon(QQuickItem *item, const QString &what)
{
    const QList<QQuickItem *> images = findQmlComponents(item, "QQuickImage");
    QVERIFY2(!images.isEmpty(), qPrintable(what + " has no image to draw an icon in"));
    const bool drawn = Utils::anyOf(images, [](QQuickItem *image) {
        return !image->property("source").toUrl().isEmpty()
               // Image.Ready, which is not a public C++ enum.
               && image->property("status").toInt() == 1
               && image->property("paintedWidth").toReal() > 0;
    });
    QVERIFY2(drawn, qPrintable(what + " draws no icon"));
}





// What a macro expander offers, as rows. It used to live inside
// VariableChooser, reachable only by the QTreeView that widget builds - which
// is why the forms that offer "Insert variable" are the ones still drawn with
// widgets. Out on its own it can be read by anything, and a Qt Quick view
// reads a model by its role names, so those are what this checks.
void QuickUiTest::testTheVariablesAMacroExpanderOffersAreAModel()
{
    Utils::MacroExpander expander;
    expander.setDisplayName("Test");
    expander.registerVariable("Test:Answer", "The answer", [] { return QString("42"); });

    Utils::VariableModel model;
    model.addMacroExpanderProvider(Utils::MacroExpanderProvider(&expander));

    // One group per provider, and the variables under it once it is asked.
    QCOMPARE(model.rowCount(), 1);
    const QModelIndex group = model.index(0, 0);
    QCOMPARE(group.data(Qt::DisplayRole).toString(), QString("Test"));
    if (model.canFetchMore(group))
        model.fetchMore(group);
    QVERIFY2(model.rowCount(group) > 0, "a group that offers variables listed none");

    QModelIndex answer;
    for (int row = 0; row < model.rowCount(group); ++row) {
        const QModelIndex candidate = model.index(row, 0, group);
        if (candidate.data(Qt::DisplayRole).toString() == "Test:Answer")
            answer = candidate;
    }
    QVERIFY2(answer.isValid(), "the variable that was registered is not in the model");

    // The three things a chooser needs of a row: what to insert, what it
    // stands for, and what to say about it.
    QCOMPARE(answer.data(Utils::VariableModel::UnexpandedTextRole).toString(),
             QString("%{Test:Answer}"));
    QCOMPARE(answer.data(Utils::VariableModel::ExpandedTextRole).toString(), QString("42"));
    QVERIFY(answer.data(Utils::VariableModel::CurrentValueDisplayRole)
                .toString().contains("The answer"));

    // Named, so a Qt Quick view can ask for them. A model without these is
    // one a QML delegate cannot read at all - and "display" is among them
    // because that is the name the stock tree delegate draws its label from.
    const QHash<int, QByteArray> names = model.roleNames();
    for (const char *name : {"display", "unexpandedText", "expandedText", "currentValue",
                             "selectable"}) {
        QVERIFY2(names.values().contains(QByteArray(name)),
                 qPrintable(QString("no role is called %1").arg(name)));
    }

    // The variable being edited is listed and cannot be chosen, so that a
    // field cannot be made to expand itself.
    QVERIFY(answer.flags() & Qt::ItemIsEnabled);
    model.setCurrentVariableName("Test:Answer");
    QVERIFY2(!(answer.flags() & Qt::ItemIsEnabled),
             "a variable can be inserted into the field that defines it");
}

// A form that lists a container's aspects keeps the hidden ones. Build
// settings depend on it: the build directory's warnings appear as you type
// and the Qt Quick compiler row when the kit changes, and an aspect left out
// when the page was built could never appear at all. The widget form said so
// in a comment and added every aspect by hand; the generic Qt Quick form is
// what draws it now, so the same thing has to be true of the model.
void QuickUiTest::testAFormKeepsAnAspectThatIsHiddenForNow()
{
    Utils::AspectContainer page;
    page.setAutoApply(false);

    Utils::BoolAspect always(&page);
    always.setSettingsKey("Always");
    always.setLabelText("Always here");

    Utils::BoolAspect later(&page);
    later.setSettingsKey("Later");
    later.setLabelText("Not yet");
    later.setVisible(false);

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QQuickItem * const root = quickWidget->rootObject();
    QVERIFY(root);

    // Both are built. The hidden one is drawn and not shown, which is what
    // lets it appear later without the form being made again.
    const auto delegateFor = [root](Utils::BaseAspect *aspect) -> QQuickItem * {
        const QList<QQuickItem *> delegates = findAspectDelegates(root);
        return Utils::findOr(delegates, nullptr, [aspect](QQuickItem *item) {
            return item->property("aspect").value<Utils::BaseAspect *>() == aspect;
        });
    };
    QQuickItem *hidden = nullptr;
    QTRY_VERIFY2((hidden = delegateFor(&later)),
                 "an aspect that starts hidden was left out of the form");
    QVERIFY(delegateFor(&always));
    QVERIFY2(!hidden->isVisible(), "an aspect that is hidden is drawn anyway");

    // And it arrives when the aspect says so - no rebuilding, which is the
    // whole point.
    later.setVisible(true);
    QTRY_VERIFY2(hidden->isVisible(),
                 "an aspect that stopped being hidden never appeared");
    QCOMPARE(delegateFor(&later), hidden);

    // And the door a caller outside this library comes through: a container
    // with no page of its own to name still gets the Qt Quick form, which is
    // what a build configuration asks for.
    const std::unique_ptr<QWidget> throughCore(Core::createGenericAspectForm(&page));
    QVERIFY2(throughCore, "a container with no page of its own got no form at all");
    QVERIFY2(throughCore->findChild<QQuickWidget *>(),
             "the generic form came back as a widget layout");
}

// A search field that is empty says what it is. Utils::QtcSearchBox gets
// "Filter" from FancyLineEdit's filtering mode; the Quick one said nothing,
// which is only visible beside the widget it mirrors - the two were grabbed
// side by side to find it.
void QuickUiTest::testASearchBoxSaysWhatItIsWhenItIsEmpty()
{
    QQmlComponent component(QtcQuick::engine(),
                            QUrl("qrc:/qt/qml/QtCreator/Ui/QtcSearchBox.qml"));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    const std::unique_ptr<QObject> box(component.create());
    QVERIFY(box);
    QCOMPARE(box->property("placeholderText").toString(), QString("Filter"));

    // And a caller with a better word keeps it: the file dialog searches
    // rather than filters, so its field must not be relabelled by this.
    QQmlComponent dialogComponent(QtcQuick::engine(),
                                  QUrl("qrc:/qt/qml/QtCreator/Ui/QtcFileDialog.qml"));
    QVERIFY2(!dialogComponent.isError(), qPrintable(dialogComponent.errorString()));
    const std::unique_ptr<QObject> dialog(dialogComponent.create());
    QVERIFY(dialog);
    QQuickItem * const search = dialog->findChild<QQuickItem *>("searchBox");
    QVERIFY(search);
    QCOMPARE(search->property("placeholderText").toString(), QString("Search"));
}

// Turning the listing round. The widget dialog's header sorts on one key
// whichever column is clicked - FileSortRole is directories first, then name
// without regard to case - so what a click really changes is the direction,
// and the caret is what says which way it is being read.
void QuickUiTest::testTheListingCanBeReadBothWaysRound()
{
    Utils::TemporaryDirectory dir("quickui-sorting");
    QVERIFY(dir.isValid());
    const Utils::FilePath root = dir.path();
    for (const QString &name : QStringList{"alpha.txt", "beta.txt", "gamma.txt"})
        QVERIFY((root / name).writeFileContents("x"));
    QVERIFY((root / "adir").createDir());

    QtcQuick::FileBrowser browser;
    browser.setDirectory(root.toUserOutput());
    QtcQuick::FileEntries * const entries = browser.entries();
    const auto listing = [entries] {
        QStringList names;
        for (int row = 0; row < entries->rowCount(); ++row) {
            names << entries->data(entries->index(row, 0),
                                   QtcQuick::FileEntries::NameRole).toString();
        }
        return names;
    };

    // Directories first, then by name: the order the widget dialog lists in.
    QTRY_COMPARE(listing(), QStringList({"adir", "alpha.txt", "beta.txt", "gamma.txt"}));
    QVERIFY(!browser.sortDescending());

    // The other way round is the same key read backwards, so the directory
    // goes last rather than staying pinned at the top.
    browser.setSortDescending(true);
    QTRY_COMPARE(listing(), QStringList({"gamma.txt", "beta.txt", "alpha.txt", "adir"}));
    browser.setSortDescending(false);
    QTRY_COMPARE(listing(), QStringList({"adir", "alpha.txt", "beta.txt", "gamma.txt"}));

    // The dialog's heading is what turns it, and the arrow says which way.
    QQmlComponent component(QtcQuick::engine(),
                            QUrl("qrc:/qt/qml/QtCreator/Ui/QtcFileDialog.qml"));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    const std::unique_ptr<QObject> object(
        component.createWithInitialProperties({{"classic", true}}));
    QVERIFY(object);
    auto * const dialog = qobject_cast<QQuickWindow *>(object.get());
    QVERIFY(dialog);
    dialog->setProperty("currentFolder", root.toUserOutput());
    dialog->resize(900, 600);
    dialog->show();
    QVERIFY(QTest::qWaitForWindowExposed(dialog));

    QObject * const shown = dialog->findChild<QObject *>("fileBrowser");
    QVERIFY(shown);
    QQuickItem * const heading = dialog->findChild<QQuickItem *>("nameHeading");
    QVERIFY2(heading, "the listing has no heading to sort by");
    QQuickItem * const caret = dialog->findChild<QQuickItem *>("sortIndicator");
    QVERIFY2(caret, "nothing says which way the listing is read");

    const QUrl ascending = caret->property("source").toUrl();
    QVERIFY(!ascending.isEmpty());
    const QPointF middle = heading->mapToScene(
        QPointF(heading->width() / 2, heading->height() / 2));
    QTest::mouseClick(dialog, Qt::LeftButton, Qt::NoModifier, middle.toPoint());
    QTRY_VERIFY2(shown->property("sortDescending").toBool(),
                 "clicking the heading did not turn the listing round");
    QTRY_VERIFY2(caret->property("source").toUrl() != ascending,
                 "the arrow points the same way whichever way the listing reads");

    // And back, because it is the same heading.
    QTest::mouseClick(dialog, Qt::LeftButton, Qt::NoModifier, middle.toPoint());
    QTRY_VERIFY(!shown->property("sortDescending").toBool());
    QCOMPARE(caret->property("source").toUrl(), ascending);
}

// What the widget file dialog looks like, in the places where the Quick one
// had drifted: a row of glyphs rather than a row of words, a sidebar that
// says what each place is, and the name and the kind of file on a row each.
//
// Appearance is not usually worth a test. This is, because every one of these
// failed *silently*: setting ItemDelegate.icon.source draws nothing at all -
// the style's delegate is a bare Text - and a page whose icons have quietly
// become labels still passes every behavioural test in this file.
void QuickUiTest::testTheDialogLooksLikeTheOneItReplaces()
{
    QQmlComponent component(QtcQuick::engine(),
                            QUrl("qrc:/qt/qml/QtCreator/Ui/QtcFileDialog.qml"));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    const std::unique_ptr<QObject> object(
        component.createWithInitialProperties({{"classic", true}}));
    QVERIFY(object);
    auto * const dialog = qobject_cast<QQuickWindow *>(object.get());
    QVERIFY(dialog);
    dialog->setProperty("currentFolder", QDir::homePath());
    dialog->resize(900, 600);
    dialog->show();
    QVERIFY(QTest::qWaitForWindowExposed(dialog));
    QQuickItem * const root = dialog->contentItem();

    // The sidebar's three headings, which is how the widget dialog groups
    // them: what the reader kept, where this machine keeps things, and what
    // is not this machine.
    const auto shows = [root](const QString &text) {
        const QList<QQuickItem *> labels = findQmlComponents(root, "Label");
        return Utils::anyOf(labels, [&text](QQuickItem *label) {
            return label->property("text").toString() == text && label->isVisible();
        });
    };
    QVERIFY2(shows("Favorites"), "the sidebar does not head what the reader kept");
    QVERIFY2(shows("Locations"), "the sidebar does not head this machine's places");
    QVERIFY2(shows("Devices"), "the sidebar lists no devices under their own heading");

    // And every entry says what it is. The model has handed out an icon all
    // along; nothing drew it.
    QList<QQuickItem *> rows;
    QTRY_VERIFY((rows = findQmlComponents(root, "SidebarRow")).size() >= 4);
    for (QQuickItem *row : std::as_const(rows)) {
        // What is drawn, not what the row was told: an icon assigned to
        // something that does not draw it leaves the property set and the
        // sidebar bare, which is exactly how this went wrong.
        drawsAnIcon(row, "a sidebar entry: " + row->property("text").toString());
    }

    // The toolbar is glyphs. A row of four words where the widget dialog has
    // four icons is the first thing that tells the two apart.
    const QStringList glyphs{"backButton", "forwardButton", "upButton",
                             "gotoButton", "optionsButton"};
    for (const QString &name : glyphs) {
        QQuickItem * const button = dialog->findChild<QQuickItem *>(name);
        QVERIFY2(button, qPrintable(name + " is not on the toolbar"));
        drawsAnIcon(button, name);
        QVERIFY2(button->property("text").toString().isEmpty(),
                 qPrintable(name + " is drawn as a word"));
    }
    // Typing a path is offered where the path is not already a field to type
    // in, which is the arrangement this dialog is not in.
    QVERIFY2(!dialog->findChild<QQuickItem *>("gotoButton")->isVisible(),
             "the classic arrangement offers a button for what its field does");

    // The toolbar reads in the widget's order: the view options sit with the
    // other glyphs, before the path, not after it.
    QQuickItem * const gear = dialog->findChild<QQuickItem *>("optionsButton");
    QQuickItem * const path = dialog->findChild<QQuickItem *>("pathField");
    QVERIFY(gear && path);
    QVERIFY2(gear->mapToScene(QPointF(0, 0)).x() < path->mapToScene(QPointF(0, 0)).x(),
             "the view options are drawn after the path rather than before it");

    // The search glyph takes the leading side, as the field it replaces does.
    QQuickItem * const search = dialog->findChild<QQuickItem *>("searchBox");
    QQuickItem * const glyph = dialog->findChild<QQuickItem *>("searchIcon");
    QVERIFY(search && glyph);
    QVERIFY2(glyph->mapToItem(search, QPointF(0, 0)).x() < search->width() / 2,
             "the search glyph is drawn on the trailing side");

    // How much room the places get is the reader's, as the widget's splitter
    // makes it.
    QVERIFY2(dialog->findChild<QQuickItem *>("sidebarSplit"),
             "the sidebar cannot be resized");

    // Making a folder is a button of its own, not only a menu entry.
    QQuickItem * const newFolder = dialog->findChild<QQuickItem *>("newFolderButton");
    QVERIFY(newFolder);
    QVERIFY2(newFolder->isVisible(), "making a folder is hidden in a menu");

    // The name and the kind are on a row each. Side by side, "Files of type:"
    // reads as the unit of the field in front of it.
    QQuickItem * const nameField = dialog->findChild<QQuickItem *>("nameField");
    QQuickItem * const filterBox = dialog->findChild<QQuickItem *>("filterBox");
    QVERIFY(nameField && filterBox);
    QVERIFY(nameField->isVisible() && filterBox->isVisible());
    const QPointF named = nameField->mapToItem(root, {0, 0});
    const QPointF kind = filterBox->mapToItem(root, {0, 0});
    QVERIFY2(kind.y() > named.y() + nameField->height() / 2,
             "the name of the file and the kind of it are drawn on one row");
}

// The window our own file dialog is, if one is open. It is a window with no
// parent, made and destroyed with the browsing, so it is found among the
// windows rather than under an item.
static QQuickWindow *deviceBrowseWindow()
{
    for (QWindow *window : QGuiApplication::topLevelWindows()) {
        if (QString::fromLatin1(window->metaObject()->className()).startsWith("QtcFileDialog"))
            return qobject_cast<QQuickWindow *>(window);
    }
    return nullptr;
}

// Which dialog a path field opens. The platform's cannot see a device, so a
// field holding a path on one has to be browsed with Qt Creator's own - the
// rule the widget path chooser follows, which no Quick page did.
void QuickUiTest::testAPathOnADeviceIsBrowsedWithOurOwnDialog()
{
    Utils::AspectContainer page;
    page.setAutoApply(false);
    Utils::FilePathAspect path(&page);
    path.setLabelText("Path");
    path.setExpectedKind(Utils::PathChooserKind::ExistingCommand);

    Utils::FilePathAspect localOnly(&page);
    localOnly.setLabelText("Here only");
    localOnly.setExpectedKind(Utils::PathChooserKind::ExistingCommand);
    localOnly.setAllowPathFromDevice(false);

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QList<QQuickItem *> delegates;
    QTRY_COMPARE((delegates = findQmlComponents(quickWidget->rootObject(), "StringDelegate"))
                     .size(), 2);
    const auto delegateFor = [&delegates](Utils::BaseAspect *aspect) -> QQuickItem * {
        for (QQuickItem *item : delegates) {
            if (item->property("aspect").value<Utils::BaseAspect *>() == aspect)
                return item;
        }
        return nullptr;
    };
    QQuickItem * const anywhere = delegateFor(&path);
    QQuickItem * const hereOnly = delegateFor(&localOnly);
    QVERIFY(anywhere && hereOnly);

    // What the aspect says reaches the form. It defaults to allowing one, so
    // the field that forbids one is the interesting half.
    QVERIFY2(anywhere->property("allowsDevicePaths").toBool(),
             "the field does not know it may take a path from a device");
    QVERIFY2(!hereOnly->property("allowsDevicePaths").toBool(),
             "a field that refuses device paths says it takes them");

    // And the browser behind our dialog does reach a device path, which is
    // the whole reason for having one: it works in Utils::FilePath, so a
    // device root is a directory like any other.
    QtcQuick::AspectModels models;
    QVERIFY(models.isLocalPath(QDir::homePath()));
    QVERIFY2(!models.isLocalPath("docker://nosuchimage/tmp"),
             "a path on a device is taken for one on this machine, so the "
             "platform dialog would be opened for it");

    // A path that is not on a device yet is reached by asking. The widget
    // path chooser hung Local and Remote off the browse button; the field
    // does the same, and offers it only where a device path would be taken.
    auto * const options = anywhere->findChild<QQuickItem *>("browseOptionsButton");
    QVERIFY(options);
    QVERIFY2(options->isVisible(),
             "a field that takes a device path does not offer to browse one");
    auto * const noOptions = hereOnly->findChild<QQuickItem *>("browseOptionsButton");
    QVERIFY(noOptions);
    QVERIFY2(!noOptions->isVisible(),
             "a field that refuses device paths offers to browse one anyway");

    // And asking opens ours, from a local path, where the rule alone would
    // have left it to the platform. Only this half can be tried: the other
    // opens the platform's own dialog, which on this host is modal and real.
    QVERIFY(!deviceBrowseWindow());
    QMetaObject::invokeMethod(anywhere, "browse", Q_ARG(bool, true));
    QQuickWindow *dialog = nullptr;
    QTRY_VERIFY2((dialog = deviceBrowseWindow()),
                 "asking for Remote did not open the dialog that reaches a device");
    dialog->close();
    dialog->deleteLater();
    QTRY_VERIFY(!deviceBrowseWindow());
}

// Getting about: back and forward over where the reader has been, and the
// directories they kept. Kept where the widget dialog keeps them, so that a
// favourite added in one is there in the other.
void QuickUiTest::testTheFileBrowserRemembersWhereItHasBeen()
{
    Utils::TemporaryDirectory dir("quickui-browser-history");
    QVERIFY(dir.isValid());
    const Utils::FilePath root = dir.path();
    const Utils::FilePath first = root / "first";
    const Utils::FilePath second = root / "second";
    QVERIFY(first.createDir());
    QVERIFY(second.createDir());

    QtcQuick::FileBrowser browser;
    // Nowhere to go back to before anywhere has been visited.
    QVERIFY(!browser.canGoBack());
    QVERIFY(!browser.canGoForward());

    browser.setDirectory(root.toUserOutput());
    QVERIFY(!browser.canGoBack());
    browser.setDirectory(first.toUserOutput());
    QVERIFY(browser.canGoBack());
    QVERIFY(!browser.canGoForward());

    browser.goBack();
    QCOMPARE(browser.directory(), root.toUserOutput());
    QVERIFY(browser.canGoForward());
    browser.goForward();
    QCOMPARE(browser.directory(), first.toUserOutput());

    // Going somewhere new from part way back drops what was ahead: there is
    // no coming forward to a place the reader turned away from.
    browser.goBack();
    browser.setDirectory(second.toUserOutput());
    QCOMPARE(browser.directory(), second.toUserOutput());
    QVERIFY2(!browser.canGoForward(), "forward still leads to the road not taken");
    // And back leads to where the reader came from, which is the directory
    // they turned away from "first" at - not "first" itself. Asking only
    // whether forward is possible does not say that: an entry left behind
    // ends up behind the new one rather than ahead of it.
    browser.goBack();
    QCOMPARE(browser.directory(), root.toUserOutput());

    // Favourites, about the directory being looked at - so stand in one.
    // Cleaned up afterwards, because they are the user's own settings and
    // this is a test.
    browser.setDirectory(second.toUserOutput());
    QCOMPARE(browser.directory(), second.toUserOutput());
    const QStringList before = [] {
        QSettings settings;
        settings.beginGroup("FileDialog");
        return settings.value("Favorites").toStringList();
    }();
    const QScopeGuard putBack([before] {
        QSettings settings;
        settings.beginGroup("FileDialog");
        settings.setValue("Favorites", before);
    });

    QVERIFY2(!browser.currentIsFavorite(), "a fresh directory is already a favourite");
    browser.addFavorite(second.toUserOutput());
    QVERIFY(browser.currentIsFavorite());
    QCOMPARE(browser.favorites()->rowCount(), 1);
    QCOMPARE(Utils::FilePath::fromUserInput(
                 browser.favorites()
                     ->data(browser.favorites()->index(0, 0), QtcQuick::FileEntries::FilePathRole)
                     .toString()),
             second);

    // The order is the reader's own and is kept: a second favourite can be
    // moved above the first, and it is still above it next time.
    const Utils::FilePath alsoKept = root / "first";
    browser.addFavorite(alsoKept.toUserOutput());
    const auto keptOrder = [&browser] {
        QStringList paths;
        for (int row = 0; row < browser.favorites()->rowCount(); ++row) {
            paths << browser.favorites()
                         ->data(browser.favorites()->index(row, 0),
                                QtcQuick::FileEntries::FilePathRole)
                         .toString();
        }
        return paths;
    };
    QCOMPARE(keptOrder(), QStringList({second.toUserOutput(), alsoKept.toUserOutput()}));
    browser.moveFavorite(1, 0);
    QCOMPARE(keptOrder(), QStringList({alsoKept.toUserOutput(), second.toUserOutput()}));
    // Moving nowhere, or off the end, changes nothing rather than losing one.
    browser.moveFavorite(0, 0);
    browser.moveFavorite(1, 5);
    browser.moveFavorite(-1, 0);
    QCOMPARE(keptOrder(), QStringList({alsoKept.toUserOutput(), second.toUserOutput()}));
    {
        QSettings settings;
        settings.beginGroup("FileDialog");
        QCOMPARE(settings.value("Favorites").toStringList().first(),
                 alsoKept.toFSPathString());
    }
    browser.removeFavorite(alsoKept.toUserOutput());

    // Written where the widget dialog reads them.
    {
        QSettings settings;
        settings.beginGroup("FileDialog");
        QVERIFY2(settings.value("Favorites").toStringList().contains(second.toFSPathString()),
                 "the widget dialog would not see this favourite");
    }

    browser.removeFavorite(second.toUserOutput());
    QVERIFY(!browser.currentIsFavorite());
    QCOMPARE(browser.favorites()->rowCount(), 0);

    // And making a directory here, which is the other thing a dialog has to
    // be able to do before a file can be saved into it.
    const QString made = browser.createDirectory("brand-new");
    QCOMPARE(Utils::FilePath::fromUserInput(made), second / "brand-new");
    QVERIFY((second / "brand-new").isDir());
}

// Looking for a file rather than walking to it. The widget dialog swaps its
// listing for the hits while a search is on; so does this, so a view needs to
// know nothing about it.
void QuickUiTest::testTheFileBrowserFindsFilesBelowTheDirectory()
{
    Utils::TemporaryDirectory dir("quickui-browser-search");
    QVERIFY(dir.isValid());
    const Utils::FilePath root = dir.path();
    const Utils::FilePath deeper = root / "deeper";
    QVERIFY(deeper.createDir());
    QVERIFY((root / "needle.txt").writeFileContents("x"));
    QVERIFY((deeper / "another-needle.txt").writeFileContents("x"));
    QVERIFY((deeper / "haystack.txt").writeFileContents("x"));

    QtcQuick::FileBrowser browser;
    browser.setDirectory(root.toUserOutput());
    QtcQuick::FileEntries * const entries = browser.entries();
    QTRY_COMPARE(entries->rowCount(), 2);

    const auto found = [entries] {
        QStringList names;
        for (int row = 0; row < entries->rowCount(); ++row)
            names << entries->data(entries->index(row, 0), QtcQuick::FileEntries::NameRole)
                         .toString();
        names.sort();
        return names;
    };

    browser.setSearchText("needle");
    // Found below as well as here, and named by where it is: two files called
    // the same thing in different directories are one list.
    QTRY_COMPARE(found(), QStringList({"deeper/another-needle.txt", "needle.txt"}));
    QTRY_VERIFY(!browser.isSearching());

    // Clearing it goes back to what is in the directory.
    browser.setSearchText("");
    QTRY_COMPARE(found(), QStringList({"deeper", "needle.txt"}));

    // And going somewhere else ends the search rather than showing hits from
    // a directory that is no longer the one being looked at.
    browser.setSearchText("needle");
    QTRY_VERIFY(entries->rowCount() > 0);
    browser.setDirectory(deeper.toUserOutput());
    QCOMPARE(browser.searchText(), QString());
    QTRY_COMPARE(found(), QStringList({"another-needle.txt", "haystack.txt"}));
}

// An aspect's dialog filter names several kinds of file - "Sources (*.cpp
// *.h);;All files (*)" - and the reader picks one. Handing the browser every
// group's patterns at once instead would merge them, and a group of "*" makes
// every other group pointless.
void QuickUiTest::testTheFileDialogOffersEachKindOfFileSeparately()
{
    Utils::TemporaryDirectory dir("quickui-dialog-filters");
    QVERIFY(dir.isValid());
    const Utils::FilePath root = dir.path();
    QVERIFY((root / "code.cpp").writeFileContents("x"));
    QVERIFY((root / "code.h").writeFileContents("x"));
    QVERIFY((root / "notes.txt").writeFileContents("x"));

    QQmlComponent component(QtcQuick::engine(),
                            QUrl("qrc:/qt/qml/QtCreator/Ui/QtcFileDialog.qml"));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    const std::unique_ptr<QObject> dialog(component.create());
    QVERIFY(dialog);

    // A group naming several patterns, which is the usual shape of one and
    // the only shape that tells a group from a pattern.
    dialog->setProperty("nameFilter", "Sources (*.cpp *.h);;All files (*)");
    dialog->setProperty("currentFolder", root.toUserOutput());
    auto * const browser = dialog->findChild<QtcQuick::FileBrowser *>("fileBrowser");
    QVERIFY(browser);
    // What each group names, not what becomes of the files it does not: on
    // this host the listing keeps them by default.
    browser->setHideFilteredFiles(true);

    // Both groups are on offer, and the first one is what is being shown.
    QCOMPARE(dialog->property("filterGroups").toList().size(), 2);
    QCOMPARE(browser->nameFilters(), QStringList({"*.cpp", "*.h"}));
    QtcQuick::FileEntries * const entries = browser->entries();
    const auto names = [entries] {
        QStringList found;
        for (int row = 0; row < entries->rowCount(); ++row)
            found << entries->data(entries->index(row, 0), QtcQuick::FileEntries::NameRole)
                         .toString();
        found.sort();
        return found;
    };
    QTRY_COMPARE(names(), QStringList({"code.cpp", "code.h"}));

    // Picking the other one shows what it names.
    QObject * const filterBox = dialog->findChild<QObject *>("filterBox");
    QVERIFY(filterBox);
    filterBox->setProperty("currentIndex", 1);
    QCOMPARE(browser->nameFilters(), QStringList({"*"}));
    QTRY_COMPARE(names(), QStringList({"code.cpp", "code.h", "notes.txt"}));

    // With nothing to choose between, the box is not in the way.
    dialog->setProperty("nameFilter", "Sources (*.cpp *.h)");
    QCOMPARE(browser->nameFilters(), QStringList({"*.cpp", "*.h"}));
    QVERIFY2(!filterBox->property("visible").toBool(),
             "a choice of one is offered as a choice");
}

// A list of paths - include paths, suppression files - is added to several at
// a time. The platform dialog does that and ours has to as well, or the Quick
// dialog is a worse answer than the one it replaces on a device.
void QuickUiTest::testTheFileDialogCanChooseSeveralFilesAtOnce()
{
    Utils::TemporaryDirectory dir("quickui-dialog-several");
    QVERIFY(dir.isValid());
    const Utils::FilePath root = dir.path();
    QVERIFY((root / "one.txt").writeFileContents("x"));
    QVERIFY((root / "two.txt").writeFileContents("x"));
    QVERIFY((root / "three.txt").writeFileContents("x"));
    const Utils::FilePath elsewhere = root / "elsewhere";
    QVERIFY(elsewhere.createDir());
    QVERIFY((elsewhere / "unrelated.txt").writeFileContents("x"));

    QQmlComponent component(QtcQuick::engine(),
                            QUrl("qrc:/qt/qml/QtCreator/Ui/QtcFileDialog.qml"));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    const std::unique_ptr<QObject> dialog(component.create());
    QVERIFY(dialog);

    dialog->setProperty("mode", 3); // QtcFileDialog.OpenFiles
    dialog->setProperty("currentFolder", root.toUserOutput());
    auto * const browser = dialog->findChild<QtcQuick::FileBrowser *>("fileBrowser");
    QVERIFY(browser);
    QTRY_COMPARE(browser->entries()->rowCount(), 4);

    // Two of the three, picked the way holding Ctrl or Command picks them.
    const auto rowOf = [browser](const QString &name) {
        for (int row = 0; row < browser->entries()->rowCount(); ++row) {
            if (browser->filePathAt(row).endsWith(name))
                return row;
        }
        return -1;
    };
    const int first = rowOf("one.txt");
    const int third = rowOf("three.txt");
    QVERIFY(first >= 0 && third >= 0);
    dialog->setProperty("alsoPicked", QVariantList({first, third}));

    const QStringList would = dialog->property("wouldChooseAll").toStringList();
    QCOMPARE(would.size(), 2);
    QVERIFY(would.contains((root / "one.txt").toUserOutput()));
    QVERIFY(would.contains((root / "three.txt").toUserOutput()));

    // What was picked is picked by row, so going somewhere else has to forget
    // it: rows 0 and 2 of another directory are other files entirely, and
    // accepting would quietly hand those back instead.
    dialog->setProperty("currentFolder", elsewhere.toUserOutput());
    QTRY_COMPARE(browser->entries()->rowCount(), 1);
    QCOMPARE(dialog->property("alsoPicked").toList().size(), 0);
    QVERIFY2(!dialog->property("wouldChooseAll").toStringList().contains(
                 (root / "one.txt").toUserOutput()),
             "a file picked in another directory is still being offered");
    dialog->setProperty("currentFolder", root.toUserOutput());
    QTRY_COMPARE(browser->entries()->rowCount(), 4);
    dialog->setProperty("alsoPicked",
                        QVariantList({rowOf("one.txt"), rowOf("three.txt")}));

    QSignalSpy accepted(dialog.get(), SIGNAL(accepted(QStringList)));
    QMetaObject::invokeMethod(dialog.get(), "accept");
    QCOMPARE(accepted.size(), 1);
    QCOMPARE(accepted.at(0).at(0).toStringList().size(), 2);
    QCOMPARE(dialog->property("selectedFiles").toStringList().size(), 2);

    // And a dialog choosing one still answers with a list of one, so a caller
    // has one shape to deal with.
    const std::unique_ptr<QObject> single(component.create());
    QVERIFY(single);
    single->setProperty("currentFolder", root.toUserOutput());
    // The arrangement with a field to type the name into; see above.
    single->setProperty("classic", true);
    QObject * const nameField = single->findChild<QObject *>("nameField");
    QVERIFY(nameField);
    nameField->setProperty("text", "two.txt");
    QCOMPARE(single->property("wouldChooseAll").toStringList(),
             QStringList({(root / "two.txt").toUserOutput()}));
}

// A dialog showing only names makes the reader open a file to find out which
// one it is. The model already writes the size, the kind and the date the way
// a reader wants them; the browser was reading the size as a number, which it
// is not, and so had nothing to show.
void QuickUiTest::testEachEntrySaysHowBigItIsAndWhatItIs()
{
    Utils::TemporaryDirectory dir("quickui-browser-columns");
    QVERIFY(dir.isValid());
    const Utils::FilePath root = dir.path();
    QVERIFY((root / "sub").createDir());
    QVERIFY((root / "some.txt").writeFileContents(QByteArray(2048, 'x')));

    QtcQuick::FileBrowser browser;
    browser.setDirectory(root.toUserOutput());
    QtcQuick::FileEntries * const entries = browser.entries();
    QTRY_COMPARE(entries->rowCount(), 2);

    const auto valueOf = [entries](const QString &name, int role) {
        for (int row = 0; row < entries->rowCount(); ++row) {
            const QModelIndex index = entries->index(row, 0);
            if (entries->data(index, QtcQuick::FileEntries::NameRole).toString() == name)
                return entries->data(index, role).toString();
        }
        return QString("no such entry");
    };

    // A size a reader can read, not a count of bytes and not an empty string
    // where a number failed to parse.
    const QString size = valueOf("some.txt", QtcQuick::FileEntries::SizeRole);
    QVERIFY2(!size.isEmpty(), "the file says nothing about how big it is");
    QVERIFY2(size != "0", qPrintable("the size was read as a number: " + size));
    QVERIFY2(size.contains(QLocale().formattedDataSize(2048)),
             qPrintable("it says " + size + " rather than "
                        + QLocale().formattedDataSize(2048)));

    // A directory has no size, which is what the widget dialog shows too.
    QCOMPARE(valueOf("sub", QtcQuick::FileEntries::SizeRole), QString());

    // What each of them is, and when it changed.
    QVERIFY(!valueOf("some.txt", QtcQuick::FileEntries::TypeRole).isEmpty());
    QVERIFY(!valueOf("sub", QtcQuick::FileEntries::TypeRole).isEmpty());
    QVERIFY(!valueOf("some.txt", QtcQuick::FileEntries::ModifiedRole).isEmpty());

    // A hit from a search says the same things: a search is a way of finding
    // a file, not a poorer view of one.
    browser.setSearchText("some");
    QTRY_COMPARE(entries->rowCount(), 1);
    QVERIFY2(!valueOf("some.txt", QtcQuick::FileEntries::SizeRole).isEmpty(),
             "a file found by searching says nothing about how big it is");
}

// The two things the widget dialog's context menu does to an entry. Copying
// and pasting files it also does, and this does not yet.
void QuickUiTest::testAnEntryCanBeRenamedOrBinned()
{
    Utils::TemporaryDirectory dir("quickui-browser-entryactions");
    QVERIFY(dir.isValid());
    const Utils::FilePath root = dir.path();
    QVERIFY((root / "before.txt").writeFileContents("x"));
    QVERIFY((root / "doomed.txt").writeFileContents("x"));

    QtcQuick::FileBrowser browser;
    browser.setDirectory(root.toUserOutput());
    QtcQuick::FileEntries * const entries = browser.entries();
    QTRY_COMPARE(entries->rowCount(), 2);
    const auto rowOf = [&browser, entries](const QString &name) {
        for (int row = 0; row < entries->rowCount(); ++row) {
            if (browser.filePathAt(row).endsWith(name))
                return row;
        }
        return -1;
    };

    // Renamed on disk, not just in the listing.
    const int before = rowOf("before.txt");
    QVERIFY(before >= 0);
    QVERIFY(browser.rename(before, "after.txt"));
    QVERIFY((root / "after.txt").exists());
    QVERIFY(!(root / "before.txt").exists());
    QTRY_VERIFY(rowOf("after.txt") >= 0);

    // Renaming to nothing is refused rather than making a file with no name,
    // and so is a name that cannot be created - "sub/name" names a directory
    // that is not there. Either way the file keeps the name it had.
    QVERIFY(!browser.rename(rowOf("after.txt"), ""));
    QVERIFY(!browser.rename(rowOf("after.txt"), "sub/name"));
    QVERIFY((root / "after.txt").exists());
    QVERIFY(!(root / "sub" / "name").exists());

    // And into the bin, which is the machine's own - the file goes.
    const int doomed = rowOf("doomed.txt");
    QVERIFY(doomed >= 0);
    const QString failed = browser.moveToTrash(doomed);
    QVERIFY2(failed.isEmpty(), qPrintable(failed));
    QVERIFY(!(root / "doomed.txt").exists());
    QTRY_COMPARE(entries->rowCount(), 1);

    // A path on a device has no bin, and the browser says so rather than
    // deleting it instead, which is not what "move to the bin" means. Asked
    // of the rule rather than by binning something on a device: a browser
    // pointed at one has no entries to bin unless the device is really there,
    // and then the answer would be about there being no rows.
    QVERIFY(browser.whyNotBinned((root / "after.txt").toUserOutput()).isEmpty());
    const QString refused = browser.whyNotBinned("docker://nosuchimage/tmp/thing");
    QVERIFY2(!refused.isEmpty(), "something with no bin can be put in one");
    // And says *why*: "could not" is what a failed bin says as well, and the
    // reader can do something about one of those and not the other.
    QVERIFY2(refused.contains("device"), qPrintable("it said: " + refused));
    QVERIFY(!browser.whyNotBinned("").isEmpty());
}

// A file dialog is used by keyboard as much as by pointer, and the widget one
// binds the platform's own sequences for going about.
//
// What is checked is each shortcut's sequence and what activating it does.
// Delivery is not: a shortcut reaches a window that has the keyboard, and this
// process cannot take it here - showing the dialog and typing at it skipped
// every time, which is no guard at all. So this can be wrong about one thing,
// that the sequences arrive, and right about the two that go wrong far more
// often: which keys, and what they do.
void QuickUiTest::testTheFileDialogGetsAboutByKeyboard()
{
    Utils::TemporaryDirectory dir("quickui-dialog-keys");
    QVERIFY(dir.isValid());
    const Utils::FilePath root = dir.path();
    const Utils::FilePath inner = root / "inner";
    QVERIFY(inner.createDir());

    QQmlComponent component(QtcQuick::engine(),
                            QUrl("qrc:/qt/qml/QtCreator/Ui/QtcFileDialog.qml"));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    const std::unique_ptr<QObject> dialog(component.create());
    QVERIFY(dialog);

    auto * const browser = dialog->findChild<QtcQuick::FileBrowser *>("fileBrowser");
    QVERIFY(browser);
    browser->setDirectory(root.toUserOutput());
    browser->setDirectory(inner.toUserOutput());

    const auto shortcut = [&dialog](const QString &name) {
        return dialog->findChild<QObject *>(name);
    };
    const auto press = [](QObject *shortcut) {
        QVERIFY2(shortcut, "no such shortcut");
        QMetaObject::invokeMethod(shortcut, "activated");
    };

    // The keys themselves, as the platform writes them. The widget dialog
    // binds the same, and a dialog whose Back is not the system's Back is
    // worse than one with no shortcut at all.
    QObject * const back = shortcut("backShortcut");
    QVERIFY(back);
    QCOMPARE(back->property("nativeText").toString(),
             QKeySequence(QKeySequence::Back).toString(QKeySequence::NativeText));
    QObject * const parent = shortcut("parentShortcut");
    QVERIFY(parent);
    // Equal, not "contains": the native text of Ctrl+Shift+Up contains the
    // native text of Ctrl+Up, so asking whether one is inside the other
    // accepts a different shortcut that happens to include this one.
    QCOMPARE(parent->property("nativeText").toString(),
             QKeySequence("Ctrl+Up").toString(QKeySequence::NativeText));

    // And what each of them does.
    press(parent);
    QCOMPARE(browser->directory(), root.toUserOutput());

    press(back);
    QCOMPARE(browser->directory(), inner.toUserOutput());
    press(shortcut("forwardShortcut"));
    QCOMPARE(browser->directory(), root.toUserOutput());

    QSignalSpy rejected(dialog.get(), SIGNAL(rejected()));
    press(shortcut("closeShortcut"));
    QCOMPARE(rejected.size(), 1);

    // Renaming needs a row to rename, and says so by being off without one.
    QObject * const rename = shortcut("renameShortcut");
    QVERIFY(rename);
    QVERIFY2(!rename->property("enabled").toBool(),
             "renaming is offered with nothing selected to rename");
}

// Copying files in the dialog and pasting them somewhere else, which the
// widget dialog does through the clipboard - so a file copied in one can be
// pasted in the other, and in a file manager.
void QuickUiTest::testFilesCanBeCopiedAndPasted()
{
    Utils::TemporaryDirectory dir("quickui-browser-clipboard");
    QVERIFY(dir.isValid());
    const Utils::FilePath root = dir.path();
    const Utils::FilePath other = root / "other";
    QVERIFY(other.createDir());
    QVERIFY((root / "copied.txt").writeFileContents("contents"));

    // The clipboard is the machine's, so put back whatever was on it.
    QClipboard * const clipboard = QGuiApplication::clipboard();
    const QScopeGuard restore([clipboard] { clipboard->clear(); });

    QtcQuick::FileBrowser browser;
    browser.setDirectory(root.toUserOutput());
    QTRY_COMPARE(browser.entries()->rowCount(), 2);
    const auto rowOf = [&browser](const QString &name) {
        for (int row = 0; row < browser.entries()->rowCount(); ++row) {
            if (browser.filePathAt(row).endsWith(name))
                return row;
        }
        return -1;
    };

    clipboard->clear();
    QVERIFY2(!browser.canPaste(), "there is something to paste with an empty clipboard");

    browser.copyToClipboard({rowOf("copied.txt")});
    QVERIFY2(browser.canPaste(), "copying a file left nothing to paste");
    // On the clipboard as a file, which is what a file manager pastes.
    QVERIFY(clipboard->mimeData()->hasUrls());
    QCOMPARE(Utils::FilePath::fromUrl(clipboard->mimeData()->urls().first()),
             root / "copied.txt");

    // Pasted somewhere else: the file is there, with what was in it. The
    // copy runs in the background, so this waits for it to say it is done
    // rather than for the file to appear - a paste that never finishes would
    // otherwise look the same as one that copied nothing.
    QtcQuick::FileBrowser into;
    into.setDirectory(other.toUserOutput());
    const auto pasteInto = [](QtcQuick::FileBrowser &browser) {
        QSignalSpy done(&browser, &QtcQuick::FileBrowser::pasteFinished);
        browser.startPaste();
        if (!done.wait(30000))
            return QString("the paste never finished");
        return done.at(0).at(0).toString();
    };
    QCOMPARE(pasteInto(into), QString());
    QVERIFY((other / "copied.txt").exists());
    QCOMPARE((other / "copied.txt").fileContents().value_or(QByteArray()), QByteArray("contents"));

    // And pasted back where it came from: a duplicate rather than nothing,
    // and named the way the widget dialog names one.
    QCOMPARE(pasteInto(browser), QString());
    QVERIFY2((root / "copied copy.txt").exists(),
             "pasting into the directory a file came from did not duplicate it");
    QVERIFY((root / "copied.txt").exists());
    // Twice over, so the second duplicate does not overwrite the first.
    QCOMPARE(pasteInto(browser), QString());
    QVERIFY((root / "copied copy 2.txt").exists());

    // Nothing is left saying it is still copying.
    QVERIFY(!browser.isPasting());
    QVERIFY(browser.pasteStatus().isEmpty());
}

// Icons beside the names, and the icon view the widget dialog also offers.
void QuickUiTest::testTheFileDialogShowsIconsAndCanBeAGrid()
{
    Utils::TemporaryDirectory dir("quickui-dialog-icons");
    QVERIFY(dir.isValid());
    const Utils::FilePath root = dir.path();
    QVERIFY((root / "sub").createDir());
    QVERIFY((root / "code.cpp").writeFileContents("x"));

    QtcQuick::FileBrowser browser;
    browser.setDirectory(root.toUserOutput());
    QtcQuick::FileEntries * const entries = browser.entries();
    QTRY_COMPARE(entries->rowCount(), 2);

    // Something an Image can load, which a QIcon is not. The role is called
    // iconSource because a Control's own icon property is FINAL: a delegate
    // that took a role called "icon" would not load at all.
    const auto iconOf = [entries](const QString &name) {
        for (int row = 0; row < entries->rowCount(); ++row) {
            const QModelIndex index = entries->index(row, 0);
            if (entries->data(index, QtcQuick::FileEntries::NameRole).toString() == name)
                return entries->data(index, QtcQuick::FileEntries::IconRole).toString();
        }
        return QString();
    };
    const QString fileIcon = iconOf("code.cpp");
    const QString dirIcon = iconOf("sub");
    QVERIFY2(!fileIcon.isEmpty(), "a file has no icon to draw");
    QVERIFY2(fileIcon.startsWith("image://"), qPrintable("it is not loadable: " + fileIcon));
    QVERIFY2(!dirIcon.isEmpty(), "a directory has no icon to draw");
    // A directory does not look like a file, which is the whole point of
    // showing an icon at all.
    QVERIFY2(fileIcon != dirIcon, "a file and a directory are drawn with the same icon");

    // And the two views over those rows: one shows at a time, and both are
    // fed by the same entries.
    QQmlComponent component(QtcQuick::engine(),
                            QUrl("qrc:/qt/qml/QtCreator/Ui/QtcFileDialog.qml"));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    const std::unique_ptr<QObject> dialog(component.create());
    QVERIFY(dialog);
    dialog->setProperty("currentFolder", root.toUserOutput());

    auto * const list = dialog->findChild<QQuickItem *>("entryList");
    auto * const gridView = dialog->findChild<QQuickItem *>("entryGrid");
    QVERIFY(list && gridView);
    QVERIFY2(list->isVisible() && !gridView->isVisible(),
             "the dialog does not open as a list");
    QCOMPARE(gridView->property("model").value<QObject *>(),
             list->property("model").value<QObject *>());

    dialog->setProperty("showingIcons", true);
    QVERIFY2(gridView->isVisible() && !list->isVisible(),
             "asking for icons did not swap the views");
}

// The widget dialog puts up a progress dialog while it copies, with a Cancel.
// A paste that blocks the dialog until it finishes looks broken, and one that
// cannot be stopped is worse on a device than on this machine.
void QuickUiTest::testALongPasteSaysWhatItIsDoingAndCanBeStopped()
{
    Utils::TemporaryDirectory dir("quickui-browser-longpaste");
    QVERIFY(dir.isValid());
    const Utils::FilePath root = dir.path();
    const Utils::FilePath many = root / "many";
    const Utils::FilePath into = root / "into";
    QVERIFY(many.createDir());
    QVERIFY(into.createDir());
    // Enough of them that the copy is still going when it is asked to stop.
    // Empty, so that making them is quick: what takes the time is the number
    // of files, not their size.
    for (int i = 0; i < 6000; ++i)
        QVERIFY((many / QString("file%1.txt").arg(i)).writeFileContents({}));

    QClipboard * const clipboard = QGuiApplication::clipboard();
    const QScopeGuard restore([clipboard] { clipboard->clear(); });

    QtcQuick::FileBrowser browser;
    browser.setDirectory(root.toUserOutput());
    QTRY_COMPARE(browser.entries()->rowCount(), 2);
    int manyRow = -1;
    for (int row = 0; row < browser.entries()->rowCount(); ++row) {
        if (browser.filePathAt(row).endsWith("many"))
            manyRow = row;
    }
    QVERIFY(manyRow >= 0);
    browser.copyToClipboard({manyRow});

    QtcQuick::FileBrowser target;
    target.setDirectory(into.toUserOutput());
    QVERIFY(!target.isPasting());

    // What it said it was copying, as opposed to how often it said anything:
    // the status is also cleared when the paste ends, so counting the signal
    // would be satisfied by that alone.
    QStringList saidWhat;
    QSignalSpy finished(&target, &QtcQuick::FileBrowser::pasteFinished);
    target.startPaste();
    QVERIFY2(target.isPasting(), "the paste did not start, or finished before it could be seen");

    // Stopped as soon as it says it has started, which is the earliest a
    // reader could press Cancel too.
    connect(&target, &QtcQuick::FileBrowser::pasteStatusChanged, &target, [&] {
        if (!target.pasteStatus().isEmpty())
            saidWhat << target.pasteStatus();
        target.cancelPaste();
    });

    // It says which file it is copying while it copies, which is the whole
    // reason for doing it in the background.
    QTRY_VERIFY2(!saidWhat.isEmpty(), "the paste never said which file it was copying");
    QVERIFY2(saidWhat.first().startsWith("file"),
             qPrintable("it said it was copying " + saidWhat.first()));
    QTRY_VERIFY2(finished.size() > 0, "the paste did not stop when it was asked to");
    QVERIFY(!target.isPasting());
    QVERIFY(target.pasteStatus().isEmpty());

    // Half a directory is not what was asked for, so what was copied so far
    // goes rather than being left behind. No skip for "it finished first":
    // the cancel went in on the first file it reported, so a copy that ran to
    // the end is one that ignored it - which is what this is here to catch.
    QVERIFY2(!(into / "many").exists(),
             "a cancelled copy left the half it had made behind");
}

// Whether a path aspect holds something usable. The widget path chooser checks
// this itself and tells the aspect; on a Quick page nothing did, so a page that
// offers its options only when the command is there - every beautifier does -
// had them greyed out for good.
void QuickUiTest::testAPathFieldSaysWhetherWhatItHoldsIsThere()
{
    Utils::AspectContainer page;
    page.setAutoApply(false);
    Utils::FilePathAspect command(&page);
    command.setLabelText("Command");
    command.setExpectedKind(Utils::PathChooserKind::ExistingCommand);
    command.setValue(Utils::FilePath::fromUserInput("/bin/echo"));

    Utils::FilePathAspect missing(&page);
    missing.setLabelText("Missing");
    missing.setExpectedKind(Utils::PathChooserKind::ExistingCommand);
    missing.setValue(Utils::FilePath::fromUserInput("/no/such/command/at/all"));

    // Nobody has looked yet, so neither is valid: an aspect is not valid
    // because it has never been questioned.
    QVERIFY(!command.isValid());
    QVERIFY(!missing.isValid());

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QList<QQuickItem *> delegates;
    QTRY_COMPARE((delegates = findQmlComponents(quickWidget->rootObject(), "StringDelegate"))
                     .size(), 2);

    // Showing the field is what asks, and the answer comes back from another
    // thread - the path is looked for on whatever device it names.
    QTRY_VERIFY2(command.isValid(), "a command that is there never came to be valid");
    QVERIFY2(!missing.isValid(), "a command that is not there is taken for one that is");

    // And the field says what is wrong with it, which is the same answer read
    // by a reader rather than by a page.
    const auto messageOf = [&delegates](Utils::BaseAspect *aspect) {
        for (QQuickItem *item : delegates) {
            if (item->property("aspect").value<Utils::BaseAspect *>() != aspect)
                continue;
            for (QQuickItem *part : findQmlComponents(item, "")) {
                if (part->objectName() == "validationMessage")
                    return part->property("text").toString();
            }
        }
        return QString();
    };
    QTRY_VERIFY2(!messageOf(&missing).isEmpty(),
                 "the field says nothing about a command that is not there");
    QVERIFY2(messageOf(&command).isEmpty(),
             qPrintable("a command that is there is complained about: " + messageOf(&command)));

    // Asking about something new does not make it valid until the answer is
    // back. There is no message while the path is being looked for, and an
    // empty message is exactly what "nothing is wrong with it" looks like -
    // so a page watching this would light up its options for whatever a bad
    // path takes to answer.
    missing.validationMessage(QVariant::fromValue(QString("/another/missing/thing")));
    QVERIFY2(!missing.isValid(),
             "a path counts as valid while the answer is still being fetched");

    // An untouched field that offers a placeholder is not complained about,
    // which is what the widget line edit does with one: it shows neither
    // valid nor invalid while the placeholder is what is on screen. Without
    // this every empty path field on every page would say so in red.
    Utils::FilePathAspect empty(&page);
    empty.setLabelText("Empty");
    empty.setPlaceHolderText("Leave blank for the default");
    QVERIFY2(empty.validationMessage(QVariant::fromValue(QString())).isEmpty(),
             "an untouched field with a placeholder is complained about");
    QVERIFY(!empty.isValid());

    // An untouched field is not complained about even with no placeholder to
    // show instead. The widget line edit does say "the path must not be
    // empty" in that case, and this deliberately does not: the message would
    // appear under the field on every page with a path that has not been
    // filled in yet, which is a great deal of red about nothing being wrong.
    // What matters for a page asking is that it is not *valid* either.
    Utils::FilePathAspect blank(&page);
    blank.setLabelText("Blank");
    QVERIFY(blank.validationMessage(QVariant::fromValue(QString())).isEmpty());
    QVERIFY2(!blank.isValid(), "an empty path is taken for a usable one");
}

// A directory big enough to notice. The browser rebuilds its whole row list
// whenever the model underneath says anything, and the model fills a listing
// in as it arrives - so a directory of thousands could be rebuilt thousands of
// times, each one longer than the last.
void QuickUiTest::testABigDirectoryIsListedWithoutTheDialogHanging()
{
    Utils::TemporaryDirectory dir("quickui-browser-big");
    QVERIFY(dir.isValid());
    const Utils::FilePath root = dir.path();
    const int many = 8000;
    for (int i = 0; i < many; ++i)
        QVERIFY((root / QString("file%1.txt").arg(i)).writeFileContents({}));

    QtcQuick::FileBrowser browser;
    QElapsedTimer listing;
    listing.start();
    browser.setDirectory(root.toUserOutput());
    QTRY_COMPARE_WITH_TIMEOUT(browser.entries()->rowCount(), many, 60000);
    const qint64 took = listing.elapsed();
    qInfo() << "listed" << many << "files in" << took << "ms";

    // Not a benchmark, and not a guard on the laziness either - that what
    // each row says is still right is the column test's business. This
    // catches the shape going wrong: a rebuild per inserted row, or a
    // filesystem question per row up front, turns seconds into minutes. It
    // takes about half a second here.
    QVERIFY2(took < 5000,
             qPrintable(QString("listing %1 files took %2 ms").arg(many).arg(took)));
}

// Which dialog a field opens was decided in two delegates, and they had begun
// to differ. It is one component now, so the rule is tested once.
void QuickUiTest::testWhichDialogAFieldOpensIsOneDecision()
{
    QQmlComponent component(QtcQuick::engine(),
                            QUrl("qrc:/qt/qml/QtCreator/Ui/DeviceBrowse.qml"));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    const std::unique_ptr<QObject> browse(
        component.createWithInitialProperties({{"allowed", true}, {"pathKind", "ExistingCommand"}}));
    QVERIFY(browse);

    const auto wanted = [&browse](const QString &current) {
        bool answer = false;
        QMetaObject::invokeMethod(browse.get(), "wanted", Q_RETURN_ARG(bool, answer),
                                  Q_ARG(QString, current));
        return answer;
    };

    // The rule the widget path chooser follows, which is one line of it:
    //     remote = remote || !filePath().isLocal();
    QtcQuick::AspectModels models;
    if (models.hasNativeFileDialog()) {
        // A path on this machine is the platform's business.
        QVERIFY(!wanted(QDir::homePath()));
        QVERIFY(!wanted(""));
    } else {
        // Unless the platform has nothing to offer, when there is nothing to
        // give up by using ours - as the widget dialog also reasoned.
        QVERIFY2(wanted(QDir::homePath()),
                 "the platform has no dialog and ours was not used anyway");
    }
    // One on a device is ours whatever the platform offers: its dialog cannot
    // see a device.
    QVERIFY2(wanted("docker://nosuchimage/tmp"),
             "a path on a device would be browsed with the platform's dialog");

    // A field that refuses device paths never opens it, however it is asked.
    const std::unique_ptr<QObject> hereOnly(
        component.createWithInitialProperties({{"allowed", false}, {"pathKind", "ExistingCommand"}}));
    QVERIFY(hereOnly);
    bool answer = true;
    QMetaObject::invokeMethod(hereOnly.get(), "wanted", Q_RETURN_ARG(bool, answer),
                              Q_ARG(QString, QString("docker://nosuchimage/tmp")));
    QVERIFY2(!answer, "a field that takes no device path was sent to a device anyway");
}

// Directories first, then by name without regard to case - the order the
// widget dialog shows and the one a reader looks for a file in. Without it a
// listing comes out in whatever order the directory was read.
void QuickUiTest::testADirectoryIsListedInAnOrderAReaderExpects()
{
    Utils::TemporaryDirectory dir("quickui-browser-order");
    QVERIFY(dir.isValid());
    const Utils::FilePath root = dir.path();
    // Made in an order that is neither the answer nor its reverse, so that
    // "it happens to come out right" is not what is being seen.
    for (const QString &name : QStringList{"widget.cpp", "Alpha.txt", "main.cpp", "zebra.h"})
        QVERIFY((root / name).writeFileContents({}));
    for (const QString &name : QStringList{"tests", "Include", "src"})
        QVERIFY((root / name).createDir());

    QtcQuick::FileBrowser browser;
    browser.setDirectory(root.toUserOutput());
    QtcQuick::FileEntries * const entries = browser.entries();
    QTRY_COMPARE(entries->rowCount(), 7);

    QStringList listed;
    for (int row = 0; row < entries->rowCount(); ++row)
        listed << browser.nameAt(row);

    QCOMPARE(listed, QStringList({"Include", "src", "tests",
                                  "Alpha.txt", "main.cpp", "widget.cpp", "zebra.h"}));
}

// Where a group's spare height goes. A beautifier page shows the editor for a
// configuration only once one is chosen, so on a machine with none the tall
// thing in the group is invisible - and the group's spare height, having
// nothing to fill, pushed the handful of visible controls to the bottom of it.
// Four hundred pixels of nothing between the group's title and its first
// control, which no test asked about because every control was present, the
// right size, and correct.
void QuickUiTest::testAGroupPutsItsContentAtItsTop()
{
    Core::setAspectFormFactory([](Utils::AspectContainer *container) {
        return QtcQuick::createAspectForm(container);
    });

    Core::IOptionsPage *page = nullptr;
    for (Core::IOptionsPage *candidate : Core::IOptionsPage::allOptionsPages()) {
        if (candidate->displayName() == "ClangFormat")
            page = candidate;
    }
    QVERIFY2(page, "no ClangFormat page - is the Beautifier plugin loaded?");

    const std::unique_ptr<QWidget> widget(page->createWidget());
    QVERIFY(widget);
    widget->resize(900, 700);
    widget->show();
    QVERIFY(QTest::qWaitForWindowExposed(widget.get()));
    auto * const quickWidget = widget->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QQuickItem * const root = quickWidget->rootObject();
    QVERIFY(root);

    // The last control of the first group, and the first of the second.
    QQuickItem *mimeTypes = nullptr;
    QQuickItem *firstOption = nullptr;
    QTRY_VERIFY([&] {
        mimeTypes = nullptr;
        firstOption = nullptr;
        for (QQuickItem *item : findQmlComponents(root, "StringDelegate")) {
            auto aspect = item->property("aspect").value<Utils::BaseAspect *>();
            if (aspect && aspect->qmlName() == "SupportedMimeTypes")
                mimeTypes = item;
        }
        const QList<QQuickItem *> radios = findQmlComponents(root, "RadioDelegate");
        if (!radios.isEmpty())
            firstOption = radios.first();
        return mimeTypes && firstOption;
    }());

    const QPointF mimeBottom = mimeTypes->mapToItem(root, QPointF(0, mimeTypes->height()));
    const QPointF optionTop = firstOption->mapToItem(root, QPointF(0, 0));
    const qreal gap = optionTop.y() - mimeBottom.y();
    QVERIFY2(gap >= 0, qPrintable(QString("the options are above the group before them: %1")
                                      .arg(gap)));
    // Room for the group's title and its padding, and nothing like the four
    // hundred pixels an unfilled group used to leave.
    QVERIFY2(gap < 150,
             qPrintable(QString("%1 pixels of nothing between the groups").arg(gap)));
}

// A group whose title repeats the label of the one aspect inside it says the
// same thing twice, one above the other. The To-Do page did: a group called
// "Scanning Scope" around an aspect called "Scanning Scope".
void QuickUiTest::testAPageSaysAHeadingOnlyOnce()
{
    Core::setAspectFormFactory([](Utils::AspectContainer *container) {
        return QtcQuick::createAspectForm(container);
    });

    Core::IOptionsPage *page = nullptr;
    for (Core::IOptionsPage *candidate : Core::IOptionsPage::allOptionsPages()) {
        if (candidate->displayName() == "To-Do")
            page = candidate;
    }
    QVERIFY2(page, "no To-Do page - is the Todo plugin loaded?");

    const std::unique_ptr<QWidget> widget(page->createWidget());
    QVERIFY(widget);
    widget->resize(900, 700);
    widget->show();
    QVERIFY(QTest::qWaitForWindowExposed(widget.get()));
    auto * const quickWidget = widget->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QQuickItem * const root = quickWidget->rootObject();
    QVERIFY(root);

    int saidIt = 0;
    QTRY_VERIFY([&] {
        saidIt = 0;
        for (QQuickItem *item : findQmlComponents(root, "")) {
            if (!item->isVisible())
                continue;
            const QVariant text = item->property("text");
            if (text.isValid() && text.toString() == "Scanning Scope")
                ++saidIt;
        }
        return saidIt > 0;
    }());
    QCOMPARE(saidIt, 1);
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
    // A preview holding more code than fits must scroll itself rather than grow
    // the page and make the page scroll. TextViewport lays out only what is on
    // screen, so its contentHeight is the document's and its height is the
    // space the layout gave it; those two disagreeing is the whole point.
    std::unique_ptr<QWidget> host;
    QQuickItem *root = showPage("Code Style", host, {640, 400});
    if (!root)
        QSKIP("The C++ Code Style page is not available here");

    QQuickItem *viewport = nullptr;
    QTRY_VERIFY(viewport = findQmlNamed(root, "codeViewport").value(0));
    // The text is the buffer's, not the view's: a viewport draws a document and
    // does not hold one.
    QObject *buffer = root->findChild<QObject *>("codeStylePreviewBuffer");
    QVERIFY(buffer);

    const QString snippet = buffer->property("text").toString();
    QVERIFY(!snippet.isEmpty());
    // More code than fits, which is the case that used to grow the page.
    buffer->setProperty("text", snippet.repeated(8));
    QCoreApplication::processEvents();

    QTRY_VERIFY2(viewport->property("contentHeight").toReal() > viewport->height(),
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

void QuickUiTest::testEditingThePreviewReachesTheAspectThatOwnsIt()
{
    // The buffer holds the text while it is being edited, but the aspect owns
    // it: Reset goes back to the factory's snippet and Format runs the
    // formatter, and both work on the aspect's value. A preview whose edits
    // never got there would format the text as it was before they were made.
    //
    // Written back when focus leaves rather than on every keystroke, because
    // the aspect's value changing re-indents, and that must not rewrite the
    // document under the cursor.
    std::unique_ptr<QWidget> host;
    QQuickItem *root = showPage("Code Style", host);
    if (!root)
        QSKIP("No Code Style page is available here");

    QQuickItem *viewport = nullptr;
    QTRY_VERIFY(viewport = findQmlNamed(root, "codeViewport").value(0));
    auto quickWidget = host->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);

    Utils::BaseAspect *preview = nullptr;
    for (QQuickItem *delegate : findAspectDelegates(root)) {
        if (auto aspect = delegate->property("aspect").value<Utils::BaseAspect *>()) {
            if (aspect->qmlName() == "Preview")
                preview = aspect;
        }
    }
    QVERIFY2(preview, "the preview does not say which aspect it draws");

    QMetaObject::invokeMethod(viewport, "forceActiveFocus");
    QTRY_VERIFY(viewport->hasActiveFocus());
    viewport->setProperty("cursorPosition", 0);
    quickWidget->setFocus();
    QTest::keyClick(quickWidget, 'Z');

    // Still being typed, so the aspect has not been told yet.
    QVERIFY2(!preview->volatileVariantValue().toString().startsWith('Z'),
             "the preview wrote to the aspect while the cursor was still in it");

    // Focus leaves, and now it has.
    viewport->setProperty("focus", false);
    QTRY_VERIFY(!viewport->hasActiveFocus());
    QTRY_VERIFY2(preview->volatileVariantValue().toString().startsWith('Z'),
                 "editing the preview never reached the aspect that owns it");
}

void QuickUiTest::testTabTypesAnIndentInACodeEditor()
{
    // Tab in an editor types an indent, and what an indent is is the code
    // style's answer - not a tab character, wherever the style says spaces.
    std::unique_ptr<QWidget> host;
    QQuickItem *root = showPage("Code Style", host);
    if (!root)
        QSKIP("The C++ Code Style page is not available here");

    // Focus goes to the viewport itself: it is a focus scope, so focusing the
    // component around it would stop one level short and no key would arrive.
    QQuickItem *viewport = nullptr;
    QTRY_VERIFY(viewport = findQmlNamed(root, "codeViewport").value(0));
    QObject *buffer = root->findChild<QObject *>("codeStylePreviewBuffer");
    QVERIFY(buffer);
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
        QMetaObject::invokeMethod(viewport, "forceActiveFocus");
        QTRY_VERIFY(viewport->hasActiveFocus());
        viewport->setProperty("cursorPosition", 0);
        const QString before = buffer->property("text").toString();
        // Sent to the QQuickWidget rather than to the scene's own window: the
        // widget focus chain is what takes the key, and delivering straight to
        // the scene steps over the thing being tested. The widget needs the
        // focus for the same reason - a QQuickWidget hands the Tab on from
        // focusNextPrevChild().
        quickWidget->setFocus();
        QTest::keyClick(quickWidget, Qt::Key_Tab);
        const QString after = buffer->property("text").toString();
        typedByTab = after.left(after.size() - before.size());
    };

    // Spaces where the style says spaces, as many as it indents by.
    tabPolicy->setValue(0);
    indentSize->setValue(4);
    typeTab();
    QCOMPARE(typedByTab, QString("    "));
    QVERIFY2(viewport->hasActiveFocus(), "Tab left the editor");

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
    const QString before = buffer->property("text").toString();
    QTest::keyClick(quickWidget, Qt::Key_Backtab);
    QCOMPARE(buffer->property("text").toString(), before.mid(typedByTab.size()));
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

// The widget list put the keyboard on the first format when it opened, so the
// properties beside it had something to show. Without that the page is a list
// beside an empty half until something is clicked.
void QuickUiTest::testTheFormatListOpensOnAFormat()
{
    std::unique_ptr<QWidget> host;
    QQuickItem *root = showPage("Font && Colors", host);
    if (!root)
        QSKIP("The Font && Colors page is not available here");

    QQuickItem *foreground = nullptr;
    QTRY_VERIFY([&] {
        for (QQuickItem *item : findAspectDelegates(root)) {
            if (auto *aspect = item->property("aspect").value<Utils::BaseAspect *>()) {
                if (aspect->qmlName() == "Foreground") {
                    foreground = item;
                    return true;
                }
            }
        }
        return false;
    }());

    QVERIFY2(foreground->isVisible(),
             "the page opened with no format chosen, so it showed no format properties");
}

void QuickUiTest::testTheFormatListIsReadOnTheSchemesOwnBackground()
{
    // The other half, on the page this exists for. A dark scheme sets a Text
    // background - "#000000" in Dark, "#2e2f30" in Qt Creator Dark - and the
    // list of formats has to be read on it. The shipped Default scheme sets
    // none and relies on built-in defaults, so it leaves the form alone, which
    // is what the widget list did too.
    std::unique_ptr<QWidget> host;
    QQuickItem *root = showPage("Font && Colors", host);
    if (!root)
        QSKIP("The Font && Colors page is not available here");

    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(root, "TableDelegate"));

    Utils::SelectionAspect *scheme = nullptr;
    for (QQuickItem *item : findAspectDelegates(root)) {
        if (auto aspect = item->property("aspect").value<Utils::BaseAspect *>()) {
            if (aspect->qmlName() == "Scheme")
                scheme = qobject_cast<Utils::SelectionAspect *>(aspect);
        }
    }
    QVERIFY2(scheme, "the page does not offer a scheme to choose");

    // Whichever of the shipped schemes says what code is read on. Named rather
    // than assumed: the order they are listed in is not this test's business.
    int dark = -1;
    for (int i = 0; i < scheme->optionCount(); ++i) {
        if (scheme->displayForIndex(i).contains("Dark"))
            dark = i;
    }
    if (dark == -1)
        QSKIP("No dark colour scheme is installed here");

    scheme->setValue(dark);
    QTRY_VERIFY2(delegate->property("pres").toMap().value("rowBackground").canConvert<QColor>()
                     && delegate->property("pres")
                            .toMap()
                            .value("rowBackground")
                            .value<QColor>()
                            .isValid(),
                 "the format list is not read on the scheme's own background");
}

void QuickUiTest::testATableIsReadOnTheBackgroundItsAspectNames()
{
    // A list of syntax formats is showing what it describes, and most formats
    // set no background of their own - the model answers nothing for them. The
    // widget list carried the editor's background in its palette so those rows
    // were still read on it; on the form's background a dark scheme's colours
    // are unreadable. Rows only: the header keeps the form's colours, as it did
    // in the widget view.
    const auto backgroundBehindTheRows = [](QQuickItem *root, const QColor &wanted) {
        QQuickItem *view = tableViewOf(root);
        if (!view || !view->parentItem())
            return -1;
        int painted = 0;
        for (QQuickItem *rect : findQmlComponents(view->parentItem(), "QQuickRectangle")) {
            if (rect->parentItem() == view->parentItem() && rect->isVisible()
                && rect->property("color").value<QColor>() == wanted) {
                ++painted;
            }
        }
        return painted;
    };

    // An aspect that says nothing leaves the form alone, which is what every
    // other table wants.
    Utils::AspectContainer plain;
    TestTableAspect quiet(&plain);
    quiet.setLabelText("Rows");
    const std::unique_ptr<QWidget> plainForm(showForm(&plain));
    QVERIFY(plainForm);
    auto plainQuick = plainForm->findChild<QQuickWidget *>();
    QVERIFY(plainQuick);
    QTRY_VERIFY(tableViewOf(plainQuick->rootObject()));
    QCOMPARE(backgroundBehindTheRows(plainQuick->rootObject(), QColor(Qt::darkBlue)), 0);

    Utils::AspectContainer page;
    TestTableAspect table(&page);
    table.setLabelText("Rows");
    table.m_rowBackground = QColor(Qt::darkBlue);
    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);
    QTRY_VERIFY(tableViewOf(quickWidget->rootObject()));
    QTRY_COMPARE(backgroundBehindTheRows(quickWidget->rootObject(), QColor(Qt::darkBlue)), 1);
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

void QuickUiTest::testEditingAPageMakesItDirtyAndCancelPutsItBack()
{
    // The promise every page in the Preferences dialog makes: what you change
    // is not settled until you press Apply, and Cancel undoes it. Three passive
    // censuses failed to check this because it is only answerable *after* an
    // edit - an unedited page looks exactly like one that can never be dirty.
    //
    // So this edits, on the pages where editing is bounded: a container that is
    // exactly Utils::AspectContainer, so nothing overrides apply(), cancel() or
    // isDirty() and there is no page-local copy to think about, and a plain
    // BoolAspect, which is one bit and no side effect. The edit is to the
    // volatile value, which is what a check box writes, and cancel() puts it
    // back without anything being written to disk.
    int pagesEdited = 0;
    int ownContainer = 0;
    int appliesImmediately = 0;
    int plainButNoBool = 0;
    QStringList notDirty;
    QStringList notRestored;

    for (Core::IOptionsPage *page : Core::IOptionsPage::allOptionsPages()) {
        const std::optional<Utils::AspectContainer *> aspects = page->aspects();
        if (!aspects || !*aspects)
            continue;
        Utils::AspectContainer * const container = *aspects;

        // Only the plain ones. A subclass may keep a copy of its own and mean
        // something different by all three of these.
        if (container->metaObject() != &Utils::AspectContainer::staticMetaObject) {
            ++ownContainer;
            continue;
        }

        // A page that applies as it is edited never becomes dirty, and that is
        // not a failure of Apply on the pages that mean it. Which ones do is
        // testNoPageHasAnApplyThatCannotSaveAnything()'s business; this one
        // only asks about pages that defer.
        if (!container->aspects().isEmpty() && container->aspects().first()->isAutoApply()) {
            ++appliesImmediately;
            continue;
        }

        // Nested too: most pages put their settings in groups, so the plain
        // values are a container down. A nested container is still only a
        // grouping - the page's own container is the one that must be exactly
        // AspectContainer, and it is.
        Utils::BaseAspect *editable = nullptr;
        QVariant before;
        QVariant wanted;
        std::function<void(Utils::AspectContainer *)> look =
            [&](Utils::AspectContainer *from) {
                for (Utils::BaseAspect *child : from->aspects()) {
                    if (editable)
                        return;
                    if (auto nested = qobject_cast<Utils::AspectContainer *>(child)) {
                        look(nested);
                        continue;
                    }
                    if (!child->isEnabled() || !child->isVisible())
                        continue;

                    // Only what is actually saved. An aspect with no settings
                    // key is not a setting - the device picker on the Compilers
                    // page chooses whose toolchains to *show* - so it has
                    // nothing to make dirty and never promised to.
                    if (child->settingsKey().isEmpty())
                        continue;

                    // Numbers and check boxes only. A path is a string that
                    // gets resolved against the filesystem when it changes, and
                    // a plain string may drive a completer; neither is one
                    // value and no side effect, which is the whole licence
                    // this test is operating under.
                    const QVariant current = child->volatileVariantValue();
                    if (qobject_cast<Utils::BoolAspect *>(child)) {
                        before = current;
                        wanted = !current.toBool();
                    } else if (qobject_cast<Utils::IntegerAspect *>(child)
                               || qobject_cast<Utils::DoubleAspect *>(child)) {
                        before = current;
                        wanted = current.toDouble() + 1;
                    } else if (auto choice = qobject_cast<Utils::SelectionAspect *>(child)) {
                        // A different one of the choices it already offers. The
                        // page may show a different sub-form for it, which is a
                        // display effect and not a side effect.
                        if (choice->optionCount() < 2)
                            continue;
                        before = current;
                        wanted = current.toInt() == 0 ? 1 : 0;
                    } else if (qobject_cast<Utils::TriStateAspect *>(child)) {
                        before = current;
                        wanted = current.toInt() == 0 ? 1 : 0;
                    } else {
                        continue;
                    }

                    // Only if the write actually takes - a value at the top of
                    // its range does not move, and asserting on one that did
                    // not change would assert nothing.
                    child->setVolatileVariantValueFromGui(wanted);
                    if (child->volatileVariantValue() == before) {
                        child->setVolatileVariantValueFromGui(before);
                        continue;
                    }
                    editable = child;
                    return;
                }
            };
        look(container);

        if (!editable) {
            ++plainButNoBool;
            continue;
        }

        const QString name = page->displayName() + "/" + editable->qmlName();
        ++pagesEdited;

        if (!static_cast<Utils::BaseAspect *>(container)->isDirty())
            notDirty << name;

        container->cancel();
        if (editable->volatileVariantValue() != before)
            notRestored << name;
    }

    QVERIFY2(pagesEdited > 0, "no page offered a plain check box, so this checked nothing");
    QVERIFY2(notDirty.isEmpty(),
             qPrintable("pages that did not notice being edited, so Apply has nothing to "
                        "commit and the edit is already settled: "
                        + notDirty.join(", ")));
    QVERIFY2(notRestored.isEmpty(),
             qPrintable("pages whose Cancel did not put the value back: "
                        + notRestored.join(", ")));
    qInfo().noquote() << "pages edited and cancelled:" << pagesEdited
                      << "| skipped, own container:" << ownContainer
                      << "| skipped, applies as edited:" << appliesImmediately
                      << "| skipped, nothing safe to edit:" << plainButNoBool;
}

void QuickUiTest::testNoPageHasAnApplyThatCannotSaveAnything()
{
    // A page whose aspects auto-apply has written the user's edit through
    // already, so its container is never dirty and Apply has nothing to commit.
    // That is the shape of "nothing ever saves", and it is also the shape of
    // four pages that mean it - so this names them rather than claiming to
    // detect the bug.
    //
    // It cannot detect the bug. Whether Apply works is only answerable after an
    // edit: an unedited page is not dirty either, and editing one of each of a
    // hundred pages to find out runs whatever each of them does about it. What
    // this does instead is hold the list still. A fifth page arriving here is a
    // decision someone made, and it should be made on purpose:
    //
    //   - the three Code Style pages keep a page-local copy that their aspects
    //     edit live, and CodeStyleAspect overrides apply()/cancel()/isDirty()
    //     to push it across. Covered by CodeStyleAspectTest::
    //     testTheApplyButtonReachesTheStyleAndNotJustTheAspect().
    //   - Android says so with IOptionsPage::setAutoApply(): its changes are
    //     immediate and it offers no Apply of its own.
    //
    // Anything else in this list has neither, and its Apply button is a no-op.
    static const QSet<QString> byDesign = {
        "A.Cpp.Code Style",
        "A.Code Style",
        "Nim.NimCodeStyleSettings",
        "BB.Android Configurations",
    };

    QStringList unexplained;
    QStringList goneQuiet;

    for (Core::IOptionsPage *page : Core::IOptionsPage::allOptionsPages()) {
        const std::optional<Utils::AspectContainer *> aspects = page->aspects();
        if (!aspects || !*aspects)
            continue;
        Utils::AspectContainer * const container = *aspects;
        if (container->aspects().isEmpty())
            continue;

        const QString id = page->id().toString();
        const bool applies = container->aspects().first()->isAutoApply();
        if (applies && !byDesign.contains(id))
            unexplained << page->displayName() + " [" + id + "]";
        if (!applies && byDesign.contains(id))
            goneQuiet << page->displayName() + " [" + id + "]";
    }

    QVERIFY2(unexplained.isEmpty(),
             qPrintable("pages that apply as they are edited without saying why - if that is "
                        "deliberate, say so here and cover it: "
                        + unexplained.join(", ")));
    // The other way too, so the list does not outlive the reason for it.
    QVERIFY2(goneQuiet.isEmpty(),
             qPrintable("pages listed here that no longer apply as they are edited, so the "
                        "entry can go: "
                        + goneQuiet.join(", ")));
}

void QuickUiTest::testEveryGroupedListMapsItsRowsBothWays()
{
    // The third shape of the same thing. A grouped list - Kits, Toolchains,
    // Debuggers, Qt Versions, the CMake/Meson/GN tool pages - shows items under
    // headings, so a row of the view is not a row of the aspect. Clicking a
    // cell does
    //
    //     aspect.currentRow = aspect.rowForIndex(view.index(row, column))
    //
    // and moving the current row the other way asks indexForRow(). Those two
    // have to be inverses or the page acts on an item the user did not pick,
    // and nothing operated either of them.
    //
    // Called by name, the way QML calls them: that is also what says they are
    // still Q_INVOKABLE, which no build error would.
    Core::setAspectFormFactory([](Utils::AspectContainer *container) {
        return QtcQuick::createAspectForm(container);
    });

    int listsChecked = 0;
    int rowsChecked = 0;
    QStringList wrong;

    for (Core::IOptionsPage *page : Core::IOptionsPage::allOptionsPages()) {
        const std::optional<Utils::AspectContainer *> aspects = page->aspects();
        if (!aspects || !*aspects || (*aspects)->qmlSource().isEmpty())
            continue;

        std::unique_ptr<Core::IOptionsPageWidget> widget(page->createWidget());
        if (!widget)
            continue;
        auto quickWidget = widget->findChild<QQuickWidget *>();
        if (!quickWidget || !quickWidget->rootObject())
            continue;

        for (QQuickItem *delegate :
             findQmlComponents(quickWidget->rootObject(), "GroupedListDelegate")) {
            auto aspect = delegate->property("aspect").value<Utils::BaseAspect *>();
            if (!aspect)
                continue;

            // Walked until the aspect says there is no such row, rather than
            // asking the display model - its rows include the headings.
            int rows = 0;
            for (int row = 0;; ++row) {
                QModelIndex index;
                if (!QMetaObject::invokeMethod(aspect, "indexForRow",
                                               Q_RETURN_ARG(QModelIndex, index),
                                               Q_ARG(int, row))) {
                    wrong << page->displayName() + ": indexForRow is not callable";
                    break;
                }
                if (!index.isValid())
                    break;
                ++rows;

                int back = -1;
                if (!QMetaObject::invokeMethod(aspect, "rowForIndex", Q_RETURN_ARG(int, back),
                                               Q_ARG(QModelIndex, index))) {
                    wrong << page->displayName() + ": rowForIndex is not callable";
                    break;
                }
                if (back != row) {
                    wrong << QString("%1: row %2 came back as %3")
                                 .arg(page->displayName())
                                 .arg(row)
                                 .arg(back);
                }
            }

            if (rows == 0)
                continue;
            ++listsChecked;
            rowsChecked += rows;

            // And the current row is settable, which is what a click does.
            aspect->setProperty("currentRow", rows - 1);
            if (aspect->property("currentRow").toInt() != rows - 1)
                wrong << page->displayName() + ": currentRow did not take";
        }
    }

    QVERIFY2(listsChecked > 0, "no page offered a grouped list with rows");
    QVERIFY2(wrong.isEmpty(), qPrintable("grouped lists that disagree: " + wrong.join("; ")));
    qInfo().noquote() << "grouped lists checked:" << listsChecked
                      << "rows round-tripped:" << rowsChecked;
}

void QuickUiTest::testAPageRefusesAnIndexFromSomeoneElsesModel()
{
    // setCurrentIndex() is Q_INVOKABLE, so what reaches it is whatever QML
    // passed - and a QModelIndex carries an internalPointer that only means
    // anything to the model that made it. External Tools casts that pointer to
    // an ExternalTool without asking, so an index from anywhere else is a heap
    // overflow rather than a wrong answer: reverting TreeDelegate's mapToSource
    // aborts this suite under AddressSanitizer inside ExternalTool::preset().
    //
    // The mapping is right and stays. This is the other end of it: a page
    // called with an index it did not make must decline rather than crash.
    Core::setAspectFormFactory([](Utils::AspectContainer *container) {
        return QtcQuick::createAspectForm(container);
    });

    std::unique_ptr<QWidget> host;
    QQuickItem *root = showPage("External Tools", host);
    if (!root)
        QSKIP("The External Tools page is not available here");

    Utils::BaseAspect *tools = nullptr;
    for (QQuickItem *item : findAspectDelegates(root)) {
        if (auto aspect = item->property("aspect").value<Utils::BaseAspect *>()) {
            if (aspect->qmlName() == "Tools")
                tools = aspect;
        }
    }
    QVERIFY2(tools, "the page does not say which aspect holds its tools");

    // A model of someone else's, with an internalPointer of its own.
    QStandardItemModel foreign;
    foreign.appendRow(new QStandardItem("not a tool"));
    const QModelIndex stranger = foreign.index(0, 0);
    QVERIFY(stranger.isValid());

    // Reached by name, the way QML reaches it - which also says the method is
    // still there and still invokable. Returning from it at all is the rest of
    // the assertion: unguarded, this does not return, it aborts the run.
    QVERIFY2(QMetaObject::invokeMethod(tools, "setCurrentIndex",
                                       Q_ARG(QModelIndex, stranger)),
             "setCurrentIndex is no longer callable by name from QML");

    // Asking the page to show what it now thinks is current is what actually
    // dereferences the index, so it is done here rather than left until some
    // later repaint decides to.
    QVERIFY(QMetaObject::invokeMethod(tools, "setCurrentIndex", Q_ARG(QModelIndex, QModelIndex())));
}

void QuickUiTest::testEveryTreeOnEveryPageReportsItsCurrentItem()
{
    // A tree is the table's shape again: the view says which row the cursor is
    // on, a handler hands it to the aspect, and nothing ever operated one. The
    // table version of this was -1 for every page for as long as it existed.
    //
    // Two things are checked, because they fail differently. The delegate has
    // to answer an index in the aspect's *own* model - an index from the filter
    // proxy finds nothing there. And the handler has to run without QML
    // complaining: `aspects.X.setCurrentIndex(...)` is resolved by name at call
    // time, so a setter that is not Q_INVOKABLE, or one whose argument is an
    // int where the delegate hands over a QModelIndex, is a TypeError at
    // runtime and silence everywhere else.
    // Installed here rather than relied on: the census installs the factory and
    // clears it again, and showPage() installs it and does not, so whether a
    // page builds with Quick at this point in the run is a matter of which
    // tests happened to run first. A test that reads global state it did not
    // set passes or fails for reasons that are not about it.
    Core::setAspectFormFactory([](Utils::AspectContainer *container) {
        return QtcQuick::createAspectForm(container);
    });

    QStringList complaints;
    const QMetaObject::Connection warned = QObject::connect(
        QtcQuick::engine(), &QQmlEngine::warnings, QtcQuick::engine(),
        [&complaints](const QList<QQmlError> &errors) {
            for (const QQmlError &error : errors)
                complaints << error.toString();
        });
    const QScopeGuard disconnect([warned] { QObject::disconnect(warned); });

    int treesChecked = 0;
    QStringList silent;

    for (Core::IOptionsPage *page : Core::IOptionsPage::allOptionsPages()) {
        const std::optional<Utils::AspectContainer *> aspects = page->aspects();
        if (!aspects || !*aspects || (*aspects)->qmlSource().isEmpty())
            continue;

        std::unique_ptr<Core::IOptionsPageWidget> widget(page->createWidget());
        if (!widget)
            continue;
        auto quickWidget = widget->findChild<QQuickWidget *>();
        if (!quickWidget || !quickWidget->rootObject())
            continue;

        for (QQuickItem *delegate : findQmlComponents(quickWidget->rootObject(), "TreeDelegate")) {
            QQuickItem *view = findQmlNamed(delegate, "aspectTree").value(0);
            if (!view)
                continue;
            auto shown = view->property("model").value<QAbstractItemModel *>();
            auto selection = view->property("selectionModel").value<QItemSelectionModel *>();
            if (!shown || !selection || shown->rowCount({}) == 0)
                continue;

            ++treesChecked;
            selection->setCurrentIndex(shown->index(0, 0), QItemSelectionModel::SelectCurrent);
            const QModelIndex reported = delegate->property("currentIndex").toModelIndex();
            if (!reported.isValid() || reported.model() == shown)
                silent << page->displayName();
        }
    }

    QVERIFY2(treesChecked > 0, "no page offered a tree with rows, so this checked nothing");
    QVERIFY2(silent.isEmpty(),
             qPrintable("trees that did not report the selected item in the aspect's own model: "
                        + silent.join(", ")));
    QVERIFY2(complaints.isEmpty(),
             qPrintable("QML complained while a tree was being operated:\n"
                        + complaints.join("\n")));
    qInfo().noquote() << "trees asked which item is current:" << treesChecked;
}

void QuickUiTest::testEveryTableOnEveryPageReportsItsCurrentRow()
{
    // The one above says the property works. This says it works on the pages
    // that depend on it, which is where it was broken: six pages hand
    // TableDelegate.currentRow to an aspect, and for as long as it read a
    // property TableView does not have, every one of them was told -1.
    //
    // Deliberately not folded into the page census: that one builds pages and
    // looks at them, and selecting a row runs whatever the page does about it.
    // Kept apart so a side effect there cannot be mistaken for a rendering
    // failure here.
    // Installed here rather than relied on: the census installs the factory and
    // clears it again, and showPage() installs it and does not, so whether a
    // page builds with Quick at this point in the run is a matter of which
    // tests happened to run first. A test that reads global state it did not
    // set passes or fails for reasons that are not about it.
    Core::setAspectFormFactory([](Utils::AspectContainer *container) {
        return QtcQuick::createAspectForm(container);
    });

    int tablesChecked = 0;
    QStringList silent;

    for (Core::IOptionsPage *page : Core::IOptionsPage::allOptionsPages()) {
        const std::optional<Utils::AspectContainer *> aspects = page->aspects();
        if (!aspects || !*aspects || (*aspects)->qmlSource().isEmpty())
            continue;

        std::unique_ptr<Core::IOptionsPageWidget> widget(page->createWidget());
        if (!widget)
            continue;
        auto quickWidget = widget->findChild<QQuickWidget *>();
        if (!quickWidget || !quickWidget->rootObject())
            continue;

        for (QQuickItem *delegate : findQmlComponents(quickWidget->rootObject(), "TableDelegate")) {
            QQuickItem *view = findQmlComponent(delegate, "QQuickTableView");
            if (!view)
                continue;
            auto shown = view->property("model").value<QAbstractItemModel *>();
            auto selection = view->property("selectionModel").value<QItemSelectionModel *>();
            if (!shown || !selection || shown->rowCount({}) == 0)
                continue;

            ++tablesChecked;
            selection->setCurrentIndex(shown->index(0, 0), QItemSelectionModel::SelectCurrent);
            if (delegate->property("currentRow").toInt() != 0)
                silent << page->displayName() + "/" + delegate->property("labelText").toString();
        }
    }

    QVERIFY2(tablesChecked > 0, "no page offered a table with rows, so this checked nothing");
    QVERIFY2(silent.isEmpty(),
             qPrintable("tables that did not report the selected row: " + silent.join(", ")));
    qInfo().noquote() << "tables asked which row is current:" << tablesChecked;
}

void QuickUiTest::testSelectingARowTellsThePageWhichOneItIs()
{
    // A page showing a detail of the current row - the properties of the format
    // in Font && Colors, the text of the snippet in Snippets - reads
    // TableDelegate.currentRow and hands it to its aspect. That was reading
    // TableView.currentIndex, which does not exist: TableView answers
    // currentRow and currentColumn. So it was -1 whatever the user clicked, and
    // no page was ever told which row it was showing.
    Utils::AspectContainer page;
    TestTableAspect table(&page);
    table.setLabelText("Rows");

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);

    // Shown, because an unshown TableView lays out once and then stops
    // following its model - its currentRow would go stale halfway through this.
    form->resize(600, 400);
    form->show();
    QVERIFY(QTest::qWaitForWindowExposed(form.get()));

    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "TableDelegate"));
    QQuickItem *view = nullptr;
    QTRY_VERIFY(view = tableViewOf(quickWidget->rootObject()));
    QTRY_COMPARE(view->property("rows").toInt(), 2);

    QCOMPARE(delegate->property("currentRow").toInt(), -1);

    auto shown = view->property("model").value<QAbstractItemModel *>();
    QVERIFY(shown);
    auto selection = view->property("selectionModel").value<QItemSelectionModel *>();
    QVERIFY(selection);
    selection->setCurrentIndex(shown->index(1, 0), QItemSelectionModel::SelectCurrent);
    QTRY_COMPARE(delegate->property("currentRow").toInt(), 1);
    selection->setCurrentIndex(shown->index(0, 0), QItemSelectionModel::SelectCurrent);
    QTRY_COMPARE(delegate->property("currentRow").toInt(), 0);

    // The answer is meant to be the row in the aspect's *own* model rather than
    // the one at that position in whatever is on screen, which is why it goes
    // through the filter proxy. That half is not asserted here: with a filter
    // applied, currentRow already holds the number the assertion would look
    // for, so it would be satisfied by the state before the filter rather than
    // by the mapping. See the migration doc.
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


void QuickUiTest::testAFieldOffersTheVariablesItCanBeWrittenIn()
{
    // Every line edit on a widget page carries a variable chooser; a Quick
    // page had none, so a value that was meant to be written in terms of
    // %{...} had to be typed from memory.
    Utils::MacroExpander expander;
    expander.setDisplayName("Test");
    expander.registerVariable("Test:Name", "What the test calls itself",
                              [] { return QString("Nemo"); });

    Utils::AspectContainer page;
    page.setAutoApply(false);
    Utils::StringAspect option(&page);
    option.setLabelText("Option");
    option.setDisplayStyle(Utils::StringAspect::LineEditDisplay);
    option.setMacroExpander(&expander);

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);

    QQuickItem *field = nullptr;
    QTRY_VERIFY(field = findQmlComponent(quickWidget->rootObject(), "TextField"));

    const QList<QQuickItem *> buttons = findQmlNamed(field, "insertVariableButton");
    QCOMPARE(buttons.size(), 1);
    QQuickItem * const button = buttons.first();

    // The widget line edit only grows the button while it is being edited, so
    // a form of ten fields is not ten icons. Unfocused it is not there.
    QVERIFY2(!button->isVisible(), "the chooser button is offered on a field nobody is in");

    field->forceActiveFocus();
    QTRY_VERIFY(button->isVisible());
    drawsAnIcon(button, "the insert-variable button");

    // Pressed rather than opened from here: what opens it is a handler in the
    // QML, and calling the popup's own method would leave that untested.
    const QPointF centre = button->mapToScene(
        QPointF(button->width() / 2, button->height() / 2));
    QTest::mouseClick(quickWidget, Qt::LeftButton, Qt::NoModifier, centre.toPoint());

    QObject * const chooser = field->findChild<QObject *>("variableChooser");
    QVERIFY(chooser);
    QTRY_VERIFY(chooser->property("visible").toBool());

    auto rowFor = [chooser](const QString &unexpanded) -> QQuickItem * {
        auto * const popup = chooser->property("contentItem").value<QQuickItem *>();
        if (!popup)
            return nullptr;
        const QList<QQuickItem *> rows = findQmlNamed(popup, "variableRow");
        return Utils::findOr(rows, nullptr, [&unexpanded](QQuickItem *row) {
            return row->property("unexpandedText").toString() == unexpanded;
        });
    };

    // Everything the two expanders offer is in there - far more than fits on
    // screen, which is what the filter above the list is for.
    QQuickItem * const contents = chooser->property("contentItem").value<QQuickItem *>();
    QVERIFY(contents);
    QQuickItem * const filter = findQmlNamed(contents, "variableFilter").value(0);
    QVERIFY(filter);
    filter->setProperty("text", "Test:Name");
    QQuickItem *variable = nullptr;
    QTRY_VERIFY(variable = rowFor("%{Test:Name}"));

    // The list is walked from the filter, so that narrowing it to one variable
    // does not then mean reaching for the mouse.
    bool reached = false;
    for (int i = 0; i < 10 && !reached; ++i) {
        QTest::keyClick(quickWidget, Qt::Key_Down);
        reached = chooser->property("currentText").toString() == "%{Test:Name}";
    }
    QVERIFY2(reached, "the keyboard never reached the variable the filter left on screen");

    // What it is pointing at says what it is for, which is the whole reason
    // for the list.
    QObject * const description = chooser->findChild<QObject *>("variableDescription");
    QVERIFY(description);
    QTRY_VERIFY2(description->property("text").toString().contains("What the test calls itself"),
                 qPrintable("the description says: " + description->property("text").toString()));

    // A group is a heading. Clicking one selects it, says nothing about it,
    // and Enter on it inserts nothing - it has no text of its own.
    // By its row rather than by its place among the delegates: a view keeps
    // those in whatever order it recycled them in.
    const QList<QQuickItem *> rows = findQmlNamed(contents, "variableRow");
    QQuickItem * const group = Utils::findOr(rows, nullptr, [](QQuickItem *row) {
        return row->property("row").toInt() == 0;
    });
    QVERIFY(group);
    QCOMPARE(group->property("unexpandedText").toString(), QString());
    const QPointF onGroup = group->mapToScene(
        QPointF(group->width() / 2, group->height() / 2));
    QTest::mouseClick(quickWidget, Qt::LeftButton, Qt::NoModifier, onGroup.toPoint());
    QTRY_COMPARE(description->property("text").toString(),
                 chooser->property("defaultDescription").toString());
    QVERIFY(!chooser->property("currentChoosable").toBool());
    field->setProperty("text", "ab");
    QTest::keyClick(quickWidget, Qt::Key_Return);
    QCOMPARE(field->property("text").toString(), QString("ab"));
    QVERIFY2(chooser->property("visible").toBool(),
             "choosing nothing closed the chooser anyway");

    // And choosing the variable writes it where the cursor is, unexpanded:
    // the point is to store %{Test:Name}, not what it stands for today.
    reached = false;
    for (int i = 0; i < 10 && !reached; ++i) {
        QTest::keyClick(quickWidget, Qt::Key_Down);
        reached = chooser->property("currentText").toString() == "%{Test:Name}";
    }
    QVERIFY(reached);
    field->setProperty("cursorPosition", 1);
    QTest::keyClick(quickWidget, Qt::Key_Return);
    QTRY_COMPARE(field->property("text").toString(), QString("a%{Test:Name}b"));
    QTRY_VERIFY(!chooser->property("visible").toBool());
}

void QuickUiTest::testASeveralLineFieldOffersVariablesToo()
{
    // The widget renderer puts a chooser on every line edit, text edit and
    // path chooser alike, so a value typed over several lines is no less
    // written in terms of variables than a one-line one.
    Utils::MacroExpander expander;
    expander.setDisplayName("Test");
    expander.registerVariable("Test:Name", "What the test calls itself",
                              [] { return QString("Nemo"); });

    Utils::AspectContainer page;
    page.setAutoApply(false);
    Utils::StringAspect option(&page);
    option.setLabelText("Option");
    option.setDisplayStyle(Utils::StringAspect::TextEditDisplay);
    option.setMacroExpander(&expander);

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);

    // Through the ScrollView: searching for "TextArea" from further up matches
    // TextAreaDelegate itself, which has no text to insert into.
    QQuickItem *delegate = nullptr;
    QTRY_VERIFY(delegate = findQmlComponent(quickWidget->rootObject(), "TextAreaDelegate"));
    QQuickItem * const scroll = findQmlComponent(delegate, "ScrollView");
    QVERIFY(scroll);
    QQuickItem * const area = findQmlComponent(scroll, "TextArea");
    QVERIFY(area);

    QQuickItem * const button = findQmlNamed(quickWidget->rootObject(),
                                             "insertVariableButton").value(0);
    QVERIFY(button);
    QVERIFY2(!button->isVisible(), "the chooser button is offered on a field nobody is in");

    area->forceActiveFocus();
    QTRY_VERIFY(button->isVisible());
    drawsAnIcon(button, "the insert-variable button of a text area");

    const QPointF centre = button->mapToScene(
        QPointF(button->width() / 2, button->height() / 2));
    QTest::mouseClick(quickWidget, Qt::LeftButton, Qt::NoModifier, centre.toPoint());

    QObject * const chooser = delegate->findChild<QObject *>("variableChooser");
    QVERIFY(chooser);
    QTRY_VERIFY(chooser->property("visible").toBool());

    // And what it offers goes into the text, at the cursor rather than over
    // whatever is already there.
    QQuickItem * const filter = findQmlNamed(
        chooser->property("contentItem").value<QQuickItem *>(), "variableFilter").value(0);
    QVERIFY(filter);
    filter->setProperty("text", "Test:Name");
    // The list is narrowed a frame later; walking it before then walks
    // nothing.
    QTRY_VERIFY(Utils::anyOf(findQmlNamed(chooser->property("contentItem").value<QQuickItem *>(),
                                          "variableRow"),
                             [](QQuickItem *row) {
                                 return row->property("unexpandedText").toString()
                                        == "%{Test:Name}";
                             }));
    bool reached = false;
    for (int i = 0; i < 10 && !reached; ++i) {
        QTest::keyClick(quickWidget, Qt::Key_Down);
        reached = chooser->property("currentText").toString() == "%{Test:Name}";
    }
    QVERIFY2(reached, "the keyboard never reached the variable the filter left on screen");

    area->setProperty("text", "ab");
    area->setProperty("cursorPosition", 1);
    QTest::keyClick(quickWidget, Qt::Key_Return);
    QTRY_COMPARE(area->property("text").toString(), QString("a%{Test:Name}b"));
}

// An aspect that stands in for ProjectExplorer's EnvironmentEditorAspect: the
// component is in this module and reads its container by name, so what it
// needs is the names, not that plugin's class.
class StandInVariables final : public Utils::BaseAspect
{
    Q_OBJECT

public:
    explicit StandInVariables(Utils::AspectContainer *container)
        : BaseAspect(container)
    {}

    Utils::AspectPresentation presentation() const override
    {
        Utils::AspectPresentation p = BaseAspect::presentation();
        p.control = Utils::AspectControls::Table;
        return p;
    }

    QAbstractItemModel *tableModel() override { return &m_model; }

    Q_INVOKABLE void setCurrentRow(int row) { m_currentRow = row; }
    int currentRow() const { return m_currentRow; }

    Utils::EnvironmentModel m_model{this};
    int m_currentRow = -1;
};

void QuickUiTest::testAnEnvironmentEditorDrawsItsTableAndItsButtons()
{
    // The environment editor every page that edits one draws: a table of what
    // the variables come out as, the operations on whichever is current, and
    // the same changes as text.
    Utils::AspectContainer editor;
    editor.setAutoApply(true);

    StandInVariables variables(&editor);
    variables.setQmlName("Variables");
    Utils::Environment base;
    base.set("QTC_EDITOR_TEST", "one");
    variables.m_model.setBaseEnvironment(base);

    Utils::StringAspect changes(&editor);
    changes.setQmlName("Changes");
    changes.setDisplayStyle(Utils::StringAspect::TextEditDisplay);

    std::vector<std::unique_ptr<Utils::ActionAspect>> actions;
    for (const QString &name : QStringList{"Edit", "Add", "Reset", "Unset", "Toggle",
                                           "AppendPath", "PrependPath", "OpenTerminal"}) {
        auto action = std::make_unique<Utils::ActionAspect>(&editor);
        action->setQmlName(name);
        action->setActionText(name);
        actions.push_back(std::move(action));
    }

    QQmlComponent component(QtcQuick::engine(),
                            QUrl("qrc:/qt/qml/QtCreator/Ui/EnvironmentEditor.qml"));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    const std::unique_ptr<QObject> object(component.createWithInitialProperties(
        {{"editor", QVariant::fromValue(QtcQuick::AspectModels().named(&editor))}}));
    QVERIFY(object);
    auto * const item = qobject_cast<QQuickItem *>(object.get());
    QVERIFY(item);

    const std::unique_ptr<QQuickWidget> host(new QQuickWidget);
    item->setParentItem(host->quickWindow()->contentItem());
    host->resize(900, 500);
    host->show();
    QVERIFY(QTest::qWaitForWindowExposed(host.get()));

    // The variables the environment comes out with, drawn as rows. The rows
    // are what matters: a TableDelegate is built whether or not the name it
    // was given resolves to anything, so finding one proves nothing.
    QQuickItem *table = nullptr;
    QTRY_VERIFY(table = findQmlComponent(item, "TableDelegate"));
    QQuickItem *view = nullptr;
    QTRY_VERIFY(view = findQmlComponent(table, "QQuickTableView"));
    QTRY_VERIFY2(view->property("rows").toInt() > 0,
                 "the table shows no variables, so it was given no model");

    // And one button per operation, each showing what it does.
    const QList<QQuickItem *> buttons = findQmlComponents(item, "ButtonDelegate");
    QCOMPARE(buttons.size(), 8);
    QStringList shown;
    for (QQuickItem * const button : buttons) {
        // Not the delegate itself: "ButtonDelegate" starts with "Button", so
        // the prefix search answers it first and it has no text of its own.
        const QList<QQuickItem *> parts = findQmlComponents(button, "Button");
        for (QQuickItem * const part : parts) {
            if (part != button)
                shown << part->property("text").toString();
        }
    }
    for (const QString &name : QStringList{"Edit", "Add", "Reset", "Unset", "Toggle",
                                           "AppendPath", "PrependPath", "OpenTerminal"}) {
        QVERIFY2(shown.contains(name), qPrintable("no button says " + name));
    }

    // The other surface, under them.
    QVERIFY(findQmlComponent(item, "TextAreaDelegate"));
}

void QuickUiTest::testANestedContainerIsDrawnWithTheQmlItNames()
{
    // A container inside a page can name QML of its own, and then that is what
    // draws it. Drawn generically instead it would lose whatever that file
    // draws and no generic form can: the run configuration settings of the
    // Perf profiler, Valgrind and the QML profiler are all such containers,
    // and Perf's holds an events table.
    Utils::AspectContainer page;
    page.setAutoApply(false);

    Utils::AspectContainer nested(&page);
    nested.setQmlName("Nested");
    nested.setLabelText("Nested");
    // A file that takes the "aspects" seam a page is given - not
    // AspectForm.qml, which is the *generic* form and requires a model. What
    // is checked is which file was loaded, not what it draws, so a page of
    // another plugin's does as well as any.
    nested.setQmlSource(
        QUrl("qrc:/qt/qml/QtCreator/ProjectExplorer/ProjectCommentsPanel.qml"));
    Utils::BoolAspect inner(&nested);
    inner.setLabelText("Inner");

    const std::unique_ptr<QWidget> form(showForm(&page));
    QVERIFY(form);
    auto quickWidget = form->findChild<QQuickWidget *>();
    QVERIFY(quickWidget);

    QQuickItem *group = nullptr;
    QTRY_VERIFY(group = findQmlComponent(quickWidget->rootObject(), "GroupDelegate"));

    // The Loader is what tells the two apart: both paths put items in the
    // group, and the generic one draws the same aspect just as happily.
    QQuickItem *loader = nullptr;
    QTRY_VERIFY(loader = findQmlComponent(group, "QQuickLoader"));
    QTRY_COMPARE(loader->property("source").toUrl(), nested.qmlSource());
}

void QuickUiTest::testFormattedOutputCanBeDrawnByQtQuick()
{
    // Six output panes are text streams on Core::OutputWindow, which is a
    // QPlainTextEdit. What decides whether they can be drawn with Qt Quick at
    // all is whether the document Utils::OutputFormatter fills can be handed to
    // a Quick view - if it cannot, every one of them needs a line model and the
    // formatter has to be rewritten to feed it.
    //
    // It can. This is that, end to end, so the answer is a test rather than a
    // paragraph.
    QTextDocument document;
    Utils::OutputFormatter formatter;
    formatter.setSink(&document);
    formatter.appendMessage("a plain line\n", Utils::NormalMessageFormat);
    formatter.appendMessage("something went wrong\n", Utils::ErrorMessageFormat);
    formatter.flush();

    QVERIFY(document.toPlainText().contains("a plain line"));
    QVERIFY(document.toPlainText().contains("something went wrong"));

    // The two lines are not drawn alike, which is the whole point of a
    // formatter and the thing a plain string model would lose.
    const QTextCharFormat plainFormat =
        document.findBlockByNumber(0).begin().fragment().charFormat();
    const QTextCharFormat errorFormat =
        document.findBlockByNumber(1).begin().fragment().charFormat();
    QVERIFY2(plainFormat.foreground() != errorFormat.foreground(),
             "the formatter drew an error the same as ordinary output");

    QQmlComponent component(QtcQuick::engine());
    component.setData(R"(
        import QtQuick.Controls
        TextArea { readOnly: true }
    )", QUrl());
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    const std::unique_ptr<QObject> object(component.create());
    QVERIFY(object);

    auto * const quickDocument = object->property("textDocument").value<QQuickTextDocument *>();
    QVERIFY2(quickDocument, "a TextArea has no document to substitute into");
    quickDocument->setTextDocument(&document);

    // What the view shows is what the formatter wrote.
    QCOMPARE(object->property("text").toString(), document.toPlainText());

    // And it keeps showing it: an output pane appends for as long as something
    // is running, so a view that only took a copy at handover would stop after
    // the first line.
    formatter.appendMessage("and one more\n", Utils::NormalMessageFormat);
    formatter.flush();
    QVERIFY2(object->property("text").toString().contains("and one more"),
             "the view stopped following the document it was given");
}

void QuickUiTest::testHidingALineIsNotHowAQuickViewFilters()
{
    // An output pane filters by hiding text blocks: filterNewContent() walks
    // the document calling QTextBlock::setVisible(). That is honoured by
    // QPlainTextEdit, whose layout is a QPlainTextDocumentLayout and knows to
    // skip them. Whether a Qt Quick view does is what decides how the six
    // text-stream panes can filter at all, so it is asked here rather than
    // assumed.
    QTextDocument document;
    document.setPlainText("first line\nsecond line\nthird line");
    QCOMPARE(document.blockCount(), 3);

    QQmlComponent component(QtcQuick::engine());
    component.setData(R"(
        import QtQuick.Controls
        TextArea { readOnly: true; width: 400 }
    )", QUrl());
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    const std::unique_ptr<QObject> object(component.create());
    QVERIFY(object);
    auto * const area = qobject_cast<QQuickItem *>(object.get());
    QVERIFY(area);

    auto * const quickDocument = object->property("textDocument").value<QQuickTextDocument *>();
    QVERIFY(quickDocument);
    quickDocument->setTextDocument(&document);

    const std::unique_ptr<QQuickWidget> host(new QQuickWidget);
    area->setParentItem(host->quickWindow()->contentItem());
    host->resize(400, 300);
    host->show();

    QTRY_VERIFY(object->property("contentHeight").toReal() > 0);
    const qreal allThree = object->property("contentHeight").toReal();

    // Hide the middle one, the way the filter does.
    document.findBlockByNumber(1).setVisible(false);
    document.markContentsDirty(0, document.characterCount());
    QCoreApplication::processEvents();

    const qreal withOneHidden = object->property("contentHeight").toReal();

    // Before concluding anything from a height that did not move: show that it
    // moves at all. A measurement that never changes proves nothing by
    // staying put, and this one is read one line after the change that is
    // supposed to shrink it.
    document.setPlainText("first line\nsecond line\nthird line\nfourth line");
    QTRY_VERIFY2(object->property("contentHeight").toReal() > allThree,
                 "the height does not follow the document at all, so what it says about "
                 "a hidden line means nothing");

    qInfo() << "content height with three lines:" << allThree
            << "with the middle one hidden:" << withOneHidden;
    QVERIFY2(qFuzzyCompare(allThree, withOneHidden),
             "a Qt Quick view honours QTextBlock::setVisible after all - if this fails, "
             "the output panes can keep filtering the way they do now");
}

void QuickUiTest::testTheOutputViewShowsWhatAFormatterWrites()
{
    // The view the six text-stream panes are headed for. It holds no text of
    // its own: a pane hands it the document its formatter writes into, which
    // is what carries the colours and the links the line parsers left.
    QTextDocument document;
    Utils::OutputFormatter formatter;
    formatter.setSink(&document);
    formatter.appendMessage("compiling everything\n", Utils::NormalMessageFormat);
    formatter.appendMessage("it went wrong\n", Utils::ErrorMessageFormat);
    formatter.flush();

    const std::unique_ptr<QtcQuick::OutputView> view(new QtcQuick::OutputView);
    view->setDocument(&document);
    view->resize(600, 300);
    view->show();

    QQuickItem * const root = qobject_cast<QQuickItem *>(view->rootObject());
    QVERIFY(root);
    QQuickItem * const area = root->findChild<QQuickItem *>("outputText");
    QVERIFY2(area, "the view draws no text at all");

    // What the formatter wrote is what the view shows.
    QTRY_COMPARE(area->property("text").toString(), document.toPlainText());
    QVERIFY(area->property("text").toString().contains("it went wrong"));

    // And it keeps showing it. A pane appends for as long as something runs,
    // so a view that took a copy when it was handed the document would stop
    // at the first two lines.
    formatter.appendMessage("and again\n", Utils::ErrorMessageFormat);
    formatter.flush();
    QTRY_VERIFY2(area->property("text").toString().contains("and again"),
                 "the view stopped following the document it was given");

    // Read-only, because output is not edited, and selectable, because
    // copying an error out of the pane is half of what a pane is for.
    QVERIFY(area->property("readOnly").toBool());
    QVERIFY(area->property("selectByMouse").toBool());
}

namespace {

// A parser that turns one word into a link, the way a compiler parser turns a
// file name into one. Used so the test exercises the formatter's own link
// path - OutputFormatter::linkFormat() setting the anchor - rather than an
// anchor written by hand, which would prove nothing about real output.
class WordLinkingParser : public Utils::OutputLineParser
{
public:
    WordLinkingParser(const QString &word, const QString &target)
        : m_word(word), m_target(target) {}

    Result handleLine(const QString &line, Utils::OutputFormat) override
    {
        const int pos = line.indexOf(m_word);
        if (pos < 0)
            return Status::NotHandled;
        return {Status::Done, {{pos, int(m_word.size()), m_target}}};
    }

private:
    const QString m_word;
    const QString m_target;
};

} // namespace

void QuickUiTest::testZoomIsPointsAddedWithAFloor()
{
    // What a zoom value means, shared by the widget output window and the Qt
    // Quick one because a pane hands one view's zoom to its others.
    QFont base;
    base.setPointSizeF(12);

    QCOMPARE(Utils::StyleHelper::zoomedFont(base, 0).pointSizeF(), 12.0);
    QCOMPARE(Utils::StyleHelper::zoomedFont(base, 3).pointSizeF(), 15.0);
    QCOMPARE(Utils::StyleHelper::zoomedFont(base, -4).pointSizeF(), 8.0);

    // Points added, not a percentage of the size - which is what the text
    // editor's zoom is, and the difference is invisible at 100%.
    QFont larger;
    larger.setPointSizeF(24);
    QCOMPARE(Utils::StyleHelper::zoomedFont(larger, 3).pointSizeF(), 27.0);

    // Zooming out stops rather than reaching zero and vanishing.
    QCOMPARE(Utils::StyleHelper::zoomedFont(base, -100).pointSizeF(),
             double(Utils::StyleHelper::minimumZoomedFontSize));

    // And it is the size alone that the zoom touches.
    base.setFamily("Courier");
    base.setBold(true);
    const QFont zoomed = Utils::StyleHelper::zoomedFont(base, 2);
    QCOMPARE(zoomed.family(), QString("Courier"));
    QVERIFY(zoomed.bold());
}

void QuickUiTest::testTheOutputViewZoomsTheTextItDraws()
{
    QTextDocument document;
    document.setPlainText("a line of output\nand another one\n");

    const std::unique_ptr<QtcQuick::OutputView> view(new QtcQuick::OutputView);
    QFont base;
    base.setPointSizeF(12);
    view->setBaseFont(base);
    view->setDocument(&document);
    view->resize(600, 300);
    view->show();

    QQuickItem * const root = qobject_cast<QQuickItem *>(view->rootObject());
    QVERIFY(root);
    QQuickItem * const area = root->findChild<QQuickItem *>("outputText");
    QVERIFY(area);

    // The zoom has to reach the glyphs, not just be remembered. A pane's
    // "Reset Zoom" and its fan-out to sibling views both go through here.
    QTRY_COMPARE(area->property("font").value<QFont>().pointSizeF(), 12.0);
    const qreal unzoomedHeight = area->property("contentHeight").toReal();
    QVERIFY(unzoomedHeight > 0);

    view->setFontZoom(8);
    QCOMPARE(view->fontZoom(), 8.0f);
    QTRY_COMPARE(area->property("font").value<QFont>().pointSizeF(), 20.0);
    QTRY_VERIFY2(area->property("contentHeight").toReal() > unzoomedHeight,
                 "the text is drawn at the same height after zooming in");

    view->resetZoom();
    QTRY_COMPARE(area->property("font").value<QFont>().pointSizeF(), 12.0);
}

void QuickUiTest::testCtrlWheelZoomsTheOutputViewAndPlainWheelDoesNot()
{
    QTextDocument document;
    document.setPlainText("a line of output\n");

    const std::unique_ptr<QtcQuick::OutputView> view(new QtcQuick::OutputView);
    QFont base;
    base.setPointSizeF(12);
    view->setBaseFont(base);
    view->setDocument(&document);
    view->resize(600, 300);
    view->show();

    QQuickItem * const area
        = qobject_cast<QQuickItem *>(view->rootObject())->findChild<QQuickItem *>("outputText");
    QVERIFY(area);
    QTRY_VERIFY(area->property("contentHeight").toReal() > 0);

    QSignalSpy wheelZooms(view.get(), &QtcQuick::OutputView::wheelZoom);
    const QPointF pos(50, 10);

    const auto sendWheel = [&](int degrees, Qt::KeyboardModifiers modifiers) {
        QWheelEvent event(pos, view->mapToGlobal(pos.toPoint()), {}, QPoint(0, degrees),
                          Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
        // Sent to the QQuickWidget, which is what forwards to the Quick scene.
        // The OutputView itself is the wrapper around it and forwards nothing,
        // so an event addressed there is quietly dropped.
        QCoreApplication::sendEvent(view->quickWidget(), &event);
    };

    // A plain wheel scrolls. It must not resize the output, or reading a long
    // build log would change its size all the way down.
    sendWheel(120, Qt::NoModifier);
    QCOMPARE(view->fontZoom(), 0.0f);
    QCOMPARE(wheelZooms.count(), 0);

    // Ctrl+wheel zooms, one notch to one point, as in the widget window.
    sendWheel(120, Qt::ControlModifier);
    QTRY_COMPARE(view->fontZoom(), 1.0f);
    QTRY_COMPARE(wheelZooms.count(), 1);

    // The pane can switch it off - the widget window has setWheelZoomEnabled
    // and the app output pane turns it off when the setting says so.
    view->setWheelZoomEnabled(false);
    sendWheel(120, Qt::ControlModifier);
    QCOMPARE(view->fontZoom(), 1.0f);
}

void QuickUiTest::testTheOutputViewSeesTheLinksAFormatterWrote()
{
    // The risk this checks: the formatter marks links as QTextCharFormat
    // anchors, and nothing said a Quick text item would find them in a
    // document it was handed rather than one it parsed itself.
    const QString target = "file:///tmp/broken.cpp::17::0";

    QTextDocument document;
    Utils::OutputFormatter formatter;
    formatter.setSink(&document);
    // The formatter takes ownership of its parsers.
    formatter.setLineParsers({new WordLinkingParser("broken.cpp", target)});
    formatter.appendMessage("broken.cpp: it went wrong\n", Utils::StdErrFormat);
    formatter.flush();

    const std::unique_ptr<QtcQuick::OutputView> view(new QtcQuick::OutputView);
    view->setDocument(&document);
    view->resize(600, 300);
    view->show();

    QQuickItem * const area
        = qobject_cast<QQuickItem *>(view->rootObject())->findChild<QQuickItem *>("outputText");
    QVERIFY(area);
    QTRY_VERIFY(area->property("contentHeight").toReal() > 0);

    // Asked at the middle of the first line, which the document's own layout
    // says where is. Halfway down the view is not it: a one-line document is
    // two blocks tall, so that lands under the text and finds nothing.
    const QRectF firstLine
        = document.documentLayout()->blockBoundingRect(document.firstBlock());
    const qreal lineMiddle = firstLine.center().y();

    QString found;
    for (qreal x = 1; x < 60 && found.isEmpty(); x += 2)
        found = view->linkAt(x, lineMiddle);
    QCOMPARE(found, target);

    // And the rest of the same line is not a link. On the same line on
    // purpose: anywhere else it would come back empty for the duller reason
    // that there is no text there.
    QVERIFY2(view->linkAt(120, lineMiddle).isEmpty(), "text with no link reported one");

    // Clicking one tells the pane, which is where handleLink() is called.
    QSignalSpy activations(view.get(), &QtcQuick::OutputView::linkActivated);
    const QPoint onTheLink(10, qRound(lineMiddle));
    QTest::mousePress(view->quickWidget(), Qt::LeftButton, {}, onTheLink);
    QTest::mouseRelease(view->quickWidget(), Qt::LeftButton, {}, onTheLink);
    QTRY_COMPARE(activations.count(), 1);
    QCOMPARE(activations.first().first().toString(), target);
}

void QuickUiTest::testCoreHandsOutAnOutputViewItCannotDrawItself()
{
    // The seam the panes need. Everything here is reached through Core alone,
    // which is all a pane has: Core cannot link a Qt Quick library, and the
    // plugins holding the panes cannot reach past Core to one.
    const std::unique_ptr<Core::OutputView> view(Core::createOutputView());
    QVERIFY2(view.get(), "Core hands out no output view at all");

    QTextDocument document;
    Utils::OutputFormatter formatter;
    formatter.setSink(&document);
    // The formatter takes ownership of its parsers.
    const QString target = "file:///tmp/broken.cpp::17::0";
    formatter.setLineParsers({new WordLinkingParser("broken.cpp", target)});
    formatter.appendMessage("broken.cpp: it went wrong\n", Utils::StdErrFormat);
    formatter.flush();

    QFont base;
    base.setPointSizeF(12);
    view->setBaseFont(base);
    view->setDocument(&document);
    QCOMPARE(view->document(), &document);
    view->resize(600, 300);
    view->show();

    // Reached the way a test has to, not the way a pane would: what is drawn
    // is behind an interface that deliberately does not say what draws it.
    auto * const quickWidget = view->findChild<QQuickWidget *>();
    QVERIFY2(quickWidget, "the view Core handed out draws nothing with Qt Quick");
    QQuickItem * const area
        = quickWidget->rootObject()->findChild<QQuickItem *>("outputText");
    QVERIFY(area);

    QTRY_COMPARE(area->property("text").toString(), document.toPlainText());
    QTRY_VERIFY(area->property("contentHeight").toReal() > 0);

    // Each call has to arrive on the other side. A forwarding layer that drops
    // one is silent: the pane sets something and nothing happens.
    view->setFontZoom(8);
    QCOMPARE(view->fontZoom(), 8.0f);
    QTRY_COMPARE(area->property("font").value<QFont>().pointSizeF(), 20.0);
    view->resetZoom();
    QTRY_COMPARE(area->property("font").value<QFont>().pointSizeF(), 12.0);

    // And each signal has to come back. Both cross two objects here.
    QSignalSpy wheelZooms(view.get(), &Core::OutputView::wheelZoom);
    QWheelEvent wheel(QPointF(50, 10), quickWidget->mapToGlobal(QPoint(50, 10)), {},
                      QPoint(0, 120), Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase,
                      false);
    QCoreApplication::sendEvent(quickWidget, &wheel);
    QTRY_COMPARE(wheelZooms.count(), 1);
    QCOMPARE(view->fontZoom(), 1.0f);

    QSignalSpy activations(view.get(), &Core::OutputView::linkActivated);
    const QRectF firstLine
        = document.documentLayout()->blockBoundingRect(document.firstBlock());
    const QPoint onTheLink(10, qRound(firstLine.center().y()));
    QTest::mousePress(quickWidget, Qt::LeftButton, {}, onTheLink);
    QTest::mouseRelease(quickWidget, Qt::LeftButton, {}, onTheLink);
    QTRY_COMPARE(activations.count(), 1);
    QCOMPARE(activations.first().first().toString(), target);
}

void QuickUiTest::testCoreHasNoOutputViewWithoutAFrontEnd()
{
    // The documented answer when nothing installed a factory, which is what
    // lets a pane keep its QPlainTextEdit rather than lose its output. The
    // aspect form seam gets this wrong in one of its two functions - see
    // createAspectForm(), which falls back and so can never say no - and that
    // cost a batch, so this one is asserted rather than assumed.
    Core::setOutputViewFactory({});
    const QScopeGuard restore([] {
        Core::setOutputViewFactory([](QWidget *parent) -> Core::OutputView * {
            return new QuickOutputView(parent);
        });
    });
    QVERIFY(!Core::createOutputView());
}

void QuickUiTest::testAVariableBeingDefinedIsNotOfferedForItself()
{
    // A variable whose value is being edited must not be offered as part of
    // its own value. The widget tree lists it and refuses it; a Quick view
    // cannot read item flags, so the model has to say so as data.
    Utils::MacroExpander expander;
    expander.setDisplayName("Test");
    expander.registerVariable("Test:Name", "What the test calls itself",
                              [] { return QString("Nemo"); });
    expander.registerVariable("Test:Other", "Something else",
                              [] { return QString("Other"); });

    Utils::AspectContainer page;
    Utils::StringAspect option(&page);
    option.setMacroExpander(&expander);

    QtcQuick::AspectModels models;
    QAbstractItemModel * const rows = models.variables(&option);
    QVERIFY(rows);
    auto * const source = option.findChild<Utils::VariableModel *>();
    QVERIFY(source);

    const auto roleOf = [rows](const QByteArray &name) {
        return rows->roleNames().key(name);
    };
    // At any depth: an expander that defers to another is a group inside a
    // group, which is where an aspect's own variables end up.
    const std::function<QModelIndex(const QModelIndex &, const QString &)> findIn =
        [&](const QModelIndex &parent, const QString &unexpanded) {
            for (int i = 0; i < rows->rowCount(parent); ++i) {
                const QModelIndex row = rows->index(i, 0, parent);
                if (row.data(roleOf("unexpandedText")).toString() == unexpanded)
                    return row;
                if (const QModelIndex found = findIn(row, unexpanded); found.isValid())
                    return found;
            }
            return QModelIndex();
        };
    const auto findRow = [&](const QString &unexpanded) { return findIn({}, unexpanded); };

    QVERIFY2(findRow("%{Test:Name}").isValid(),
             "the variables were not fetched, so nothing could be filtered either");
    QVERIFY(findRow("%{Test:Name}").data(roleOf("selectable")).toBool());

    source->setCurrentVariableName("Test:Name");
    QVERIFY2(!findRow("%{Test:Name}").data(roleOf("selectable")).toBool(),
             "a variable was offered as a value for itself");
    QVERIFY2(findRow("%{Test:Other}").data(roleOf("selectable")).toBool(),
             "every other variable was refused along with it");

    // And the filter narrows the list without losing the group above it.
    QVERIFY(QMetaObject::invokeMethod(rows, "setFilterFixedString",
                                      Q_ARG(QString, "Other")));
    QVERIFY2(findRow("%{Test:Other}").isValid(),
             "the filter dropped what it matched, or the group holding it");
    QVERIFY2(!findRow("%{Test:Name}").isValid(), "the filter kept what it did not match");
}

QObject *createQuickUiTest()
{
    return new QuickUiTest;
}

} // namespace QuickUi::Internal

#include "quickui_test.moc"
