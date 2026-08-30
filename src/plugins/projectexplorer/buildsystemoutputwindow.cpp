// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "buildsystemoutputwindow.h"

#include "projectexplorerconstants.h"
#include "projectexplorertr.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/find/optionspopup.h>
#include <coreplugin/icontext.h>
#include <coreplugin/outputview.h>

#include <texteditor/fontsettings.h>

#include <utils/fancylineedit.h>
#include <utils/qtcassert.h>
#include <utils/stylehelper.h>
#include <utils/widgets.h>
#include <utils/utilsicons.h>

#include <coreplugin/find/ifindsupport.h>
#include <utils/aggregate.h>
#include <QQuickItem>
#include <QQuickWidget>
#include <QTest>
#include <QHBoxLayout>
#include <QVBoxLayout>

using namespace Core;
using namespace Utils;

namespace ProjectExplorer::Internal {

const char kBuildSystemOutputContext[] = "ProjectsMode.BuildSystemOutput";
const char kRegExpActionId[] = "OutputFilter.RegularExpressions.BuildSystemOutput";
const char kCaseSensitiveActionId[] = "OutputFilter.CaseSensitive.BuildSystemOutput";
const char kInvertActionId[] = "OutputFilter.Invert.BuildSystemOutput";

BuildSystemOutputView::BuildSystemOutputView(QWidget *parent)
    : QWidget(parent)
    , m_startOfNewContent(&m_source)
{
    m_startOfNewContent.setKeepPositionOnInsert(true);
    m_formatter.setSink(&m_source);

    auto * const layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_view = Core::createOutputView();
    QTC_ASSERT(m_view, return);
    layout->addWidget(m_view);
    m_view->setDocument(&m_source);
}

void BuildSystemOutputView::appendMessage(const QString &text, OutputFormat format)
{
    m_formatter.appendMessage(text, format);
    m_formatter.flush();

    // Only what arrived is filtered, not the whole document again: a build
    // system appends thousands of times.
    if (isFiltering()) {
        OutputWindow::appendFiltered(&m_source, &m_filtered,
                                     OutputWindow::filterPredicate(m_filterText, m_filterMode),
                                     0, 0, m_appendState);
    }
}

void BuildSystemOutputView::grayOutOldContent()
{
    OutputWindow::grayOutContentBefore(m_startOfNewContent, palette());

    // Dimming changes the colours of text the filtered copy already took, so
    // that copy has to be made again. Only while a filter is set, which is the
    // rare case.
    if (isFiltering())
        refilter();
}

void BuildSystemOutputView::clear()
{
    m_formatter.clear();
    m_source.clear();
    m_filtered.clear();
    m_appendState = {};
    m_startOfNewContent.setPosition(0);
}

void BuildSystemOutputView::clearLinesPrefixedWith(const QString &prefix,
                                                   bool deleteTrailingLineBreak)
{
    OutputWindow::removeLinesPrefixedWith(&m_source, prefix, deleteTrailingLineBreak);

    // Lines were taken out of the middle, so what the filtered copy holds no
    // longer lines up with where it had got to.
    if (isFiltering())
        refilter();
}

bool BuildSystemOutputView::isFiltering() const
{
    return !m_filterText.isEmpty();
}

void BuildSystemOutputView::setFilter(const QString &text, OutputWindow::FilterModeFlags mode)
{
    if (m_filterText == text && m_filterMode == mode)
        return;
    m_filterText = text;
    m_filterMode = mode;
    refilter();
}

void BuildSystemOutputView::refilter()
{
    QTC_ASSERT(m_view, return);

    m_filtered.clear();
    m_appendState = {};
    if (isFiltering()) {
        // Filtering everything at once and filtering it a line at a time are
        // the same operation from an empty state, so there is one code path.
        OutputWindow::appendFiltered(&m_source, &m_filtered,
                                     OutputWindow::filterPredicate(m_filterText, m_filterMode),
                                     0, 0, m_appendState);
    }
    m_view->setDocument(isFiltering() ? &m_filtered : &m_source);
}

QTextDocument *BuildSystemOutputView::shownDocument() const
{
    return isFiltering() ? const_cast<QTextDocument *>(&m_filtered)
                         : const_cast<QTextDocument *>(&m_source);
}

void BuildSystemOutputView::setBaseFont(const QFont &font)
{
    QTC_ASSERT(m_view, return);
    m_view->setBaseFont(font);
}

void BuildSystemOutputView::zoomIn()
{
    QTC_ASSERT(m_view, return);
    m_view->setFontZoom(m_view->fontZoom() + 1);
}

void BuildSystemOutputView::zoomOut()
{
    QTC_ASSERT(m_view, return);
    m_view->setFontZoom(m_view->fontZoom() - 1);
}

BuildSystemOutputWindow::BuildSystemOutputWindow()
    : m_view(new BuildSystemOutputView)
{
    auto * const layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    IContext::attach(this, Context(kBuildSystemOutputContext));

    Command *clearCommand = ActionManager::command(Core::Constants::OUTPUTPANE_CLEAR);
    m_clear.setIcon(Utils::Icons::CLEAN_TOOLBAR.icon());
    m_clear.setText(clearCommand->action()->text());
    ActionManager::registerAction(&m_clear,
                                  Core::Constants::OUTPUTPANE_CLEAR,
                                  Context(kBuildSystemOutputContext));
    connect(&m_clear, &QAction::triggered, m_view, &BuildSystemOutputView::clear);

    m_filterActionRegexp.setCheckable(true);
    m_filterActionRegexp.setText(Tr::tr("Use Regular Expressions"));
    connect(&m_filterActionRegexp, &QAction::toggled, this,
            &BuildSystemOutputWindow::updateFilter);
    ActionManager::registerAction(&m_filterActionRegexp,
                                  kRegExpActionId,
                                  Context(Constants::C_PROJECTEXPLORER));

    m_filterActionCaseSensitive.setCheckable(true);
    m_filterActionCaseSensitive.setText(Tr::tr("Case Sensitive"));
    connect(&m_filterActionCaseSensitive, &QAction::toggled, this,
            &BuildSystemOutputWindow::updateFilter);
    ActionManager::registerAction(&m_filterActionCaseSensitive,
                                  kCaseSensitiveActionId,
                                  Context(Constants::C_PROJECTEXPLORER));

    m_invertFilterAction.setCheckable(true);
    m_invertFilterAction.setText(Tr::tr("Show Non-matching Lines"));
    connect(&m_invertFilterAction, &QAction::toggled, this,
            &BuildSystemOutputWindow::updateFilter);
    ActionManager::registerAction(&m_invertFilterAction,
                                  kInvertActionId,
                                  Context(Constants::C_PROJECTEXPLORER));

    connect(&TextEditor::globalFontSettings(), &TextEditor::FontSettings::changed,
            this,
            [this] { m_view->setBaseFont(TextEditor::globalFontSettings().data().font()); });
    m_view->setBaseFont(TextEditor::globalFontSettings().data().font());

    m_zoomIn.setIcon(Utils::Icons::PLUS_TOOLBAR.icon());
    m_zoomIn.setText(ActionManager::command(Core::Constants::ZOOM_IN)->action()->text());
    connect(&m_zoomIn, &QAction::triggered, m_view, &BuildSystemOutputView::zoomIn);
    ActionManager::registerAction(&m_zoomIn,
                                  Core::Constants::ZOOM_IN,
                                  Context(kBuildSystemOutputContext));

    m_zoomOut.setIcon(Utils::Icons::MINUS_TOOLBAR.icon());
    m_zoomOut.setText(ActionManager::command(Core::Constants::ZOOM_OUT)->action()->text());
    connect(&m_zoomOut, &QAction::triggered, m_view, &BuildSystemOutputView::zoomOut);
    ActionManager::registerAction(&m_zoomOut,
                                  Core::Constants::ZOOM_OUT,
                                  Context(kBuildSystemOutputContext));
}

void BuildSystemOutputWindow::appendMessage(const QString &text, OutputFormat format)
{
    m_view->appendMessage(text, format);
}

void BuildSystemOutputWindow::grayOutOldContent()
{
    m_view->grayOutOldContent();
}

void BuildSystemOutputWindow::clearLinesPrefixedWith(const QString &prefix,
                                                     bool deleteTrailingLineBreak)
{
    m_view->clearLinesPrefixedWith(prefix, deleteTrailingLineBreak);
}

QWidget *BuildSystemOutputWindow::toolBar()
{
    if (!m_toolBar) {
        m_toolBar = new StyledBar(this);
        auto clearButton
            = Command::toolButtonWithAppendedShortcut(&m_clear, Core::Constants::OUTPUTPANE_CLEAR);

        m_filterOutputLineEdit = new FancyLineEdit;
        m_filterOutputLineEdit->setButtonVisible(FancyLineEdit::Left, true);
        m_filterOutputLineEdit->setButtonIcon(FancyLineEdit::Left, Utils::Icons::MAGNIFIER.icon());
        m_filterOutputLineEdit->setFiltering(true);
        m_filterOutputLineEdit->setHistoryCompleter("ProjectsMode.BuildSystemOutput.Filter");
        m_filterOutputLineEdit->setAttribute(Qt::WA_MacShowFocusRect, false);
        connect(m_filterOutputLineEdit, &FancyLineEdit::textChanged, this,
                &BuildSystemOutputWindow::updateFilter);
        connect(m_filterOutputLineEdit, &FancyLineEdit::returnPressed, this,
                &BuildSystemOutputWindow::updateFilter);
        connect(m_filterOutputLineEdit, &FancyLineEdit::leftButtonClicked, this, [this] {
            auto popup = new OptionsPopup(m_filterOutputLineEdit,
                                          {kRegExpActionId,
                                           kCaseSensitiveActionId,
                                           kInvertActionId});
            popup->show();
        });

        auto zoomInButton = Command::toolButtonWithAppendedShortcut(&m_zoomIn,
                                                                   Core::Constants::ZOOM_IN);
        auto zoomOutButton = Command::toolButtonWithAppendedShortcut(&m_zoomOut,
                                                                    Core::Constants::ZOOM_OUT);

        auto layout = new QHBoxLayout;
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        m_toolBar->setLayout(layout);
        layout->addWidget(clearButton);
        layout->addWidget(m_filterOutputLineEdit);
        layout->addWidget(zoomInButton);
        layout->addWidget(zoomOutButton);
        layout->addStretch();
    }
    return m_toolBar;
}

void BuildSystemOutputWindow::updateFilter()
{
    if (!m_filterOutputLineEdit)
        return;

    using Flag = OutputWindow::FilterModeFlag;
    OutputWindow::FilterModeFlags mode;
    if (m_filterActionRegexp.isChecked())
        mode |= Flag::RegExp;
    if (m_filterActionCaseSensitive.isChecked())
        mode |= Flag::CaseSensitive;
    if (m_invertFilterAction.isChecked())
        mode |= Flag::Inverted;

    m_view->setFilter(m_filterOutputLineEdit->text(), mode);
}

#ifdef WITH_TESTS

class BuildSystemOutputTest final : public QObject
{
    Q_OBJECT

