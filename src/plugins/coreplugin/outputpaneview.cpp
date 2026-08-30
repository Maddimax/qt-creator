// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "outputpaneview.h"

#include "icore.h"
#include "outputview.h"
#include "messagemanager.h"
#include "ioutputpane.h"

#include <utils/fancylineedit.h>
#include <utils/qtcassert.h>
#include <utils/qtcsettings.h>

#include "find/ifindsupport.h"
#include <utils/aggregate.h>
#include <QTest>
#include <QApplication>
#include <QVBoxLayout>

using namespace Utils;

namespace Core {

OutputPaneView::OutputPaneView(const Key &zoomSettingsKey, QWidget *parent)
    : QWidget(parent)
    , m_startOfNewContent(&m_source)
    , m_zoomSettingsKey(zoomSettingsKey)
{
    m_startOfNewContent.setKeepPositionOnInsert(true);
    m_formatter.setSink(&m_source);

    auto * const layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    if (!m_zoomSettingsKey.isEmpty()) {
        m_zoom = ICore::settings()->value(m_zoomSettingsKey).toFloat();
        connect(ICore::instance(), &ICore::saveSettingsRequested, this, [this] {
            ICore::settings()->setValueWithDefault(m_zoomSettingsKey, fontZoom(), 0.f);
        });
    }

    view();
}

OutputView *OutputPaneView::view()
{
    if (m_view)
        return m_view;

    m_view = createOutputView();
    if (!m_view)
        return nullptr;

    layout()->addWidget(m_view);
    m_view->setDocument(shownDocument());
    if (m_baseFontSet)
        m_view->setBaseFont(m_baseFont);
    m_view->setFontZoom(m_zoom);
    m_view->setWheelZoomEnabled(m_wheelZoomEnabled);
    return m_view;
}

void OutputPaneView::showEvent(QShowEvent *event)
{
    // The last moment it can be made, and the first one at which every plugin
    // has certainly initialized. A pane built during startup gets its view
    // here rather than never.
    view();
    QWidget::showEvent(event);
}

OutputPaneView::~OutputPaneView() = default;

void OutputPaneView::appendMessage(const QString &text, OutputFormat format)
{
    m_formatter.appendMessage(text, format);
    m_formatter.flush();

    // Only what arrived is filtered, not the whole document again: a build
    // appends thousands of times.
    if (isFiltering()) {
        OutputWindow::appendFiltered(&m_source, &m_filtered,
                                     OutputWindow::filterPredicate(m_filterText, m_filterMode),
                                     m_beforeContext, m_afterContext, m_appendState);
    }
}

void OutputPaneView::grayOutOldContent()
{
    OutputWindow::grayOutContentBefore(m_startOfNewContent, palette());

    // Dimming changes the colours of text the filtered copy already took, so
    // that copy has to be made again. Only while a filter is set.
    if (isFiltering())
        refilter();
}

void OutputPaneView::clear()
{
    m_formatter.clear();
    m_source.clear();
    m_filtered.clear();
    m_appendState = {};
    m_startOfNewContent.setPosition(0);
}

void OutputPaneView::clearLinesPrefixedWith(const QString &prefix, bool deleteTrailingLineBreak)
{
    OutputWindow::removeLinesPrefixedWith(&m_source, prefix, deleteTrailingLineBreak);

    // Lines went out of the middle, so what the filtered copy holds no longer
    // lines up with where it had got to.
    if (isFiltering())
        refilter();
}

bool OutputPaneView::isFiltering() const
{
    return !m_filterText.isEmpty();
}

void OutputPaneView::setFilter(const QString &text, OutputWindow::FilterModeFlags mode,
                               int before, int after)
{
    if (m_filterText == text && m_filterMode == mode && m_beforeContext == before
        && m_afterContext == after) {
        return;
    }
    m_filterText = text;
    m_filterMode = mode;
    m_beforeContext = before;
    m_afterContext = after;
    refilter();
}

void OutputPaneView::refilter()
{
    m_filtered.clear();
    m_appendState = {};
    if (isFiltering()) {
        // Filtering everything at once and filtering it a line at a time are
        // the same operation from an empty state, so there is one code path.
        OutputWindow::appendFiltered(&m_source, &m_filtered,
                                     OutputWindow::filterPredicate(m_filterText, m_filterMode),
                                     m_beforeContext, m_afterContext, m_appendState);
    }
    if (OutputView * const output = view())
        output->setDocument(shownDocument());
}

QTextDocument *OutputPaneView::shownDocument() const
{
    return isFiltering() ? const_cast<QTextDocument *>(&m_filtered)
                         : const_cast<QTextDocument *>(&m_source);
}

QString OutputPaneView::toPlainText() const
{
    return m_source.toPlainText();
}

void OutputPaneView::setBaseFont(const QFont &font)
{
    m_baseFont = font;
    m_baseFontSet = true;
    if (OutputView * const output = view())
        output->setBaseFont(font);
}

void OutputPaneView::setWheelZoomEnabled(bool enabled)
{
    m_wheelZoomEnabled = enabled;
    if (OutputView * const output = view())
        output->setWheelZoomEnabled(enabled);
}

void OutputPaneView::zoomIn()
{
    setFontZoom(m_zoom + 1);
}

void OutputPaneView::zoomOut()
{
    setFontZoom(m_zoom - 1);
}

void OutputPaneView::resetZoom()
{
    setFontZoom(0);
}

void OutputPaneView::setFontZoom(float zoom)
{
    m_zoom = zoom;
    if (OutputView * const output = view())
        output->setFontZoom(zoom);
}

float OutputPaneView::fontZoom() const
{
    return m_zoom;
}

#ifdef WITH_TESTS

class OutputPaneViewTest final : public QObject
{
    Q_OBJECT