    // The view part only: it registers no commands, so a test may build one.
    // BuildSystemOutputWindow itself claims fixed action ids and there is
    // already one of those in the running instance.
    static QQuickItem *drawnText(BuildSystemOutputView *view)
    {
        auto * const quickWidget = view->findChild<QQuickWidget *>();
        if (!quickWidget || !quickWidget->rootObject())
            return nullptr;
        return quickWidget->rootObject()->findChild<QQuickItem *>("outputText");
    }

private slots:
    void testWhatIsWrittenIsShown()
    {
        BuildSystemOutputView view;
        view.resize(400, 200);
        view.show();
        QQuickItem * const text = drawnText(&view);
        QVERIFY2(text, "the build system output draws nothing with Qt Quick");

        view.appendMessage("running cmake\n", Utils::GeneralMessageFormat);
        view.appendMessage("it went wrong\n", Utils::ErrorMessageFormat);
        QTRY_COMPARE(text->property("text").toString(), view.shownDocument()->toPlainText());
        QVERIFY(text->property("text").toString().contains("it went wrong"));

        view.clear();
        QTRY_VERIFY(text->property("text").toString().isEmpty());
    }

    void testAFilterShowsOnlyTheLinesThatMatch()
    {
        BuildSystemOutputView view;
        view.resize(400, 200);
        view.show();
        QQuickItem * const text = drawnText(&view);
        QVERIFY(text);

        view.appendMessage("configuring\n", Utils::GeneralMessageFormat);
        view.appendMessage("error: nothing works\n", Utils::GeneralMessageFormat);
        view.appendMessage("done\n", Utils::GeneralMessageFormat);

        // A Qt Quick view cannot be filtered by hiding blocks, so what it is
        // shown is a second document holding the lines that pass.
        view.setFilter("error", {});
        QTRY_VERIFY(!text->property("text").toString().contains("configuring"));
        QVERIFY(text->property("text").toString().contains("error: nothing works"));

        // What arrives while a filter is set is filtered too, without the
        // whole document being done again.
        view.appendMessage("error: still nothing\n", Utils::GeneralMessageFormat);
        QTRY_VERIFY(text->property("text").toString().contains("still nothing"));
        view.appendMessage("almost there\n", Utils::GeneralMessageFormat);
        QVERIFY(!text->property("text").toString().contains("almost there"));

        // Inverted, the other lines are the ones left.
        view.setFilter("error", Core::OutputWindow::FilterModeFlag::Inverted);
        QTRY_VERIFY(text->property("text").toString().contains("configuring"));
        QVERIFY(!text->property("text").toString().contains("nothing works"));

        // Clearing it puts the whole thing back, including what was hidden.
        view.setFilter({}, {});
        QTRY_VERIFY(text->property("text").toString().contains("configuring"));
        QVERIFY(text->property("text").toString().contains("almost there"));
    }

    void testRetractingLinesByPrefix()
    {
        BuildSystemOutputView view;
        view.appendMessage("[cmake] step one\n", Utils::GeneralMessageFormat);
        view.appendMessage("kept\n", Utils::GeneralMessageFormat);
        view.appendMessage("[cmake] step two\n", Utils::GeneralMessageFormat);
        // Deliberately not the last line. Taking the last one takes the empty
        // block a trailing newline leaves with it, and what is appended next
        // then lands on the end of the line before - which the widget did too,
        // and is not this port's to change.
        view.appendMessage("and kept\n", Utils::GeneralMessageFormat);

        view.clearLinesPrefixedWith("[cmake]", true);
        QVERIFY(!view.shownDocument()->toPlainText().contains("step one"));
        QVERIFY(view.shownDocument()->toPlainText().contains("kept"));

        // And with a filter set, where the filtered copy has to be built
        // again - the lines went out of the middle of what it was following.
        view.appendMessage("[cmake] step three\n", Utils::GeneralMessageFormat);
        view.appendMessage("error: here\n", Utils::GeneralMessageFormat);
        view.appendMessage("last line\n", Utils::GeneralMessageFormat);
        view.setFilter("e", {});
        QVERIFY(view.shownDocument()->toPlainText().contains("step three"));
        view.clearLinesPrefixedWith("[cmake]", true);
        QVERIFY2(!view.shownDocument()->toPlainText().contains("step three"),
                 "a retracted line stayed in the filtered copy");
        QVERIFY(view.shownDocument()->toPlainText().contains("error: here"));
    }