    // The shared view: it registers no commands, so a test may build one.
    // The panes around it claim fixed action ids and there is already one of
    // each of those in the running instance.
    // Asked of the document handed to the view, not of the glyphs. What this
    // class decides is *which* document is shown and what is in it; that a
    // view draws the document it is given is Core::OutputView's part, and is
    // checked against the rendered frame in the QuickUi tests. Core does not
    // link Qt Quick, which is the whole point of the seam.
    static QString shownText(const OutputPaneView &view)
    {
        return view.shownDocument()->toPlainText();
    }

private slots:
    void testWhatIsWrittenIsShown()
    {
        OutputPaneView view;
        QVERIFY2(view.view(), "the output pane view has nothing to draw with");

        view.appendMessage("running cmake\n", Utils::GeneralMessageFormat);
        view.appendMessage("it went wrong\n", Utils::ErrorMessageFormat);
        QVERIFY(shownText(view).contains("it went wrong"));

        // And the view was given that document rather than a copy of it.
        QCOMPARE(view.view()->document(), view.shownDocument());

        view.clear();
        QVERIFY(shownText(view).isEmpty());
    }

    void testAFilterShowsOnlyTheLinesThatMatch()
    {
        OutputPaneView view;
        QVERIFY(view.view());

        view.appendMessage("configuring\n", Utils::GeneralMessageFormat);
        view.appendMessage("error: nothing works\n", Utils::GeneralMessageFormat);
        view.appendMessage("done\n", Utils::GeneralMessageFormat);

        // A Qt Quick view cannot be filtered by hiding blocks, so what it is
        // shown is a second document holding the lines that pass.
        view.setFilter("error", {});
        QCOMPARE(view.view()->document(), view.shownDocument());
        QVERIFY(!shownText(view).contains("configuring"));
        QVERIFY(shownText(view).contains("error: nothing works"));

        // What arrives while a filter is set is filtered too, without the
        // whole document being done again.
        view.appendMessage("error: still nothing\n", Utils::GeneralMessageFormat);
        QVERIFY(shownText(view).contains("still nothing"));
        view.appendMessage("almost there\n", Utils::GeneralMessageFormat);
        QVERIFY(!shownText(view).contains("almost there"));

        // Inverted, the other lines are the ones left.
        view.setFilter("error", OutputWindow::FilterModeFlag::Inverted);
        QVERIFY(shownText(view).contains("configuring"));
        QVERIFY(!shownText(view).contains("nothing works"));

        // Clearing it puts the whole thing back, including what was hidden.
        view.setFilter({}, {});
        QVERIFY(shownText(view).contains("configuring"));
        QVERIFY(shownText(view).contains("almost there"));
    }