    void testDimmingAndZoomReachTheView()
    {
        BuildSystemOutputView view;
        QVERIFY(view.view());

        view.appendMessage("the last run\n", Utils::GeneralMessageFormat);
        QTextCursor cursor(view.shownDocument());
        cursor.setPosition(1);
        const QColor before = cursor.charFormat().foreground().color();

        view.grayOutOldContent();
        cursor.setPosition(1);
        QVERIFY2(cursor.charFormat().foreground().color() != before,
                 "starting a new run did not dim what the last one left");

        const float zoom = view.view()->fontZoom();
        view.zoomIn();
        QCOMPARE(view.view()->fontZoom(), zoom + 1);
        view.zoomOut();
        view.zoomOut();
        QCOMPARE(view.view()->fontZoom(), zoom - 1);
    }

    void testFindSupportIsReachableFromTheOutputArea()
    {
        // The find tool bar is anchored to the area, not to the view inside
        // it, and finds its way down by asking. Losing that is invisible:
        // Ctrl+F simply does nothing.
        BuildSystemOutputView view;
        view.appendMessage("a needle in here\n", Utils::GeneralMessageFormat);

        Core::IFindSupport *find = Utils::Aggregation::query<Core::IFindSupport>(view.view());
        QVERIFY2(find, "nothing under the output area answers as find support");
        QCOMPARE(find->findStep("needle", {}), Core::IFindSupport::Found);
    }
};

QObject *createBuildSystemOutputTest()
{
    return new BuildSystemOutputTest;
}

#endif // WITH_TESTS

} // namespace ProjectExplorer::Internal

#ifdef WITH_TESTS
#include "buildsystemoutputwindow.moc"
#endif