    void testRetractingLinesByPrefix()
    {
        OutputPaneView view;
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
        OutputPaneView view;
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

    void testAFilterAlsoShowsTheLinesAroundAMatch()
    {
        // General Messages asks for context lines; Build System Output does
        // not. Passing them through was the difference between the two panes,
        // so it is worth its own case.
        OutputPaneView view;
        for (const QString &line : QStringList{"one", "two", "the match", "four", "five"})
            view.appendMessage(line + '\n', Utils::GeneralMessageFormat);

        view.setFilter("match", {}, 0, 0);
        QCOMPARE(shownText(view), QString("the match"));

        view.setFilter("match", {}, 1, 1);
        QCOMPARE(shownText(view), QString("two\nthe match\nfour"));

        view.setFilter("match", {}, 2, 0);
        QCOMPARE(shownText(view), QString("one\ntwo\nthe match"));
    }

    void testAPaneHandsOutItsTextWithoutHandingOutItsWidget()
    {
        // The contract that replaced outputWindows(). A reader wants the text,
        // not a QPlainTextEdit it then calls toPlainText() on - which is what
        // stopped any pane from being drawn with anything else.
        IOutputPane *general = nullptr;
        for (IOutputPane * const pane : IOutputPane::allOutputPanes()) {
            if (pane->id() == Utils::Id("GeneralMessages"))
                general = pane;
        }
        QVERIFY2(general, "there is no General Messages pane");

        const QString written = "a line only this test writes";
        MessageManager::writeSilently(written);

        const QStringList texts = general->outputTexts();
        QCOMPARE(texts.size(), 1);
        QVERIFY2(texts.first().contains(written),
                 "the pane did not hand out the text it was given");

        // The pane's own filter, driven through the line edit it puts in the
        // tool bar - the only way in from outside, and the only thing that
        // calls its updateFilter().
        Utils::FancyLineEdit *filterEdit = nullptr;
        for (QWidget * const widget : general->toolBarWidgets()) {
            if (auto * const edit = qobject_cast<Utils::FancyLineEdit *>(widget))
                filterEdit = edit;
        }
        QVERIFY2(filterEdit, "the pane offers no way to filter it");

        MessageManager::writeSilently("a second line, matching nothing");
        filterEdit->setText(written);
        const QScopeGuard clearFilter([filterEdit] { filterEdit->setText({}); });

        // A filter is about what is shown, not about what the pane holds: a
        // reader asking a pane for its output should get all of it, not
        // whatever happens to be on screen.
        QVERIFY2(general->outputTexts().first().contains("matching nothing"),
                 "a filter took lines out of what the pane hands to a reader");

        // But it does change what is shown. Found by looking for the view
        // holding what this test wrote, because the pane keeps it private and
        // asking outputWidget() for it would reparent the running instance's
        // pane out of its own window.
        OutputPaneView *shown = nullptr;
        for (QWidget * const widget : QApplication::allWidgets()) {
            if (auto * const candidate = qobject_cast<OutputPaneView *>(widget)) {
                if (candidate->toPlainText().contains(written))
                    shown = candidate;
            }
        }
        QVERIFY2(shown, "the General Messages view could not be found");
        QVERIFY2(!shown->shownDocument()->toPlainText().contains("matching nothing"),
                 "the pane's filter never reached its view");
        QVERIFY(shown->shownDocument()->toPlainText().contains(written));
    }

    void testFindSupportIsReachableFromTheOutputArea()
    {
        // The find tool bar is anchored to the area, not to the view inside
        // it, and finds its way down by asking. Losing that is invisible:
        // Ctrl+F simply does nothing.
        OutputPaneView view;
        view.appendMessage("a needle in here\n", Utils::GeneralMessageFormat);

        IFindSupport *find = Utils::Aggregation::query<IFindSupport>(view.view());
        QVERIFY2(find, "nothing under the output area answers as find support");
        QCOMPARE(find->findStep("needle", {}), IFindSupport::Found);
    }
};

QObject *createOutputPaneViewTest()
{
    return new OutputPaneViewTest;
}

#endif // WITH_TESTS

} // namespace Core

#ifdef WITH_TESTS
#include "outputpaneview.moc"
#endif
