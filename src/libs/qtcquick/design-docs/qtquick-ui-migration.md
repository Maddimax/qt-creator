# Replacing the Qt Creator UI with Qt Quick

Working plan and status. Written to be picked up again after a break: the
"Status" and "Next steps" sections are the resume points.

## Goal

A Qt Creator whose user interface is entirely Qt Quick, with no QtWidgets in the
shipped product.

Today the UI is QtWidgets throughout: ~2.26 M lines of C++ under `src/`, ~1200
files touching QtWidgets, 486 classes deriving from a widget base, and a visual
identity carried by `ManhattanStyle` (a 1583-line `QProxyStyle` covering ~33
primitives) plus hand-painted widgets. There is no stylesheet and no declarative
theme to translate, so the QML design system is new work, not a port.

## Decisions

- **End state:** zero QtWidgets.
- **Sequencing:** a second app target (`qtcreator-quick`) reusing the non-UI
  logic libraries. The widget UI keeps shipping until parity, then is deleted.
- **Plugin API:** the widget-based plugin API survives in an opt-in
  `widgetcompat` plugin, the only component linking QtWidgets. Core, Utils and
  in-tree plugins become QtWidgets-free; default builds can switch the shim off.
- **Text editor:** a custom `QQuickItem` over `QTextDocument`/`QTextLayout`,
  reusing Creator's document, highlighter and code-assist model.
- **QmlDesigner:** ported early; its panels are already QML.
- **Qt gaps:** solved Creator-side first, pushed upstream opportunistically.
- **Squish suite:** dropped, not ported.
- **Theme switching must work live, without a restart.** See below.
- **`Utils::Layouting` is removed, not ported.** It does not get a Quick
  backend. Layout is expressed in `.qml` files. See below.

One caveat, stated once. This repo has been moving the other way: `src/libs/tracing`
had a complete Qt Quick implementation and was migrated to `QWidget` +
`QCanvasPainter`/`QPainter` (`8f33b44a0a0` → `2acc23b3560`), the Welcome page was
QML and was rewritten in widgets, and
`src/plugins/profiler/design-docs/native-mixed-profiler-design.md` names "a new
rendering stack" as a non-goal. What failed there was *QML for high-density custom
rendering*. The artefact it left behind, `src/libs/tracing/trackpainterbase.h`'s
`enum class TrackBackend { Automatic, Gpu, Software }` with a QRectF/QRgb-only
API, is the seam to reuse for the editor, timeline and flame graph. Rule: custom
rendering goes inside a `QQuickItem` behind a painter abstraction, never into
thousands of QML items.

## Live theme switching

A hard requirement for the new UI: changing the theme takes effect immediately.

Today it does not. `themechooser.cpp:154` calls
`ICore::askForRestart("The theme change will take effect after restart.")`, and
nothing listens to `QStyleHints::colorSchemeChanged`.

**This invalidates a decision taken in phase 1.** The token singletons were
built on the assumption that the theme is immutable for the process lifetime:

- `QtcQuick::DesignSystem` derives from `Utils::Theme` via
  `Utils::Theme(Utils::creatorTheme(), parent)`, which takes a **copy** of the
  theme at construction. A later theme change does not reach it at all.
- `Tokens.qml` and `Fonts.qml` bind to `Theme.color(...)` and
  `Theme.uiFont(...)`, which are `Q_INVOKABLE`. A QML binding over an invokable
  with no notify signal never re-evaluates.
- `DesignSystem::changed()` exists as the seam but is never emitted.

What live theming requires, in order:

1. ~~`DesignSystem` must not snapshot.~~ **Done.** It forwards to
   `Utils::creatorTheme()` and is no longer a `Utils::Theme`. The enums stay
   reachable as `ThemeColor.Token_*` through a foreign registration.
2. ~~Per-token notifying properties.~~ **Done.** `Tokens` and `Fonts` are
   generated C++ singletons with a real property and `NOTIFY changed` per token,
   produced by `generate-tokens.py` from `Theme::Color` and
   `StyleHelper::UiElement`. `Tokens.qml` and `Fonts.qml` are gone. The plan had
   rejected this on the grounds that no notification was needed; with live
   theming that reasoning was void.
3. ~~`setCreatorTheme()` has to announce the change.~~ **Done.**
   `Utils::ThemeWatcher` outlives the themes that `setCreatorTheme()` deletes and
   emits `themeChanged()`.
   `tests/auto/qtcquick/designsystem` asserts a QML binding follows a theme swap,
   using two themes with deliberately different accent colours; it goes red if
   the notification is removed.
4. **The caches have to be audited.** Partly done. **45** function-local statics
   hold a *resolved* colour, brush, pixmap or icon derived from the theme and
   latch it for the life of the process. `Utils::ThemedValue` recomputes such a
   value after a theme change, keyed on a generation counter that
   `setCreatorTheme()` bumps; dropping the `static` instead would re-tint on
   every paint. Where the value is a plain colour lookup rather than a tinted
   pixmap, the `static` simply goes: `creatorColor()` is a lookup and
   `QPalette::text()` a copy, so caching bought nothing and only made them stale.

   Everything outside `qmldesigner` and `scxmleditor` is converted: the combo box
   arrows in `qtdesignwidgets`, the progress view's pin icon, the Axivion overlay
   icons, the Extension Manager checkmarks, the welcome page session icon, and
   two plain colours in `loggingviewer` and `flamegraphwidget`. What remains is
   about 25 sites in `qmldesigner` plus a few in `scxmleditor`, both last in the
   porting order.

   Note that `Utils::TextFormat` is **not** a hazard: it stores the
   `Theme::Color` role and resolves it in `color()`, so the ~21
   `static const TextFormat` instances hold no colour. An earlier version of this
   document claimed otherwise. There are ~69 `static const QColor` /
   `static QColor` and ~21 `static const TextFormat` initialisations across
   `src/plugins` and `src/libs`. Every one of them latches a colour on first use
   and will show the old theme after a switch. These are invisible while a
   restart is mandatory, and each becomes a bug the moment it is not.
5. **The widget side needs care while both UIs exist.** A theme now owns its
   base palette, and `setThemeApplicationPalette()` lives in its own translation
   unit; it deliberately still uses `QApplication::setPalette`, which propagates
   to existing widgets where the `QGuiApplication` one does not. What is left
   here is `ManhattanStyle` and `StyleHelper::setBaseColor`, which hold derived
   colours.

Items 1-3 are done. Item 4 is the remaining work and the one that will take the
time: it is a cross-cutting audit, not a localised fix. Item 5 overlaps with the
`Theme::palette()` blocker below.

The gallery (`tests/manual/quick/gallery`) demonstrates the result: switching its
theme selector recolours the scene in place, without recreating it.

## Scrolling: where the time actually went

Measured rather than guessed, on a five thousand line file with a nine hundred
pixel view, timing the phases of `updatePolish()`:

| | before | after |
| --- | --- | --- |
| a whole layout | 1.32 ms | 0.43 ms |
| shaping the rows | 0.92 ms | 0.19 ms |
| selections and parentheses | 0.04 ms | 0.04 ms |

**Seventy percent of a layout was shaping text that had not changed.** Every
layout built a fresh `QTextLayout` for every row on screen, including the rows
that had been there a moment earlier saying the same thing. A row is now kept
when it would be shaped identically - same text, same formats - and the font,
tab stop and wrap width are remembered so that any of *them* moving throws
every row away rather than inviting an argument about which ones survive.

Two things worth keeping in mind about those numbers: this build has
AddressSanitizer on (`-fsanitize=address` is in the compile line), so the
absolute figures are inflated, and the "45 ms per layout" the first
measurement seemed to show was `QTRY_COMPARE` polling, not work.

**And then the measurement itself turned out to be measuring the wrong end.**
The 1.32 ms above was a timer that stopped before the *end* of
`updatePolish()`. Timing the whole thing gives **10 to 19 ms a layout**, and
the shape of it is:

| | per layout |
| --- | --- |
| shaping rows | 0.19 ms |
| building the list QML reads | 0.51 ms (was ~3.2 ms - see below) |
| everything else, almost all of it QML | ~9 ms |

`visibleLines` is a `QVariantList` property - a map of twenty-odd entries per
row - and **eight repeaters bind to it**. Each binding read rebuilt the whole
list, so a scroll built it eight times at ~0.4 ms each. It is built once with
the layout now.

**That was the big one, and it is done: about 10 ms a layout down to about
5 ms.** Emitting `linesChanged` handed every repeater a brand new list, so all
eight threw away their delegates and made new ones - several hundred QML items
created and destroyed per scroll step. The rows are a `QAbstractListModel`
now, and because its one role is called **`modelData`** a delegate reads a row
exactly as it did from a list: eight repeaters converted by changing the model
they bind to and nothing else. The row count rarely changes while scrolling,
so the delegates stay and only what they read is announced.

**And announcing less took it to about 3.6 ms.** "Compare the rows and
announce only what differs" was the obvious next step and it is wrong on its
own terms: rows are numbered by where they are on screen, so a scroll changes
every one of them. What is true is that they *move* - what row five said is
what row four says now, word for word - so the model looks for that shift and
reports it as rows leaving the top and arriving at the bottom. The rows
between are not announced at all.

The shift is found by **comparing rows**, not by working it out from
`firstVisibleLine`. A row that compares equal says the same thing whatever the
reason it moved, and one that does not is re-read whatever the scroll says, so
the shortcut cannot show anything stale. Measuring a layout that announces
nothing at all gives 2.4 ms, so of the ~2.6 ms that announcing cost, this
recovers about 1.4 ms and the rest is the comparing and the arriving rows.

**A control here does not bite and it is worth saying why.** Removing the loop
that verifies the whole overlap changes no test. The reason is that a row's
map carries its line number, so matching the first row already pins the
alignment; the verification only earns its keep when a scroll and a content
change land in the *same* layout, and in practice they do not - three attempts
at constructing one failed. It stays, because that is what makes correctness
independent of "in practice", but it is not covered.

**And the first version of that test asked the wrong object.** It read
`viewport->visibleLine()`, which is the view's own copy and is right whatever
the model was told - so a model left holding a stale row would not have shown
up. The test asks the model now.

**What I guessed first was wrong.** The obvious suspect was the loop over
`m_highlights` inside the per-block loop - O(visible x highlights) by the look
of it. Reading it showed a `std::lower_bound` per set, so it is already cheap,
and the measurement put selections at three percent of a layout. Guessing
would have optimised the wrong thing.

**The test for this had to be told to wait.** Asserting on the layout right
after one scroll failed about half the time in class-only runs: a view still
settling its geometry lays out at a different width, and a row shaped at
another width cannot be kept - correctly. Measuring the *second* scroll makes
it stable. The control for the text comparison does not bite and I have left
it that way rather than pretend: rows are keyed by where they start, which
already tells them apart, so comparing the text is defensive rather than
load-bearing.

## The Quick editor is the default now

Registered before the plain text editor, so a text file - and, since the
lookup walks a mime type's parents, a source file - opens in it. The reason
recorded for not doing this, "no completion or auto-insertion", had stopped
being true; both have tests. The plain text editor is still offered beside it.

**Two things do not survive it, and neither is fixed:**

- **The inline diff is broken.** `DiffEditorPlugin` casts the source editor to
  `BaseTextEditor` and reaches for its `TextEditorWidget`; the Quick editor is
  neither. Three tests fail on it. This is the ghost-row feature the Quick
  editor has never had - the same reason `markDiffChangeSigns` is unported -
  so it is a known gap arriving somewhere new rather than a fresh one.
- **Core's tabbed editor lost a tab** - **fixed**. `duplicate()` had always
  answered, but `IEditor::duplicateSupported()` defaults to false and nothing
  set it. The editor manager *asks* rather than tries, so it moved the editor
  between split views instead of copying it, and the first view dropped the
  tab it was keeping. One line, and Core is 86/86 again. The bug was there all
  along; becoming the default is what made anything ask.

**`testIndentGuidesFollowTheIndentation` fails about one run in two and is not
this change's doing.** Measured both ways - the editor default swapped back and
forth, three runs each - and the rate is the same: `103/2, 103/2, 103/2`
against `103/2, 104/1, 103/2`. Which test fails varies between runs, which is
the signature of the focus contention these `QQuickView` tests have always been
prone to. It is worth someone's time; it is not worth blaming on the editor
that happens to be default.

**Almost everything alarming about the first measurement was my own mess.**
The first sweep after the change reported 27 failures in TextEditor, a hang in
ProjectExplorer, and a suite that produced no test output at all. All of it
came from Qt Creator instances I had killed earlier: the log says
`Failed to initialize instances shared memory`, and a new instance waits on the
single-instance state a dead one left behind. Killing every stray process and
running again gave **315/315 in TextEditor and ProjectExplorer back to its
usual three**. This is written down in the memory as "a startup hang right
after a timeout kill means state, not a regression", and it still took three
wrong readings before I applied it. **Clear the processes before measuring, not
after being surprised.**

## Status

Branch `utils-drop-printsupport`, 227 commits, not pushed.

**The settings-page migration is finished.** Every `IOptionsPage` that can be
aspect driven is: the census reports **0 pages still on widgets**, with no
exemptions, in both the ordinary build and one with ClangFormat loaded. The
widget fallback is gone - `CodeStyleAspect`'s layouter, the factory's value
editor API, `CodeStyleEditor`, `codestyleselectorwidget` and
`CppCodeStylePreferencesWidget` are all deleted - and a language that names no
form of its own now gets `CodeStyleDefaultPage.qml` rather than a widget. The
only `IOptionsPage` still building one wraps Qt Designer's own
`QDesignerOptionsPageInterface`, which is not ours to port.

Verified one suite at a time, re-run in full after the shared headers changed
(`result.h`, `aspectpresentation.h`) since those reach everything:

| suite | result |
| --- | --- |
| Core, TextEditor, QuickUi | 81/81, 312/312, 89/89 |
| QmlJSEditor, QmlJSTools, LanguageClient | 15/15, 12/12, 20/20 |
| CMake, Qmake, Qbs, Nim project managers | 69/69, 26/26, 8/8 (1 skipped), 6/6 |
| AutoTest, BareMetal | 5/5 (1 skipped, wants a kit), 61/61 |
| DiffEditor, Python | 48/48, 30/30 |
| all 35 `tests/auto/utils` binaries | every one exit 0, 2003 assertions between them |
| CppEditor | 155/155 for `FollowSymbolTest` alone; the whole suite poisons itself and its number means nothing |
| ProjectExplorer, Debugger | 2 and 1 pre-existing failures |
| FakeVim | 253/2, both pre-existing - see below |

Re-measured after the editor batches, because those changed an **exported
header** (`textdocumentlayout.h`) that twelve other plugins include: building
proves it compiles, not that it still behaves. The `tests/auto/utils` binaries
are 35 now rather than 33 - this branch added `result` and `aspectrenderer` -
and matter most here, since `utils` itself was changed early on.

`ClangFormat` is not built in this configuration: asking for it says "the
plugin does not exist" and returns nothing, which reads exactly like a clean
pass if only the totals are looked at. `GlslEditor`, `VcsBase`, `Copilot` and
`Lua` register no test classes at all - also zero, also not a pass.

### The minimap, and a reason to defer that did not hold

It was put off twice as "O(file), against the viewport's O(visible) premise".
That reasoning is wrong on inspection: the premise is about **scrolling**, and
a minimap is a separate item paying its own cost - the widget editor pays
exactly the same one. What it is *not* is a change to the row model, unlike
the between-lines annotation alignment, which really does need rows of
different heights and really is a decision to take deliberately.

It is done now, in two halves. `Core::renderMinimap()` draws a document as one
pixel per character, coloured by the highlighter, and it needs a document, a
font and two colours - nothing else. `MinimapOverlay` asked its editor for all
of that, which is what made the picture a widget's to draw. The pixel loop
moved unchanged; everything read from the editor is passed in, including how
much room a view that can scroll past the end needs.

It has tests now, which it could not have before: a line with text is drawn
and an empty one is not, a folded line takes no room, and asking for room to
scroll past the end adds exactly that much. Three controls, one per test.

The second half is `MinimapView`, a `QQuickPaintedItem` beside the text: where
the picture goes, how much of it is showing, and dragging the marked part to
scroll. It takes no width at all until the setting asks for it, so the text is
exactly as wide as it was. Only the marked part is a handle - a press anywhere
else does nothing, which is what the widget editor does rather than jumping to
where it was clicked.

**And it was drawn again on every layout, which is worse than it sounds.** The
first version marked the picture stale on `metricsChanged`. That is emitted at
the end of *every* `updatePolish()` - scrolling, moving the caret, typing - so
each of them threw away a file-sized image and drew it again on the next
frame. Nothing looked wrong; it was simply doing the most expensive thing in
the editor several times a second.

It follows the document now: contents, font settings, and the replacement that
happens when a file is reopened. Scrolling moves the marked part and nothing
else. `pictureChanged()` says when the picture was really drawn again, which
is what makes "scrolling left it alone" something a test can ask about - and
the control for it is the bug itself, put back.

**The same mistake was already sitting in the scroll bar marks.** Looking for
it after the minimap found it: `scrollBarHighlights` was a property notified by
`metricsChanged`, so every layout re-evaluated the binding, walked every mark
and every search result to build a list of maps, and had the `Repeater` throw
away and rebuild every rectangle on the bar. On a file with a few thousand
search hits that is per frame while scrolling. It is worked out once with the
rest of the layout now and the bar is told only when the answer differs, which
a spy can check.

**`NOTIFY metricsChanged` is the trap, and it is an easy one.** It reads like
"when the numbers change" and means "at the end of every layout". Two features
in a row bound expensive work to it. Anything whose value survives a scroll
wants a signal of its own.

**Both minimap bugs were mine, shipped in the same batch, and neither was
visible.** One made dragging depend on having been painted; the other made
painting happen far too often. A feature that looks right on screen can still
be wrong in what it does and when it does it, and neither question is asked by
looking at it.

**Building the picture in `paint()` made dragging depend on having been
drawn.** The picture is what says where the marked part *is*, so a click needs
it as much as a repaint does. The first version built it lazily on paint, and
the drag test failed with the image still null - the press landed, the drag
started, and the scroll arithmetic divided by a span of zero. Built on demand
instead, from both paths. **Anything used for hit-testing cannot be built only
when drawing.**

**Two collisions came from moving the code mechanically.** Substituting the
parameter names into the body produced `QColor defaultTextColor =
defaultTextColor;` and an `overrideColor` local shadowing the callback of the
same name - the second of which compiled far enough to complain that an
`optional` is not callable. A rename fixed both. Moving a member function to a
free one renames its inputs, and locals do not move out of the way.

**qbs could not be re-resolved here and the reason matters.** No Qt profile is
configured for qbs on this machine, so every product fails with "Dependency
'Qt.core' not found". What that run *does* establish is that the project files
**parse**: `coreplugin.qbs:4:1 Error while handling product 'Core'` means qbs
read the file and got to the product. A syntax error never gets that far.

**The build descriptions are in step, and the `.qml` half needs no work at
all.** Every `.cpp` and `.h` this branch added is listed in its `.qbs`
(`qtcquick` is 27 of 27). Nine files elsewhere are unlisted and all nine
predate this work by months - mostly headers, which qbs does not need.

The `.qml` files are covered by a **wildcard**: the "qml" group is
`files: ["*.qml"]` with `fileTags: []`, above a comment saying qbs has no QML
module support and CMake builds them. So a new `.qml` needs a
`qt_add_qml_module` entry and nothing in the `.qbs` at all.

**Both of the checks that established this were wrong the first time, in
opposite directions.** The first reported all seventy `.qml` files as missing -
it looked for filenames in a file that globs them. The second reported 335
`utils` sources as missing - `ls *.qml.qbs | head -1` had picked
`process_ctrlc_stub.qbs`, the alphabetically first `.qbs` in the directory,
rather than `utils.qbs`. A check that flags *everything* is exactly as
suspect as one that flags nothing, and both were caught by asking why the
answer was implausible rather than by acting on it.

**The FakeVim failures are older than this branch and one of them cannot pass
on macOS.** `test_vim_script_throwpoint` builds its expectation from
`QTemporaryDir::path()` - `/var/folders/...` - while FakeVim reports what
`QFileInfo::canonicalFilePath()` gave it, `/private/var/folders/...`. The same
`/var` symlink that the file watcher work turned on earlier. The last commit
touching the handler or its tests is three weeks before the first editor
batch, so neither failure is this work's.

**The file watcher is quiet now, everywhere.** Every suite above reports no
"Failed to watch" and no soft assert from `devicefileaccess.cpp` - it took
three separate fixes, and each one was only visible once the one before it was
in. The single soft assert still printed by several runs is
`QtVersionManager::versions()` asking `isLoaded()`, which is hjk's from 2022 in
a file this branch has never touched: something calls it before the manager has
read its settings.

`ProjectTest::testMultipleBuildConfigs` fails in a full ProjectExplorer run
often enough to look real and passes three times out of three on its own. It is
not in the table because it is not a result.

**Fifteen commit messages have body lines over 72 characters** - 35 lines in
all, none worse than 76. Twelve of them sit 515 to 532 commits back, so
rewrapping those rewrites the whole branch from there, including roughly 368
commits that predate this work. That is a bad trade for four characters, so
they are left alone; the three nearer the tip (23, 44 and 240 commits back)
are cheap to fix if the rule is worth enforcing before pushing. Only one of
the last forty commits is affected, so this is a habit that corrected itself
rather than an ongoing one.

**CMake/qbs agreement was audited across the branch**, since the project keeps
two build descriptions and requires them to match. Comparing each directory's
two files over all 905 the branch touches found one real gap: three new
debugger autotests, qbs products for two. `tests/auto/debugger/nativemixed.qbs`
now exists.

Everything else the audit flagged was the audit's fault, and each flavour is
worth knowing before running it again - a `files: ["*.qml"]` group, a
`**/*.qml` recursive glob, qmldesigner's `files: ["*", "**/*"]`, a QML file
delivered through a `.qrc`, and a source name that is commented out in both
files. A checker that does not expand globs reports every ported page as
missing from qbs, which is 78 false alarms and no findings.

**The branch's own sources were recompiled and now build without a
warning.** Forcing the 274 files it touched to rebuild produced 54 warning
lines, nearly all of them one thing: `AspectPresentation::Choice` is built
from a braced list that stops before `icon`, and twenty call sites said so by
omission. `icon` has a default member initializer now, the way `enabled`
already did, which is what "most choices have no icon" means in code. The
others were leftovers of moving code - `skipShebang` and `trimVendor` each had
a live copy and a dead one - and one test that dropped the result of
`qWaitForWindowExposed`, so it compared positions in a window that need never
have appeared.

**Comparing the two renderers' tests found no feature gap, which is worth
recording so nobody repeats it.** Twelve aspect setters appear in
`tst_aspectrenderer` and not in `quickui_test`, and every one of them has an
innocent explanation:

- plumbing for the widget test - `setAspectRenderer`, `setVariantValue`,
  `setVolatileVariantValue`, `setUndoStack`, `setChecked`
- QWidget properties with no meaning in Qt Quick - `setSizeAdjustPolicy`,
  `setMinimumContentsLength`, `setControlObjectName`
- carried, but as a delegate kind decided in C++ rather than a property the
  QML reads, which is why grepping the .qml files for them finds nothing:
  `setFlattened` becomes `FlattenedGroup`, and `LabelPlacement::
  BesideCheckBox` becomes `BoolWithOwnLabelDelegate`
- `setLabelPlacement`'s other values, which the Quick form does not need: it
  has no separate label column, so each delegate carries its own label and
  `AtCheckBox`, `Compact` and `InExtraLabel` all put the text beside the box.
  Different furniture, same words on screen.
- `LabelPlacement::ShowTip` has **no callers at all**. The widget renderer
  implements it, the enum carries it, nothing asks for it.
- `setPromptDialogTitle` and `setWordWrap` are honoured, just untested here.

The one thing worth doing was covering the filter conversion, which is a real
format change - `";;"`-separated for Qt, a list for QML - and it now is.

**Two things using the editor found that no test did.** A selection's colours
are format ranges on the row's layout, and a background set that way is
painted per glyph run - so it arrived in pieces, with a gap at every run
boundary and nothing at all over a selected line's indentation. Scanning the
rendered pixels showed the fill starting one character in. The row carries the
selected span as one rectangle now, drawn behind the text like the newline
tail. `QSGTextNode::addTextLayout()` will paint a selection itself, given
`selectionStart`, but it also imposes `selectionTextColor` on the text, which
would drop the syntax colouring the widget editor keeps - hence the rectangle.

And a `WheelHandler` accepts only an actual mouse wheel unless
`acceptedDevices` says otherwise, so a trackpad scrolled nothing. It also
reports distance in pixels rather than wheel notches, and using that is what
makes it follow the fingers.

**Looking next to the trackpad bug found a bigger one: the editor could not
scroll sideways at all.** `setScrollX()` existed and nothing in the product
called it - no scroll bar, no wheel handling, and no sideways half to
`ensureCursorVisible()`. Wrapping is off for code, so typing at the end of a
long line put the caret off screen and left it there, measured at x=2527 in a
viewport 400 wide.

Three pieces. `contentWidth` is the widest row on screen, which with wrapping
off is a whole line, **plus room for the caret that sits after the last
character** - without that the end of the longest line can be reached and the
caret on it cannot, one pixel short. The sideways ensure runs at the end of
`updatePolish()`, not when the caret moves, because where the caret is across
a line is only known once its row is laid out. And the wheel handler reads the
horizontal delta too.

**Page keys, and the scroll that follows the caret, both counted lines.** A
row and a line are the same thing only while wrapping is off, and two places
assumed it always. A page of thirteen rows moved thirteen lines - about four
screens - and `ensureCursorVisible()` measured the caret's place as its
block's line number times the line height.

A page scrolls a screen now and puts the caret back where it was on screen.
The rows are the viewport's own, worked out in `updatePolish()`, so the screen
point is resolved there too - the same waiting the sideways caret does. The
document's layout cannot help: **it never sees the viewport's wrapping**,
which is why handing the page keys `layoutOf(document)` - the obvious fix,
tried first - changes nothing.

**The second half was only visible once the first was in.** Scrolling a page
and then letting `ensureCursorVisible()` run put the view at 42 pixels instead
of 182: it counted the caret's block as a row. It asks the layout that counts
rows now, and adds which row of the block the caret is on - without that a
caret several rows into a wrapped line drags the view back a row every time.
Neither half works alone, and the test says so: putting either back fails it.

**Wrapping is where this branch's editor bugs live, and the reason is worth
stating: with it off a row is a line, so nothing distinguishes the two.** Four
places counted lines where rows were meant - the page keys, the scroll that
follows the caret, `gotoLine()`, and the arrows - and none of them showed up in
ordinary use or in a test, because the suite runs unwrapped. Turning it on is
the configuration to hammer.

Two further checks came back clean, and both are kept as guards. Scrolled to
the bottom of a wrapped file, the last row is the end of it. That one is kept as a guard rather than a fix. It
catches the coarse mistake - a content height that counts lines fails it, along
with seven others - but not the two layouts wrapping at different widths, which
lands the caret between rows and is what the Down test is for. Worth knowing
which test covers which, because the two look like the same bug from outside.

**Holding Down on a wrapped line skipped a row, and the cause was not where it
looked.** Rows of 54 characters, and the caret walked 54, 162, 269 - one row on
the first press, two on every one after. The rows were right, the layout's line
numbers were right, and the key handler ran once per press. What grew was `x`:
4, then 393, then 1173.

`x` is the column the caret keeps while moving up and down, and a `QTextCursor`
carries it. This view builds a fresh cursor from a position for every key, so
it was lost between presses and recomputed - and recomputed from a caret at the
start of a row it comes out as the *end of the row before*, which is the far
side of the viewport. The next move starts from there. It is handed back before
the move and taken after now; a caret put somewhere rather than moved there
still has none, so a click starts afresh.

Selecting had the same fault for the same reason - Shift and Down go through
the same rebuilt cursor - and the fix covered both, but only one had a test.
Now both do. The selection's anchor was already safe, because the cursor is
rebuilt from the selection's own start and end rather than from the caret, and
that is asserted too: it is the other half of what a rebuilt cursor can lose.

**The general shape is worth naming.** This view rebuilds a `QTextCursor` from
a bare position for every key, so anything the cursor was carrying is gone
unless it is kept somewhere. Two things it carries matter: the column for
vertical movement, which was being lost, and the selection anchor, which was
not. A third exists in principle - the char format for typing - and nothing
here depends on it yet.

Worth remembering how it was found: the break indent test asserted two rows
down and got three. Measuring the same move with break indent **off** gave the
same disagreement, which is what said the new feature had not caused it. The
assertion was dropped rather than committed against someone else's fault, and
the fault chased separately.

**What the Qt Quick editor still does not do, audited rather than guessed.**
Whitespace turned out to be one of a class - a display setting that reaches the
document and then needs someone to draw it - so all thirty of them were checked
against what the editor reads. Sixteen are unread, and they are not equally
interesting:

- **Wrapped-line indent and marker** - done, all five settings.
  `breakindent`, `breakindentMin` and `breakindentShift` push the continuation
  rows in; `showBreak` and `breakindentSbr` draw a marker at the start of them,
  before the indent or in front of the text. The rows are shaped narrower by
  both, and the layout that counts rows is told about both, so the two break in
  the same places. The arithmetic is `PlainTextDocumentLayout`'s, copied rather
  than invented - including that the marker takes room whether or not the rows
  are indented.
- **Scope highlight** - done. Hovering the folding column dims everything
  outside the folds that enclose the line under the pointer, leaving that
  scope on the plain page colour; the `highlightBlocks` setting makes the same
  highlight follow the caret instead of waiting to be hovered. The nesting
  walk and the colour ramp are shared with the widget editor rather than
  copied - `blockNestingAt()` and `scopeLevelColor()` in textdocumentlayout -
  with where a level starts on screen passed in by the caller, that being the
  one part of it that is not a property of the document.
- **Whole features not started** - one left: `markDiffChangeSigns`, which is
  not a feature of its own anyway - it is a display option on the inline diff,
  whose ghost rows the Quick editor does not have at all. `displayMinimap` and
  `scrollBarHighlights` are **done**, and in both cases needing something from
  Core turned out to be the wrong reason to put them off.
- **Animations** - all three done. `animateMatchingParentheses` pulses the
  matching bracket; `animateNavigationWithinFile` and
  `animateWithinFileTimeMax` scroll to a jump within the file instead of
  snapping to it, in two halves with a deliberate gap in the middle so that a
  long jump's direction stays readable.
- **Editor-layer behaviour rather than drawing** - all four accounted for.
  `minimalAnnotationContent` is read now; it turned out to be a parameter of
  `annotationAlignment` rather than a setting of its own, and the margin
  alignment is the only reader it has ever had. The other three:
  - `openLinksInNextSplit` is **done**. It swaps what Alt means rather than
    being a second way of asking for the other split - the widget takes Alt
    exclusive-or the setting, and its two Follow Symbol actions negate each
    other through it. The Quick editor passed Alt straight through and
    hard-coded the actions, so the setting did nothing at all.
  - `displayTabSettings` is **done**, and was the bigger find: it defaults to
    **true** and shows a toolbar button saying what the document indents with,
    with a menu to change it. The Quick editor had the line ending and the
    encoding beside it but not this one.
  - `forceOpenLinksInNextSplit` is **not a porting gap at all**. It has no
    settings key, no label, no entry on any page and no reader anywhere in the
    tree - it is dead in the widget editor too. Listing it as unported was
    wrong.

**"Everything left is off by default" was wrong, and the way it was wrong is
the useful part.** That claim came from reading the settings' defaults, which
is not the same as reading the defaults of the behaviour they gate.
`highlightBlocks` does default to false - and the scope highlight it is named
after is drawn anyway: `paintBlockHighlight()` is called unconditionally and
returns early only on an empty info, and that info is filled in by hovering
the folding column, which no setting guards. The setting widens what counts as
a hover; it does not switch the highlight on. So a list of settings reported
as opt-in was hiding behaviour the widget editor has by default and the Quick
editor did not have at all.

What finds this is asking what a setting *gates* rather than what it is
called, and the same question re-sorted the rest of the list: of the four
entries once filed as "features not started", one was an option on a feature
nobody has started, and two are not viewport work at all.

None of these is a bug in what is there; they are things not written yet. The
list is here so that the next person picks by value rather than by whichever
one they trip over.

### The behaviour settings, audited the same way

`BehaviorSettings` was recorded above as read for two of its seven fields.
That count was wrong, and wrong in the way a name-based audit always is:
`mouseHiding` is read through `hideMouseWhileTyping()`, so grepping for the
aspect's name misses it. **Count reads of the container, not mentions of the
name** - `globalBehaviorSettings()` had three call sites, not two.

Of the four that were unread, two mattered and both are on by default:

- `mouseNavigation` was **a setting that could not be turned off**. The QML
  restated the widget's modifier rule - Control yes, Shift no - and left out
  the half that consults the setting, so Ctrl+click followed symbols however
  the preference was set. The rule now lives in `isMouseNavigation()` and the
  QML asks rather than restates it.
- The rest of that same setting was missing outright: the widget underlines
  the link under the pointer in `C_LINK` and turns the cursor into a hand, and
  the Quick editor drew nothing. It does now, including Control pressed while
  hovering asking for the link without waiting for a move, and letting go
  putting it away. **Finding** a link needs a language with a link finder
  registered, which no test here has, so the test covers the drawing and not
  the lookup - `showLink()` is called directly with a link.
- Tooltips are now suppressed while Control is held, so one does not cover the
  link being aimed at. `constrainHoverTooltips` is read at the same time. This
  is the one piece with no test at all: nothing yet drives the tooltip timer.

`keyboardTooltips` is done: Alt pressed and let go with nothing in between
asks a hover handler about the caret, which was the only way of asking that
needed no mouse. Left unread on purpose: `smartSelectionChanging`, which is
not the text editor's at all - CppEditor reads it for expand/shrink selection.

### All ten settings containers, counted

Stopping at three was the same mistake again, so here is the whole set. Ten
containers: `Display`, `Margin`, `Behavior`, `Font`, `Tab`, `Typing`,
`Storage`, `Completion`, `Comments`, `ExtraEncoding`. The Quick side reads
`Typing`, `Storage`, `Completion` and `ExtraEncoding` at least as often as the
widget editor does, and `Comments` is read by neither - CppEditor owns it.

`TypingSettings` had the gap. **`m_smartBackspaceBehavior` defaults to
`BackspaceUnindents`** - inside a line's leading whitespace, Backspace takes a
whole level - and the Quick editor deleted one character whatever the setting
said, so the default was missing and the preference had no effect. All three
behaviours are implemented now, including `BackspaceFollowsPreviousIndents`,
which goes back to the indentation of the nearest line above indented less
than this one. Past the indentation there is nothing to unindent and all three
take one character, which is its own test case.

**A `TextViewport` is read-only until told otherwise.** The first version of
that test pressed Backspace and nothing happened at all - not one character
removed, the line untouched - because every key was landing on the read-only
guard. An existing test says so in as many words; a new one has to call
`setReadOnly(false)` or it is testing the guard. The tell is *nothing*
happening rather than the wrong thing happening.

### Two more that were not off by default

The list above said the rest was opt-in. Two of them are not, and both had
been recorded wrongly:

- **`scrollBarHighlights` defaults to `true`.** A file too long to see said
  nothing about where its errors were. It was put off as "needs
  `Core::HighlightScrollBarController`", which was the wrong reason: that
  class *is* a widget - a `QScrollBar`, a `QAbstractScrollArea` and an overlay
  - but what it holds is a list of places and colours. The viewport reports
  the same list as fractions of the document's height, and the QML bar draws
  them. Search results are on it too now: they were the thing the setting is
  named after, and leaving them out left a match below the fold saying nothing
  about where it was. A highlight kind says whether it belongs on the bar and
  in which colour - the theme keeps a separate one for that, and it is not the
  colour the match is drawn in.
- **`animateMatchingParentheses` defaults to `true`** as well, and is **done**
  now. It also turned out to gate more than the animation: the widget editor
  looks for a match when *either* it or the highlight is on, and the Quick
  editor only looked when the highlight was on - so with the highlight off and
  the animation on, it did nothing at all rather than pulsing.

**A test that types makes the document dirty, and a dirty editor will not
close.** The keyboard-tooltip test sends Alt with another key to show that a
shortcut is not a request for a tooltip. Sending `Key_X` *with its text* typed
an `x`, so the scope guard's `closeEditors()` put up a modal save prompt and
the run hung until it was killed. Two things fix it and both are worth doing:
send a key that types nothing (`Key_F5`), and close with `closeEditors(..., false)`
the way the other editor tests here do.

**Four lines that did nothing, in one branch.** A control that comes back
clean has meant "the code was already right without it" four times now:

| the line | why it did nothing |
| --- | --- |
| `enabled: root.showFoldMarkers` on a `HoverHandler` | `enabled` does not gate a pointer handler; `visible` does |
| `emit metricsChanged()` in the margin-settings handler | `polish()` emits it anyway, from `updatePolish()` |
| `if (!already.insert(key).operator->())` | `QSet::insert` hands back an iterator that is never null |
| `setScrollY(start)` before starting the animation | `QPropertyAnimation` applies its start value as it starts |

Three were found by a control, one by reading. None of them broke anything -
that is the point. **A negative control that does not bite is a question about
the code as often as about the test**, and the answer here was usually "that
line was never doing the work you thought".

**A guard that compiles and never fires.** The per-row de-duplication for the
bar was first written as `if (!already.insert(key).operator->()) return;`.
`QSet::insert` hands back an iterator, and its `operator->` is never null, so
the guard did nothing - it compiled, it read like a check, and every match got
its own mark. Found by reading it back rather than by a test, because the
fixture at the time had one match per line and could not have told the
difference. The fixture was widened first, *then* the control was run.

**Three of the four controls on the pulse did not bite at first, and every one
of them was the test's fault.** Worth listing, because they are three
different mistakes:

1. The whole test ran with the *highlight* off, and what stops a second pulse
   for the same pair is whether that bracket was marked last time - with
   nothing marked, the rule can never fire. Fixed by turning the highlight on
   for that part.
2. "Move away and come back" did not move away. Two `setCursorPosition` calls
   in a row are **one** layout, so the position in between is never processed
   and the pair stayed marked. Fixed by arriving at a second pair that had
   never been visited.
3. The "nothing was pulsed" assertion ran before the layout that would have
   pulsed. `QTRY_VERIFY(cursorPosition() == n)` waits for nothing - the caret
   moves synchronously. Fixed by waiting for the pair to be *marked*, which
   only happens in that layout, and checking the absence after it. This is the
   ordering rule that is already written down, applied to a case that did not
   look like it needed it.

**Checking a default is not the same as checking it once.** The
`highlightBlocks` correction earlier fixed one wrong "off by default" claim
and left these two standing, because it corrected the entry rather than
re-reading the column. A claim about a *set* has to be re-checked over the
whole set.

### Counting the fields instead of the gaps

`annotationAlignment` was missed because every audit here was built from a
list of *known gaps* and checked against that list. The fix is to enumerate
the struct: all 31 fields of `DisplaySettingsData`, each checked for a reader
on the Quick side. Seven come back unread, and all seven were already on the
list - the three animations, the minimap, the scroll bar highlights, the diff
signs, and the dead one. Nothing else was hiding.

Doing the same to the other containers found two more things and one mistake
of method:

- **`centerEditorContentWidthPercent`** was read nowhere, so "Editor content
  width" did nothing - **and neither did Zen mode**, whose entire effect on an
  editor is to set this. Implemented by insetting the view rather than making
  the viewport inset itself, so no coordinate inside it changes.
- **`lineEndingSetting`** ("Default line endings:", with a settings key and a
  combo box) has **no reader anywhere in the tree**. Like
  `forceOpenLinksInNextSplit` it is dead in the widget editor too - but unlike
  it, this one is shown to users. Not a porting gap, and not this branch's to
  fix; recorded so somebody can decide.
- **A check that finds nothing has to be shown to be able to find something.**
  The first pass over the other containers reported no unread fields anywhere,
  which was a lie: the pattern matched `final : public AspectContainer` and
  those classes are not `final`, so it enumerated zero fields and cleared them
  all. Printing the count per container is what caught it.

`showMargin` and `useIndenter` looked unread and are not: `visibleMarginColumn()`
reads them, the same helper trap as `hideMouseWhileTyping`. The storage
settings are applied by `TextDocument` on save, which is not the view's job.

### Where a mark's message goes

`annotationAlignment` was not on any of the lists above, which is how it went
unnoticed the longest: the audit was built from the settings named in the
gaps, and this one is named in none of them. It **defaults to the right side**,
and the Quick editor drew every message immediately after the text - the
setting was read nowhere at all. Three of its four positions are implemented
now (next to the content, at the right margin, against the right edge), and
`BetweenLines` is not: it puts the message on a line of its own, which needs
the block to be taller than the text in it.

**The existing test could not have caught this.** It asserted
`x >= lineWidth` - that the message does not overlap the line it is about -
which every one of the four alignments satisfies. A lower bound is not a
position. The new test pins the actual x against what the viewport reports and
against what is drawn, for two alignments.

**And the first control that did not bite was the fixture's fault, not the
harness's.** Removing the `qMax` that keeps a right-aligned message off the
end of a long line changed nothing, because every line in the fixture was
"line 42" and the right edge was never left of where the text ended. A
long line had to be put in the file before that control could bite. A control
that does not bite is a question about the fixture first.

**A `Repeater` inside a `Menu` cannot be reached from it.** The indent sizes
started as a `Repeater` over `model: 8`. `findChild` on the menu found
nothing, and opening the submenu first did not help either - the delegates are
not `QObject` children of the menu that shows them. Written out one by one
they are found immediately, like the plain `MenuItem`s next to them. A
submenu is also a popup of its own, so it is not under the menu that opens it;
both have to be asked for by name from the root object.

**A hover test failed once here, and calling it a flake was wrong.** The
full-suite run after the tooltip work reported
`testRestingTheMouseAsksTheHoverHandlers` failing; it passed on re-run, and
that was written up here as flakiness. It failed again the next batch, which
is what a pattern looks like. Counting settled it: **three failures in six
runs with the suppression, none in nine without it.**

The mechanism is only half known. The hover event that triggered the
suppression carried `ControlModifier` while nothing was holding Control -
printed, not guessed. One contributor was mine: the link test sent
`QTest::keyRelease(Qt::Key_Control)` with no matching press, and **QTest keeps
modifier state for the whole process**, so every later test was told Control
was down. Pressing as well as releasing took it from six failures in six to
three in six - real, and not the whole story.

So the suppression came back out. It was the one piece of that change with no
test of its own; a nicety is not worth a suite that fails half the time. What
this cost was two wrong conclusions in a row - first "the constrain branch is
firing" (measured: it was not), then "it is a flake" (counted: it was not) -
and what fixed both was measuring rather than arguing. **One green re-run is
not evidence that something is a flake.** Two arms of six runs is.

**Where the phantom Control comes from, since the answer decides whether
anything shipped is at risk.** Printing both the event's modifiers and
`QGuiApplication::keyboardModifiers()` on every hover showed the global state
stuck at `ControlModifier` across four consecutive tests, with the event
sometimes inheriting it and sometimes not. Qt takes the application-wide
modifier state from the last event it delivered, and the test that runs
immediately before the hover test ends on
`QTest::mouseClick(quick, Qt::LeftButton, Qt::ControlModifier, ...)`. Nothing
after it says Control is up, so the whole process believes it is.

**This is an artefact of synthetic events, not a production risk.** It matters
because the link decoration that did ship also reads `event->modifiers()`: if
hover events could carry a Control nobody is holding, links would underline
themselves. They cannot outside a test - real events carry the window system's
modifier state, which tracks the real keyboard. Only `QTest`-delivered events
can leave a stale one behind.

Adding a plain `QTest::mouseMove` after that Control+click to put the state
back **did not work** - still three failures in six. So the leak is understood
and the repair is not, and the suppression stays out rather than being shipped
on a hypothesis. What would let it back in: a way to clear the process's
modifier state between tests, and a test for the suppression itself.


**Visualised whitespace is drawn now, and was not before** - a green test said
otherwise, because it checked that `ShowTabsAndSpaces` reaches the document's
text option, which it does, and nothing then read it. `ShowTabsAndSpaces` is
honoured in `QTextLine::draw_internal()`, and **qtdeclarative's scene graph
never mentions it**, so an editor drawing through `QSGTextNode` shows nothing
whatever the option says. The rendered row had the same ink either way.

The rows carry the positions of their spaces and tabs now, and CodeViewport
draws them the way it already draws indent guides, in the same
`C_VISUAL_WHITESPACE`.

**Two false trails on the way, both worth knowing.** Setting the flag on the
rows' own text option changes nothing - the flag is not the problem. And
`findChildren()` does not find Repeater delegates: they are in the visual tree,
not the QObject one, so `childItems()` is what sees them. That one cost the
most: it said "zero items created" while the items were there and correct, and
sent me looking for a fault in QML that had none.

**The caret's way back into view sideways was invented here too.** It nudged
just inside the edge plus a quarter of the view, or four lines, whichever was
smaller - numbers with nothing behind them. The widget editor centres the
caret, in `PlainTextEditPrivate::ensureCursorVisible()`, and now so does this.

The test for it had to be moved to where the rule shows. At the end of the
longest line there is nothing to the right to scroll to, so the clamp puts the
caret against the edge whichever rule is in force - and asserting "centred"
there fails against correct code. Mid-line it is centred or 143 pixels off in
a view 400 wide.

**A wheel notch was three lines because three is the usual default.** It is a
setting - `QStyleHints::wheelScrollLines`, reachable from QML as
`Application.styleHints` - and the widget editor honours it by way of a scroll
bar whose step is one line. Anyone who had changed it got two editors that
scrolled at different rates. Both axes follow it now, twenty pixels a
notch-line sideways to match the widget bar's step.

Worth copying: the test **sets** the number instead of reading it. Reading it
would have got three on this machine, which is what the code was hard-coded to,
so the old behaviour would have passed.

**How wide the content is can only be asked of the rows on screen**, since
those are the only ones laid out - and taking that as the answer made the
horizontal scroll lose its place. In a file with lines of different lengths it
swings hard: 50 pixels among short lines, 2179 among long ones. `setScrollX()`
clamps against it, so scrolling right on a long line and then up past a short
one put the reader back at the left with nothing to say why. It accumulates
now, like the widget editor's `maximumWidth`, and resets with the document or
the wrapping.

**The defect that wasted the most time here was not in the editor.** Three
times a test waited for a condition that was already true - a row count past a
threshold the document already met, a scroll offset inside a bound that held
before anything moved, a caret position that had not been recomputed yet. Each
one made a green test, and one of them supported two confident and wrong
explanations of how a cache works.

The suite was audited for it rather than assumed clean. Making `setWrapping()`
do nothing fails all eleven tests that turn it on, so every wrapping wait is
live. Two of them were still one added line from becoming vacuous - they waited
for more than three rows on documents with exactly three - and now compare
against the count taken before wrapping.

**Wait for the change, not for a number.** A threshold cannot tell "it has
happened" from "it was already so".

**One loose end, and a lesson about the test that chased it.**
`TextEditorLayout` caches where each block starts in rows and warns in a FIXME
that the cache is not reset when the width changes. Going to a line after a
resize works, and there is a test for it.

The test was worthless when first written, and said so only under
instrumentation: it waited for the row count to pass a threshold the document
was **already over**, so the wait returned immediately, `updatePolish()` never
ran, and the rows were the same 55 characters at 400 wide and at 200. Every
conclusion drawn from it was about a rewrap that had not happened. It waits for
the rows to get narrower now, which cannot be true beforehand.

With the test finally exercising a rewrap, the cache-drop candidate is properly
rejected: taking away the `resize()` in `setBlockLineCount()` still fails
nothing. So what keeps the row numbers right across a width change remains
unknown - but the question is now open for an honest reason rather than because
the experiment was empty.

And clicking a later row of a wrapped line lands on it - the row under the
pointer is not the line's first, so the character under it is in the middle of
the block. Breaking the row offset out of `positionAt()` fails eleven tests,
which sounds like ample cover until you look: that control breaks every
position on screen, and not one of the eleven clicks a continuation row. A
coarse control failing many tests is not the same as the case being covered.

**A fourth, and the most used key there is: Down stepped over a whole wrapped
line.** The move keys take a layout so they can move by what is on screen, and
they were given the document's - `layoutOf()` returns that, while the viewport
builds a `ViewportLayout` of its own for the rows and keeps it to itself.

Fixing that exposed the two wrapping in different places: rows of 54 drawn,
Down landing on 52. Both are handed the same width through different doors.
**`setTextWidth()` is a document width** - the layout subtracts the document
margin from both sides, and the width of a line separator glyph where the text
option asks for one - while the rows are shaped with `setLineWidth()`, which is
the width of the line. Passing the same number to both is what put them two
characters apart. The compensation mirrors the layout's own arithmetic rather
than guessing at it: the margin alone closes one character of the two, which is
how it was found.

**A third site had the same bug, found by grepping for the shape of it.**
`gotoLine()` - what a search result and a compiler message use - scrolled to
the line's number times the line height, so going to line 60 of a wrapped file
left it off screen entirely, the caret not laid out at all. Anywhere a line
number is used as a vertical coordinate is worth checking:
`firstLineNumber()`, `blockNumber() *`, and either side of `m_lineHeight`. All
of them are rows now, and the one question they share is a function, so the
next caller cannot get it wrong on its own.

One more thing the test got wrong on the way. It asserted the scroll landed on
exactly a page, and passed - on a value that was true for an instant before
`ensureCursorVisible()` settled it a row lower. Asserting a number that is
about to change is not asserting anything; it wants the settled state, within
a row.

There is a horizontal scroll bar now too, mirroring the vertical one. Wrapping
needed a case of its own: no row is wider than the viewport then, but the
content width carries room for the caret past the last character, which is a
few pixels more than the viewport - enough for a bar with a 99% handle under
every wrapped document. Clamped while wrapping, and the caret allowance
applies only where lines actually run off the edge.

**The first control for that was invalid and said so by not biting.** Deleting
the clamp left an empty `if (m_wrapping)`, which skipped the caret allowance
as well, so the bug did not come back and the test passed. The control had to
restore what the code did before the wrapping case existed - the allowance
applied unconditionally - and then it failed with the original numbers,
`contentWidth 396.594` against a width of 392.

Adding the horizontal clamp exposed that neither offset was re-clamped when
the content changed size - only the line height was handled, and only
vertically. Widening a viewport scrolled to the right left 800 pixels of blank
space, and with wrapping on the bottom moves up while `scrollY` does not. Both
are re-clamped at the end of `updatePolish()` now.

**That test passed for the wrong reason twice before it worked.** Waiting on
the bound - "the offset is within what there is to scroll through" - waits for
nothing, because the bound holds the instant the width changes and before the
content size is recomputed. The wait has to be on the offset actually coming
back. And `lineCount()` counts rows rather than lines, so comparing it against
the content height is an identity, not a check that wrapping has been applied.

Two things the tests got wrong first, both worth knowing. The caret test drove
`setCursorPosition()`, which does not scroll - only `setTextCursor()` does,
which is what a key press goes through, and the same is true vertically. And
the sideways wheel test used a document of short lines, where there is nothing
to scroll to and the clamp correctly does nothing: it passed for the wrong
reason until the fixture got long lines.

The fill is measured per row with `cursorToX`, so the cases where a row is not
a whole line were checked after the fact: a wrapped line and a sideways
scroll. Both were right already. They have a test of their own because the
first one cannot see them - it selects short unwrapped lines, where a row
starts where its block does, so measuring from the wrong one of the two gives
the same answer.

**qmllint over every plugin is worth running, and now worth reading.** All 67
`*_qmllint` targets used to give 665 warnings, 654 of them one thing: a page
reaching `aspects.Foo`, a property of the `AspectPage` around it, without
saying where it comes from. Calling that noise was the wrong call - each one is
harmless, but six hundred of them are why nobody looks at the output, and a
real warning would have sat there unread. They are `root.aspects.Foo` now and
the count is **10**.

The ten are all understood, and none of them is a defect:

- `IntegerDelegate` declared `scale`, which `QQuickItem` already has, for the
  factor between the value an aspect keeps and the one it shows. Renamed, and
  now covered - the Qt Quick side had no test for the factor at all, though
  the widget renderer's has one. **This is the one the six hundred were
  hiding**, which is the argument for having cleared them.
- Seven `[missing-property]` are delegates reaching for members of a derived
  aspect through a property typed `Utils::BaseAspect`, or through
  `QQuickItem`. They work, because the object really has them; qmllint just
  cannot see it from the declared type.
- Two `[equality-type-coercion]` on `source != ""` are **not** to be changed
  to `!==`. `source` is a url, and `!==` against a string is always true, so
  the "fix" would make those items permanently visible. Compare
  `String(source)` if they are ever worth silencing.

The targets do not fail a build, so nothing notices these unless they are run
deliberately. Ten is small enough to read, so a future run that shows eleven
means something.

Qualifying 83 files at once is only safe because two censuses check them, and
between them they cover all 82 that changed:

- 65 settings pages by `testAspectDrivenPagesRenderWithQuick()`, which builds
  every registered `IOptionsPage` and fails on any QML warning.
- 17 project panels by `testPanelsThatSayWhatTheyShowRenderWithQuick()`, which
  is the reason `ProjectPanelFactory::aspects()` exists - a panel says what it
  shows so a test can ask without opening it. It reports "17 panel(s) say what
  they show; 0 still build a widget", checks each form reaches
  `QQuickWidget::Ready`, and checks every delegate found its aspect.

So a mis-qualified name is a red test rather than a page that silently draws
nothing. Mistyping one to prove the point gives "ACP Servers:
AspectListDelegate has no aspect".

**Setting the Quick Controls style is a startup job, not a lazy one.** It was
being set inside the engine that `QtcQuick::engine()` creates on first use, so
it landed wherever the first QtcQuick widget was built - and Controls keep the
first style anything loaded, so if another engine had drawn a control by then
the call did nothing. The test written for this at first tried to arrange the
order itself and was flaky for the same reason. It is a
`Q_COREAPP_STARTUP_FUNCTION` now, which is before every engine in the process.

**Reading the QWARN lines of a green suite found one more.** TextEditor
passes 281/281 and printed, in the middle of it, that
`QQuickStyle::setStyle()` had been called too late to take. The viewport tests
build their own `QQuickView`s and `CodeViewport.qml` imports Qt Quick
Controls, so those controls were drawn by the default style, not Creator's -
the tests were not looking at what the product draws. QQuickStyle is process
wide and latches on the first Controls import, so this also disabled the
`setStyle()` behind the shared engine for everything that ran afterwards.
Asking for the engine in `initTestCase()` fixes both, and the test now asserts
which style it got.

The same tests are worth knowing about for a second reason: they wait with
`QTRY_VERIFY(viewport->visibleLineCount() > n)` rather than
`qWaitForWindowExposed`, which is the better of the two - it is bound to the
state the viewport must reach, not to the window server. Eight fixtures with
no exposure wait looked like the bug above and are not it.

Warnings do not show up in an incremental build once the object file exists,
so this is worth repeating rather than assuming: touch the branch's files,
build, and read the log. The `-Winconsistent-missing-override` noise from
`baremetal/idebugserverprovider.h` is not this branch's and is still there.

A full `qbs resolve` cannot complete here: the vendored `src/shared/qbs`
wants `Qt.core5compat` and no installed Qt has it. That is pre-existing and
unrelated to the branch. The new file was checked by breaking it on purpose
and confirming qbs reported a syntax error against it, so its clean parse
means something.

The section below is the running commit log and the phase notes; the entries
after "Next steps" are the batch-by-batch record.

The second batch adds: the validator-type
hoist, the QFontComboBox removal, AspectPresentation with 23 aspects
reporting a control, the terminalcommand and StyleHelper and Theme help-menu
splits, the Prompts seam growing a file chooser, a default button and
dialogsInteractive(), the settings-accessor conversion, the theme-statics
audit (~160 sites), the Icon cache theme keying, the GUI-write undo fix,
TreeModel role names, the WidgetTextControl host interface, the gutter
display list, nine QML design-system components and five aspect delegates.

```
 1. Utils: Drop the QtPrintSupport dependency
 2. Utils: Register style tokens with the meta-object system
 3. QtcQuick: Add a Qt Quick foundation library
 4. QtcQuick: Add the QtCreatorStyle Qt Quick Controls style
 5. QtcQuick: Add an image provider for Qt Creator icons
 6. Tests: Add a manual test for the Qt Quick design system
 7. Utils: Give aspects a QML-friendly property interface
 8. QtcQuick: Render aspect containers as Qt Quick forms
 9. Core: Allow rendering settings pages with Qt Quick
10. QtcQuick: Single-source the settings form metrics
11. ExtensionSystem: Stop depending on QtWidgets
12. Utils: Remove unused QtWidgets includes
13. Utils: Move the file dialogs out of fileutils
14. Utils: Take QStyle out of stylehelper.h
15. Utils: Ask the user through a seam instead of QMessageBox
16. Utils: Write output into a QTextDocument, not a QPlainTextEdit
```

What exists now:

- `src/libs/qtcquick` — `QtcQuick` (QML module `QtCreator.Ui`) with a shared
  engine, a `QuickWidget` host, the `DesignSystem` theme bridge, an icon image
  provider, and the aspect form; plus `QtcQuickStyle` (`QtCreatorStyle`), 13 Qt
  Quick Controls styled from the tokens.
- `src/plugins/quickui` — installs the aspect-form factory when
  `QTC_QUICK_SETTINGS` is set. Its in-process test asserts that every
  aspect-driven options page takes the Qt Quick path.
- `tests/auto/qtcquick/designsystem` — headless load and drift test.
- `tests/manual/quick/gallery` — the runnable gallery, with a theme selector.

Verified: full build clean; `qmllint` clean on both QML modules with zero
suppressions; the drift test goes red without `RESOURCE_PREFIX`; the aspect
round-trip test goes red without its type guard; real preferences pages
(Interface, System, MCP Servers, Custom Language Models) render through Qt Quick;
two-way binding holds with Apply/Cancel intact.

Test state: 2984 tests. Every failure was confirmed pre-existing by
reproducing it byte-for-byte at the base commit, and four groups are now
fixed on this branch rather than merely attributed: `tst_baseenginedebugclient`
(dangling reference in the test), the 7 `AuxiliaryPropertyStorageView` aborts
(stack-use-after-scope binding a temporary into a lazy sqlite range, visible
only under ASan), and `McuModuleProjectItem` (test-isolation bug). Still
failing, still pre-existing: 3 `Model_Imports` (metaInfo expectation
mismatches, root cause not established) and `tst_debugger_dumpers`
(long-running, environment-dependent).

## Removing Layouting

`Layouting` is not given a second backend. It is deleted, and the layouts it
describes are written as `.qml` files.

This reverses an earlier working assumption, so the reasoning matters. A runtime
`Layouting::Backend` was attractive because `builderutils.h` holds no Qt types
and `Layout` already buffers `pendingItems` before materialising anything, so a
backend swap looked nearly free at the 1730 call sites. What it actually buys is
a C++ DSL that emits QML objects: every layout stays imperative, invisible to
`qmllint`, unreachable from `qmlls`, and outside `qmlcachegen`. The 1730 call
sites would survive the migration as permanent C++ UI code in a UI that is
supposed to have none, and the DSL's own escape hatches
(37 `using Implementation = QLabel;`, 64 `bindTo(T**)`, 69 `emerge()`) would each
need a runtime-null story that is discoverable only by testing. A `.qml` file has
none of those problems and is the artefact a designer can read.

So `Layouting` is transitional scaffolding for the widget UI, kept alive only
until each surface it describes has a `.qml` replacement, then removed with the
widget UI itself.

Two consequences that change the order of work:

**`AspectForm.qml` and its per-kind delegates are the end state, not a stopgap.**
They were written to prove the aspect model; they are now the shape every
aspect-driven surface targets. The `AspectPresentation` descriptor is what feeds
them, and the ~25 `addToLayoutImpl()` overrides are what it replaces.

**`addToLayoutImpl` is what pins `aspects.{h,cpp}` to the widget side, and
nothing more than that.** The header is nearly clean: it only forward-declares
`Layouting::Layout`, and its leftover `fancylineedit.h` include is unused. What
pins it is its own implementation. `aspects.cpp` includes two dozen QtWidgets
headers - `QCheckBox`, `QLineEdit`, `QSpinBox`, `QTreeWidget`, `QPainter`, ... -
because the 16 `addToLayoutImpl()` overrides *construct the controls*. A class's
header and implementation must live in the same library, so no amount of include
hygiene on `aspects.h` moves it. That is what `AspectPresentation` is for:
`BaseAspect` describes the control it wants, and a renderer on each side builds
it.

**But `aspects.h` is not the gate for the rest of `Utils`.** That was asserted
here twice before it was measured, and both times it was wrong. The measured
chain, with `<QFontComboBox>` gone from `aspects.h`, is:

    filepath.h  <- filepath.cpp -> devicefileaccess.h
                <- devicefileaccess.cpp -> qtcprocess.h
                <- qtcprocess.cpp -> terminalhooks.h
                <- terminalhooks.cpp -> externalterminalprocessimpl.h
                <- externalterminalprocessimpl.cpp -> terminalcommand.h
                <- terminalcommand.cpp -> QDialog

Every `<-` is the header/implementation pairing rule, not an include. The
terminus is `terminalcommand.cpp`, which builds a modal "Select Terminal
Emulator" dialog - `QDialog`, `QDialogButtonBox`, `QPushButton`, `pathchooser.h`,
`elidinglabel.h` - in the same file as the `TerminalCommand` value type and its
settings storage. Splitting that file is what unpins `filepath.h`,
`environment.h`, `qtcprocess.h` and `devicefileaccess.h` all at once, and it is
a smaller change than the aspects inversion.

So there are two independent pieces of work, not one gate:

1. Split `terminalcommand.cpp` - value type and settings stay, dialog leaves.
   Unpins the `filepath`/`qtcprocess`/`environment` cluster.
2. Invert `addToLayoutImpl` into `AspectPresentation`. Unpins `aspects.{h,cpp}`
   and `terminalcommand.h`.

Of the 37 headers in `Utils` that include a QtWidgets header directly, most are
genuinely widget classes - `appmainwindow.h`, `crumblepath.h`, `elidinglabel.h`,
`fancymainwindow.h`, `wizard.h`, `widgets.h`. Those belong on the widget side and
need no work; they classify correctly when the split lands. The ones worth
attention are the seams that already have a neutral counterpart:
`checkablemessagebox.h` and the dialog headers (there is a `Utils::Prompts`
seam), and `fsengine/fileiconprovider.h` (there is a `DeviceFileHooks::fileIcon`
seam).

That inversion is now on the critical path for both goals at once: it is the
gate for a QtWidgets-free `Utils`, and it is the first real piece of the
Layouting removal.

## Utils split progress

`Utils` publishing `Qt::Widgets` is the gate for every other library: a library
can reference zero widget symbols and still link QtWidgets through Utils.
Measured over the real include graph (`.h`/`.cpp`/`.mm`, excluding 3rdparty):
a file is widget-side if it reaches a QtWidgets or QtPrintSupport header
transitively, **or shares a class with a file that does** - the pairing rule is
part of the metric, because a class's header and implementation must land in
the same library. (The earlier per-file rows below did not apply the pairing
rule and overstated progress; see "Things learned the hard way".)

| | widget-side | clean |
|---|---|---|
| Start (per-file, no pairing) | 137 files / 68153 lines | 229 / 35973 |
| With pairing, before this series | 237 / 82786 | 140 / 22320 |
| After terminalcommand split | 141 / 54338 | 238 / 50796 |
| After the renderer work | 126 / 47877 | 258 / 57807 |
| After the aspect API change | 129 / 48458 | 260 / 57851 |
| Now | 131 / 47646 | 266 / 58825 |

(These last rows were measured with a re-written script, so read the split
rather than the delta across that boundary; the file count also moves because
this work added files on both sides.)

**The metric counts includes, not links.** A file can be include-clean and
still reference a symbol defined in widget code, in which case it cannot change
library. Check with `nm -u <object> | c++filt` before believing a file is ready
to move - see the `layoutbuilder` note in step 0 below for the case where this
matters.

**Where the closure stands.** Exempting each root-tainted file in turn and
re-running the closure says how many files it blocks. The largest is
`layoutbuilder` at 7; nothing else is above 4, and the remaining widget-side
files are genuinely widget classes - dialogs, labels, views, tool tips,
wizards. So the split is close to its floor apart from `layoutbuilder` and what
depends on it: `aspects.{h,cpp}`, `checkableaspect.h` and `portlist.{h,cpp}`
(which declares a `PortListAspect`).

The clean side is the larger by files and lines under the honest metric.
Clean now: `filepath`, `qtcprocess`, `environment`, `devicefileaccess`,
`terminalhooks`, `treemodel`, `commandline`, `macroexpander`, `fileutils`,
`theme/theme`, `stylehelper`, `icon`, `utilsicons`, `outputformatter`,
`settingsaccessor`, `prompts`. Still widget-side and worked on:
`aspects.cpp` - and `aspects.h` with it under the pairing rule, though the
header itself now reaches no QtWidgets header - plus the genuinely-widget files
that belong there.

## Removing Layouting

The footprint, measured: **388 files include `layoutbuilder.h`**, 332 have
`using namespace Layouting`, and there are roughly 1800 layout expressions
(649 `Column`, 441 `Row`, 392 `Group`, 183 `Form`, 49 `Grid`, the rest in
single digits). There is almost no dead weight to trim - only `SpanAll`,
`rowStretch` and `widgetAttribute` are unused, about 60 lines, and `SpanAll`
has a manual-demo user. So the size is all in the call sites, and it comes off
in phases.

Layouting does two unrelated jobs, and only the first one blocks anything:

1. **Describing an aspect container** - the 92 `setLayouter` closures that
   return `Column { aspectA, Group { title(...), Form { ... } } }`. These are
   what QML replaces.
2. **Hand-built widget UIs** - dialogs, panels and tool windows using
   `Column`/`Row` as nicer QVBoxLayout syntax over real widgets. These go when
   the Quick port reaches those windows, not before.

Of the 92 closures, **65 are nothing but an arrangement of aspects** and 27
also build raw widgets or poke objects (measured by scanning each closure body
for `new`, `QWidget`, `QLabel`, `QPushButton` or `->`).

### Phase 1 - done

`AspectContainer` no longer stores a `std::function<Layouting::Layout()>`;
it holds opaque backend data and `Utils::AspectWidgets::setLayouter()` puts the
layouter there. `aspects.{h,cpp}` reference no Layouting or QtWidgets symbol
(`nm -u`), so `Layouting` is no longer load-bearing in the aspect core and the
library split is unblocked.

### Phase 2 - done

A page can now express grouping without a closure: a nested `AspectContainer`
whose label is the group title, rendered by `GroupDelegate.qml` through the
`childModel` role. Recursive. This is what the 65 need in order to lose their
closures.

### Phase 3 - done, without waiting for full parity

The factory is installed unconditionally, so the Quick path is the default.
Full delegate parity turned out not to be a prerequisite: `createAspectForm()`
**declines a page it is not ready for**, and `IOptionsPage` keeps that page's
widget layout. So the flip is a strict improvement - no page loses a control -
and the declined pages are the backlog rather than a regression.

#### What "ready" means: the page has its own QML

The first rule tried was "accept a page whose every aspect the generic form can
show" (`AspectContainerModel::isFullyRenderable()`). It is not sufficient, and
the Qt Creator MCP Server page is the counter-example: its layouter builds a
`QTableView` of registered tools, and the per-tool `BoolAspect`s carry no label
of their own because the names live in the table's model. Every aspect on that
page is one the generic form knows, so the page was accepted - and rendered as a
column of nameless check boxes with the table gone.

(That page has its table back: `ToolEnablerAspect` hands out a model and its
control is `Table`. The names live in the model again, and the aspects keep
their labels for a renderer that lists them on their own.)

The general point: **the layouter is what decides what a page shows.** It picks
which aspects appear, arranges them, supplies labels of its own, and may build
widgets that belong to no aspect. The generic form knows none of that - it shows
the container's aspects, in order, and nothing else. And the difference cannot
be seen from the aspects, so no predicate over them can tell an unported page
that would render correctly from one that would quietly lose half of itself.

So the gate is the one thing that does carry the intent: `createAspectForm()`
renders a page **iff it names its own QML** via `AspectContainer::setQmlSource()`.
`createGenericAspectForm()` still builds the generic form for anything, and is
what the delegate tests use and what `QTC_QUICK_SETTINGS` turns on, but it is
never reached by a real page on its own.

This makes progress purely additive: a page moves to Quick when someone writes
its QML and looks at it. It also means `isFullyRenderable()` is no longer a
gate, only a measure; the test prints how many of the remaining pages contain
nothing but aspects the generic form already knows, which are the cheap ports.

#### Two traps when porting a page

**Put `setQmlSource()` on the page's container, not on an item's.** The MCP
servers page has both: `McpManagerSettings` is the page, and `McpServerAspect`
is one row of its `AspectList`. Setting it on the item left the page on its
layouter, so the QML was never loaded - and worse, the item then had no
layouter, which is what the widget editor's details pane calls. Clicking Add
aborted the process on `std::bad_function_call`. `AspectWidgets::layouter()`
now falls back to laying the container's aspects out in order rather than
returning an empty function, so the two can never combine into a crash again.

**Check what the layouter did besides listing aspects.** Three things hid in
the MCP ones:

- An `AspectList` can carry extra buttons (`addExtraButton()`); the MCP page
  uses one to fill a server in from a registry. The Quick delegate knew nothing
  about them, so the port would have dropped it silently - the same failure the
  gate above exists to prevent, one level further in. `AspectItemListModel` now
  exposes them and `AspectListDelegate` draws them after Add and Remove.
- The item's `Form` listed seven of its eight aspects, leaving out the `id` - a
  UUID that is storage only. The generic form has no such list, so it drew the
  UUID in a text field. This is the storage-only problem noted above, and the
  answer is `setVisible(false)` on the aspect: the delegates bind their
  visibility to it, so it is already the marking that was wanted. No new API.
- The closure also *wired up behaviour*: which fields apply depends on the
  connection type, and the closure connected `volatileValueChanged` to set
  their visibility. Behaviour has no business being in a layouter - it is lost
  the moment anything else draws the container - so it moved to the
  constructor, where it works for either renderer.

So when reading a closure, separate what it lists from what it *does*. Only the
first becomes QML. Every closure replaced so far has been re-read for the
second: `git log -S setQmlSource -p` and grep the deleted lines for
`setVisible|setEnabled|connect(` - the MCP one is the only one that carried
behaviour, and `groupChecker` (CTest, Beautifier, Subversion) is pure layout,
answered by `AspectGroupBox.checkAspect`.

**Text the page only shows still has to reach the renderer.** A `TextDisplay`
kept its message where only a cast to `TextDisplay` could read it, which the
widget renderer does and no one else can - so the Quick delegate drew an empty
label and the Coco page's error message was invisible. Anything a renderer has
to show goes through `BaseAspect::displayText()`, the property that already
exists for it; `StringAspect` reports its filtered value there too, which is
what a `LabelDisplay` draws. The delegate also colours the text by
`infoType`, so an error looks like one.

**Verified against the whole suite,** not just the QuickUi test: `ctest -j8`
gives 2985 tests with four failures - `tst_debugger_dumpers` and three
`Model_Imports` cases - which are the same four that failed before any of this
work started. `tst_utils_aspects` passes. Worth repeating after a batch that
touches `aspects.h`, since almost everything includes it.

**A page can be several settings objects side by side.** Behavior is five -
tab, typing, storage, encoding and behavior settings, each a container
registered on the page - and `aspects` names only the page container's own
aspects. `AspectModels.named(container)` hands a page the same by-name access
to a nested one, so it lays the sub-aspects out itself rather than settling for
the generic form of each. One `NamedAspects` per container, cached on it, so a
page and the generic form agree.

Two things needed naming for that: the five containers, which have no settings
key, and `TabSettings`' aspects, which persist through `toMap()`/`fromMap()`
rather than keys and so derive no name at all. The sub-containers keep their
closures - other pages embed them - and only the page's went.

**A closure runs when the page opens; a constructor runs at startup.** Moving
behaviour out of a layouter changes *when* it runs, and that is not always safe.
The Help page's "Use Current Page" was enabled from
`modeHelpWidget()->currentViewer()`, which the closure could ask because the
help mode existed by then; from the settings constructor the same call
dereferenced a null plugin private and took the process down. The accessor
returns nullptr now, and the plugin tells the settings to follow the widget once
it has created it. So: when moving something out of a closure, ask what it
depends on and whether that exists yet - and note that the QuickUi test's *exit
code* is what caught this, not its totals, which were never printed.

**FakeVim also builds standalone.** `tests/auto/fakevim` and
`tests/manual/fakevim` compile `fakevimactions.cpp` with `FAKEVIM_STANDALONE`,
where there is no `Utils::AspectContainer` at all. Everything added for the page
- the three preset aspects and the `setQmlSource()` - has to sit inside the
existing `#ifndef FAKEVIM_STANDALONE`, and `tst_fakevim` is what proves it does.

**A value the aspect does not keep.** `Core::SecretAspect` first said
`PasswordLineEdit`, which reads like an ordinary password field - but a secret
is not held by the aspect at all: it arrives from the keychain through
`requestValue()`, later. A generic field bound to it shows nothing and trips
`BaseAspect`'s check on the first edit, which is `qFatal` under
`QTC_FATAL_ASSERTS`.

It has a control of its own now, `AspectControls::Secret`, and the three pieces
a renderer needs are on `BaseAspect` where any renderer can reach them:

- `requestDisplayText()`, a virtual invokable meaning "go and get what you
  show". Generic, not secret-specific; the default does nothing, because most
  aspects have their value to hand.
- `displayText()` for the answer, said with `displayTextChanged()`.
- `setVolatileVariantValue()` for the write. Note *volatile*: that is what the
  `value` property's WRITE is, and overriding `setVariantValue()` instead looks
  right and does nothing at all.

`SecretDelegate` asks on completion and keeps the field read-only until the
answer arrives - typing before then would overwrite what is stored with
nothing - and waits on the signal rather than on a non-empty string, because an
empty secret is a legitimate answer. `FontAspect` still carries the original
version of this trap.

#### The three that are left, and what each one needs

Every one of these was read, not guessed at - the last three rounds each found
that the widget collapsed to controls that already existed. These do not:

| page | the widget, and why it does not collapse |
|---|---|
| Code Style (x3) | `createValueEditor()` returns a per-language editor built by a factory, and the page puts a syntax-highlighted preview beside it |

One shape is left: a hosted editor with a preview (Code Style, Snippets,
Font && Colors). Everything else is ported, so the remaining work is the Qt
Quick text editor and nothing else.

Two of these were taken further than reading, and each stopped for the same
reason - the cost is not in the QML:

**QML/JS Editing** cannot be drawn by anything but its own tree as it stands,
because the tree holds the state rather than showing it. Making the aspect own a
volatile copy and the tree show it is the right change with or without Qt Quick
- an aspect that silently applies nothing when its page was never opened is
holding on by a thread - but it is a rewrite of the aspect's core, not a port.

**Testing** is key-compatible with per-item aspects, which is how the MCP tool
table was solved: `FrameworksAspect` stores each framework under
`Id::toKey()` and its grouping under `Id::toKey() + groupSuffix`, in one settings
group, so a container of two `BoolAspect`s per framework would read and write
exactly the same keys. But it means moving a plugin's persistence, `apply()` and
dirty tracking out of a hand-written aspect, and the reward is a two-column
table becoming a flat list. Risk without a win.

#### Multi-column tables: the model says what each cell offers

CPU Usage was the first page whose value is a table, and the thing worth keeping
from it is where the knowledge went. Its editors are *context-dependent*: the
counters a row offers depend on the row's event type, a cache event has an
operation and a result where a hardware event has neither, and a raw or
breakpoint event is typed rather than picked. All of that lived in a
`QStyledItemDelegate`, which is to say in one view.

So the contract is that the model answers it, in three roles (`AspectTable` in
`aspectpresentation.h`): `ChoicesRole` gives the cell's choices as
display/id pairs, empty where there is nothing to pick; `ValidatorRole` gives a
pattern the text has to match; `EditableRole` repeats what `flags()` says,
because a Qt Quick view cannot read `flags()`. `withRoleNames()` puts the three
into a model's `roleNames()` so QML sees them as `model.choices`,
`model.validator` and `model.editable`.

`BaseAspect::tableModel()` hands the model out, and the aspect owns it, so the
`QTableView` and the Quick `TableView` show the same rows. `TableDelegate.qml`
builds a combo box where the cell offers a choice and a field otherwise; the
`QStyledItemDelegate` now reads the same three roles and dropped from 110 lines
to 25. The display forms are parsed back by the model too - `r0f3` and
`0x0000000000401000` were being un-hexed in the widget delegate, which is why
only that view could edit them.

The MCP Server page was the second user and needed three more cell shapes, so
`TableDelegate` now builds one of four things per cell: a check box where the
model says `CheckableRole`, a combo box where there are choices, a field where
the cell is writable, and a plain label where it is not. `AspectPresentation`
gained `filterPlaceholderText`; naming it puts a filter field above the table,
and `QtcQuick::TableFilterModel` - a `QSortFilterProxyModel` matching on every
column - is always in the chain, so there is one index space whether or not the
field is shown. That put the description column and the filter back, both of
which the port to a flat list of check boxes had dropped.

Things to copy when the next table aspect arrives:

- **A cell that offers nothing must say whether it is writable.** No choices and
  no pattern describes both a free-text cell and a closed one. `EditableRole`
  answered through `AspectTable::isWritable()` is what tells them apart - a
  field or a choice is editable, a check box is checkable, and either means the
  user may change it. Moving that into `flags()` also stopped the widget from
  showing disabled combo boxes.
- **A field must not write when its text has not changed.** A cell editor
  outlives its row by exactly one focus event, and the write would land on
  whichever row took its place. The test counts writes rather than comparing
  values, because writing the same value back is invisible in the result - that
  negative control did not bite until it counted.
- **`reuseItems` is off.** A reused cell keeps whatever its `Loader` built, and
  a `text` binding the user has typed over is not restored - so the guard above
  would compare stale text against a new row and write it there. These tables
  are tens of rows; the saving is not worth that.

#### Testing General: two check boxes per row

`FrameworksAspect` was the one with "no `presentation()` at all", and the
check-box cells the MCP page needed turned it into an ordinary table: a row per
registered framework and test tool, a check box that turns it on, and for a
framework a second one that groups its tests. A test tool has no tests of its
own, so its second cell is not a check box at all - `flags()` says so, and both
renderers then leave it empty rather than showing one that does nothing.

The state was the harder half. `isDirty()`, `cancel()` and `apply()` all read
`extractData(m_frameworkTreeWidget->model())`, so all three only worked while
the page was open. The aspect keeps two copies now - what was applied and what
is ticked - and the model writes into the second. `isDirty()` compares them,
`cancel()` copies back, and neither needs a widget to exist.

Two things the layouter was carrying:

- **Labels.** `scanThreadLimit` and `runAfterBuild` had no `labelText`; the
  closure put a `QLabel` and a bare string next to them. An aspect that is only
  ever labelled by its layout looks fine until the layout goes, and then it is
  an unlabelled field. Worth checking for on every page: `setLabelText()` on the
  aspect, not a label in the layout.
- **The warning.** The `InfoLabel` under the tree is a `TextDisplay` aspect now,
  and the settings object updates it from the framework aspect's
  `volatileValueChanged()`. The condition (nothing ticked, or both a framework
  and a tool) belongs to the aspect, which is why it answers `warning()` and
  `warningToolTip()` rather than leaving each renderer to work it out.

`apply()` writes settings and rebuilds the test tree, so the test leaves it
alone and covers what the rewrite made pure: the rows, the flags, dirty
tracking and cancelling, and the warning.

#### QML/JS Editing: the last of the three widget-state aspects

`AnalyzerMessagesAspect` was the worst of the three, because its `apply()`
*returned early* when no page was open - an aspect that silently applies nothing
unless someone looked at it. It keeps the two lists it persists now, plus a
volatile copy the model writes into, so `apply()`, `cancel()` and `isDirty()`
are all answers it can give on its own.

One thing to be careful with: what is stored is the list of *disabled* messages,
so the Enabled column shows the opposite of what it writes. Getting that
backwards turns every check off the first time anything is touched, which is why
there is a test for the inversion alone.

The tree's context menu is gone. "Reset to Default" is an `ActionAspect` beside
the table, which is discoverable in a way a right-click on a tree is not.
`QdsInstallAspect` - a whole aspect class whose only job was to host one button
in `addToLayoutImpl` - is an `ActionAspect` too, and the explanatory paragraph
above the Design Studio command is a `TextDisplay` with a link.

#### What a screenshot found that the tests could not

Twice now, looking at a rendered page found defects that every delegate test
passed over, and both times most of them were not that page's fault:

- A Qt Quick `SpinBox` formats for the locale, so a port number read "46.327".
  Chasing that found `displayScaleFactor` and `specialValueText` missing too -
  the Testing page was showing a 60-second timeout as "60000 s".
- The style's `TextField` had **no placeholder item at all**, so
  `placeholderText` showed nowhere in the UI.
- A delegate reserved the 200px label column even for an aspect with no label,
  which pushed controls into the middle of the page with nothing beside them.
- `AspectGroupBox` used an *invisible check box* as its label when it had
  nothing to check, so every plainly-titled group on every ported page had no
  title.
- A `GroupBox` does not lay its children out. They kept their implicit size, so
  a table inside a group stayed as narrow as its own labels and a paragraph of
  text ran off the page. `AspectGroupBox` stacks them and fills the width now,
  which is what the `Column` inside the widget `Group` did.

The lesson is not "write more delegate tests". Two of these five are invisible
to any test that reads properties off items: a label that is clipped out of view
still has its `text`, and an item at its implicit size still reports the size it
asked for. **Look at the page.**

#### The text editor: a highlighter and an indenter need no TextEditorWidget

The three pages that are left - Code Style, Snippets, Font && Colors - were
recorded as waiting on "the TextEditor ported to Qt Quick", which sounded like
reimplementing the editor. Two things make it much smaller than that.

**Font && Colors needs no text editor at all.** `ColorSchemeEdit` is a list of
format descriptions with colour pickers and style boxes for the selected one -
the list-with-details shape, not a code view. What makes it expensive is
something else: the list renders each item *in its own format*, and the aspect
that owns the page is one `Custom` aspect covering both the font group and the
scheme editor, so it is all-or-nothing.

**A highlighter works on a `QTextDocument`, and a Qt Quick `TextEdit` has one.**
`TextEditor::SyntaxHighlighter` takes a `QTextDocument *`, not a `QTextEdit`, and
`QQuickTextDocument::textDocument()` hands one over. So `CodeHighlighting`
(`texteditor/codehighlighting.{h,cpp}`, a QML element) attaches a real
`Highlighter` with the definition for a mime type to a `TextEdit`'s document -
the same four calls `HighlighterHelper::highlightCode()` makes - and
`CodeView.qml` is a `ScrollView` around a `TextEdit`. None of `TextEditorWidget`
is involved. There is a test that the document really ends up in several format
runs, not merely that the object was built.

Two things to copy:

- **The font and the colours come from the editor, not from the design tokens.**
  A code view that used `Fonts.body2` and `Tokens.textDefault` would look like
  the form around it instead of like the editor, so `CodeHighlighting` exposes
  `font`, `textColor` and `backgroundColor` from `globalFontSettings()`. The
  test compares them against the settings: a default `QFont` has a family too,
  so "not empty" passed whatever the getter returned - that control did not bite
  until it compared.
- **Attaching a highlighter with no definition is fatal**, so the "did we find
  one?" check is load-bearing rather than defensive. `highlighting` is a
  readable property for exactly that reason: a view with no definition shows
  plain text, which is worth being able to tell apart from a broken one.

The indenter came the same way. `ICodeStylePreferencesFactory::createIndenter()`
takes a `QTextDocument *` too, and `Indenter::indentBlock()` takes a
`QTextBlock` and a `TabSettingsData` - no editor anywhere. So `CodeIndenting`
attaches one and re-indents every line whenever the code style changes, which is
what makes a preview a preview.

Its test is the one worth copying. "Something got indented" passes with any
settings at all: replacing the code style's tab settings with a default
`TabSettingsData` left the output identical, because the default indent happens
to be the configured one. The test widens the style's indent by three and
expects the preview to follow, *without* asking it to - which is the actual
contract. Only then did that control bite. Note it has to widen
`currentPreferences()`, not the top-level object: the current settings come from
whichever delegate is in effect.

Snippets needed one more thing before it could use either: **a snippet group had
no mime type.** `SnippetProvider` carried a decorator, and a decorator is a
`std::function<void(TextEditorWidget *)>` - it tells a widget how to highlight
itself and is useless to anything that has no widget. So `registerGroup()` takes
the mime type its snippets are written in, and the five registrations say what
theirs are. The test walks every registered group and fails on one that names
none, so a new group cannot quietly become unhighlightable.

Snippets uses both now, which is what they were for. What is still missing for
Code Style is the per-language value editor.

#### Snippets: a detail pane is a sibling aspect

The page shows the snippets of a group and the content of the selected one. The
content lived in the editor widget, and which row was selected lived in the
table widget, so neither could be reached without opening the page.

Both are siblings now: a `SelectionAspect` for the group, a table aspect over
`SnippetsTableModel` (which was already a `QAbstractTableModel`), a
`StringAspect` for the selected snippet's content, and five `ActionAspect`s.
`TableDelegate` gained a readonly `currentRow` - **in the aspect's own model,
not the filtered one**, since a page showing a detail of the current row means
that row rather than the one at that position on screen.

Two things worth copying:

- **A two-way binding needs exactly one guard, in one place.** Selecting a
  snippet writes it into the content aspect; editing the content writes it into
  the model. The first version had two guards - a `QSignalBlocker` around the
  write *and* a value check in `setCurrentContent()` - and removing the blocker
  changed nothing, because the value check already stopped the loop. Two guards
  where one is load-bearing means the control for the other cannot bite. The
  blocker is gone and the test asserts that *selecting* a snippet leaves the
  aspect clean, which is what the remaining guard is for.
- **A list that a registry fills is empty when the page is built.** Snippet
  groups register while the plugins start up, so the group picker came up blank.
  It is filled from `setSettingsProvider()`, which runs when the page is opened.
  That is the third time this shape has come up - test frameworks, analyzer
  messages, snippet groups - and it is always the same fix.

#### Code Style: three editors behind one seam

Code Style is the last of the pages, and it is three jobs rather than one. There
is a page per language - C++, QML/JS, Nim - and each renders whatever
`ICodeStylePreferencesFactory::createValueEditor()` hands it: for C++ a tabbed
widget over a plain struct of twenty-five bools, for QML/JS a formatter picker
with three stacked settings widgets, for ClangFormat a self-managed editor.
None of them is small, and none is aspect-based.

What is in place is the seam, in two halves. A factory can name a Qt Quick form
(`setQmlSource()`), and it can hand over the aspects that form edits
(`setSettingsAspectsCreator()`) - the page itself knows nothing about any
language's settings, so a form that names `aspects.LineLength` needs the factory
to contribute it. `CodeStyleAspect` registers them against the *page-local* copy
of the preferences, so Cancel still means something. Since the page is per
language, **a language can move on its own** and the others keep their widget
editor until they do - three independent batches rather than one leap.

The rest of the page is the page's, not the language's, and is in place too.
`CodeStyleAspect::setupSelectorAspects()` contributes the selector as ordinary
aspects - `Style` (a `SelectionAspect` over the page pool's styles), `CopyStyle`,
`RemoveStyle`, `ImportStyle`, `ExportStyle` and a `ReadOnlyNote` - and a
`CodeStylePreviewAspect` holds the preview text. `CodeStyleSelector.qml` and
`CodeStylePreview.qml` lay them out, so every language's form gets the same
selector and preview by naming two components.

Three things that shaped it:

- **QML only ever sees a page's aspects.** `CodeIndenting` needs the *page's
  copy* of the preferences, which is not a setting and has no aspect of its own,
  so `CodeStylePreviewAspect` carries it as a property alongside the language id
  and the mime type. Anything a hand-written form needs that is not a value has
  to arrive this way; there is no second channel.
- **A selection over a list that changes needs `clearOptions()`.** The styles to
  delegate to come and go as the user copies and removes them, so
  `SelectionAspect` grew one, and `addOption()` now announces itself with
  `controlConfigurationChanged()` like every other setter in that file - without
  which a refilled combo box keeps showing the old choices.
- **Two triggers for one refill means neither control bites.** Keeping the
  selector in step after `syncFromReal()` was first done twice, by a scope guard
  *and* by the `currentDelegateChanged` the sync itself emits. The scope guard's
  negative control could not fail, which is what said it was dead; it was
  removed rather than kept "for safety".

What each of the three still needs, from reading them:

- **QML/JS is done.** `QmlJSCodeStyleAspects` is what the form edits: the
  formatter selection, a nested container per formatter, and
  `QmlFormatOptionsAspect` for the qmlformat table. The four
  `QmlCodeStyleWidgetBase` subclasses, the `QStackedWidget` and the
  `QStyledItemDelegate` are gone; `QmlFormatOptionsModel` stayed and grew the
  `AspectTable` roles, which is all a Qt Quick table needs from it.
- **C++ is done.** `CppCodeStyleAspects` holds the twenty bools, the statement
  macros and `TabSettings`, grouped six ways. The widget page's `QTabWidget`
  became a `Category` selection: the container shows one group at a time and
  puts that category's snippet in the preview, which is what the tabs were for.
  The twenty `QCheckBox` members, the `QPlainTextEdit` and the six
  `SnippetEditorWidget`s are gone.
- **ClangFormat** is self-managed: it lays out its own selector and owns its
  deferred apply/cancel, because its settings live in `.clang-format` files
  rather than in the preferences. It is a port like the others, not an
  exception - no page keeps its widgets.

**A container's visibility needs a widget to live on.** Showing one group at a
time works in Qt Quick by binding to the container's `visible`, but in the
widget path a nested container was added as a bare *layout*: nothing to hide, so
`AspectContainer::setVisible()` did nothing and ClangFormat's embedded C++ panel
showed all six categories at once. The renderer wraps a container in a widget
now. Worth knowing because the two renderers do not fail the same way - the
Quick side was right from the first try and the widget side was silently wrong,
and the only reason it was noticed is that a widget page still embeds those
aspects.

Three things the C++ port turned up:

- **A closure with a second caller cannot be deleted.** ClangFormat embeds
  `CppCodeStylePreferencesWidget` as its legacy indenter panel, so the widget
  stayed - but as a shell over the same `CppCodeStyleAspects` rather than a
  second definition of the same twenty check boxes. Aspects render in both
  backends, which is what makes that possible; it is the pattern to reach for
  when a page's controls are needed somewhere that is still a widget.
- **`RefactoringFile` owns the document it is handed.** `~RefactoringFile()`
  deletes `m_document`, so a stack `QTextDocument` passed to it is freed twice.
  The pointer-declaration formatter now takes and returns a string and makes
  the document on the heap, reading the result back before the file goes away
  with it.
- **`AspectPage` already has a `content` property**, being its default property
  alias. A page declaring `readonly property var content` fails to load with
  "content is a read-only property"; the QuickUi test catches it, but only
  because it loads every page.

**How the CppEditor plugin suite behaves here:** two runs of the same binary
gave 2 and 66 failures, and a third aborted early on a pre-existing ASan
use-after-free in `Core::DocumentModel::documentForFilePath`. It depends on
indexing and is not a usable signal. Run one class with
`-test CppEditor,CppCodeStyleAspectsTest`, and make a negative control require
that the class *finished* - "no FAIL line" is also what a run that died before
reaching the test looks like. Three controls read as passing that way before
this was noticed.

Four things the QML/JS port turned up, worth knowing before the next one:

- **A settings container is reached under one name.** `CodeStyleAspect` calls
  `setQmlName("Settings")` on whatever the factory hands over, so every
  language's form starts `AspectModels.named(aspects.Settings)` rather than
  inventing its own name. `NamedAspects` is one level deep, so a nested
  container needs a `qmlName()` of its own too - `TabSettings` had none.
- **A selection says how it wants to be drawn, and now the Quick form
  listens.** `AspectControls::RadioButtonGroup` used to map to `Selection`, so a
  `SelectionAspect` whose display style is `RadioButtons` - which is the
  *default* - drew a combo box. It maps to a new `RadioGroup` kind and
  `RadioGroupDelegate` now. Four already-ported pages were drawing combo boxes
  where the widget page draws radio buttons; they were changed to match.
- **The preview is the language's, not just the indenter's.** QML/JS runs
  qmlformat over it, so `ICodeStylePreferencesFactory` grew
  `setPreviewFormatter()` and `TextEditor::formatText()` was exported (the
  string-in, string-out half of `formatEditor()`). Where a language sets none,
  Format asks for a re-indent, which is all its formatting is.
- **A model handed to QML must have a parent.** `QmlFormatOptionsModel` is a
  member of the aspect and was default-constructed, so QML took ownership of it
  and freed it - all tests green, exit code 134. This is the third time; see
  `tableModel()`.

**AspectGroupBox and binding loops, since this took three attempts.** Every
page with a group on it used to log two `implicitWidth` binding-loop warnings
per group - 44 across six pages, measured in the running application. The cycle
was the group's label: the style's own label takes `availableWidth` so that it
can elide, but `AspectGroupBox` replaces the label with a `Loader`, and a
Loader's *implicit* width is its item's while the group's width is worked out
from its label's. Binding the Loader's width to the group's closed it. The fix
is to give the Loader no width at all, so the title is as wide as its text -
which is what a `QGroupBox` title was too. Rejected on the way:
`Layout.preferredWidth` on the columns (changes nothing) and `implicitWidth: 0`
on the Loader (silences it, and collapses the page to nothing, because
`topPadding` only reserves room for a label that reports a width).

**A loop cannot be caught by a test that builds one page.** It is reported only
while the Preferences dialog negotiates widths; a page built and shown on its
own settles in one pass and says nothing - verified by restoring the width
binding and watching the check pass. What `testAspectDrivenPagesRenderWithQuick`
does now collect is every other engine warning while it builds the pages, which
is the class that hides failed bindings and TypeErrors. Loops are measured by
driving the app over MCP and counting.

The QML/JS and C++ pages are taller than the viewport and scroll; capping the
table with `Layout.preferredHeight` did not change that.

One piece is shared by all three and is already done for them:
`TextEditor::TabSettings` is an `AspectContainer` of five aspects plus a
warning, so its form is a listing rather than a rewrite. Its *layouter* cannot
go yet - the per-project Editor page still embeds it as a container.

The preview those pages need is built and wired: `CodeStylePreview.qml` over
`CodeHighlighting` and `CodeIndenting`, fed by the page's own
`CodeStylePreviewAspect`. A form gets it in one line, and the language can
vary it: `setPreviewFormatter()` for what an indenter would not do (qmlformat,
the pointer-declaration formatter) and `setPreviewText()` for a form with more
than one thing to demonstrate.

#### The TextEditor in Qt Quick

The last aspect-driven page needs an editor, not a text field: ClangFormat's
page is a Qt Creator editor over the `.clang-format` file, with a language
client attached for completion and Ctrl+S to save. `CodeHighlighting` and
`CodeIndenting` cannot reach that - a highlighter and an indenter attach to any
`QTextDocument`, but a file that is opened, saved and known to a language server
has to *be* a `TextEditor::TextDocument`.

**`QQuickTextDocument::setTextDocument()` is what makes it possible.** A Qt
Quick `TextEdit` makes its own `QTextDocument` and, since Qt 6.7, will show
another one instead. So `CodeDocument` opens a real `TextDocument` for a path
and hands its `QTextDocument` to the `TextEdit`: the view is then showing the
file, and everything the document carries - the highlighter, the indenter, the
marks, `IDocument::save()` - is in the view with it. It is the substitution, not
a copy: typing in the view modifies the file's document, and the file knows it
has been changed.

**A language server has to be told, and only when asked.** The manager learns
about ordinary documents from `EditorManager`'s signals; one opened by a
settings page never passes through it, which is why the ClangFormat page has
always called `documentOpened` by name. `CodeDocument` does the same behind
`useLanguageServer`, off by default - a page that wants diagnostics or
completion asks for them, and one that does not is not to start servers behind
the user's back. `editorOpened` turns out not to be needed: everything it wires
up is guarded on `TextEditorWidget::fromEditor()`, so a view that is not one
gets nothing from it either way.

Testing that meant standing in for the manager. It is reached by object name,
and `getObjectByName()` returns the first match, so a test takes the real one
out of the pool, puts a spy in under the same name, and restores it after. That
is the only way to see the announcement: without a configured server, a real
manager does nothing observable.

**Accepting a completion no longer needs a widget.** This was the one hard
dependency: `AssistProposalItemInterface::apply()` took a `TextEditorWidget *`,
so nothing but a widget could ever accept a proposal. What the items actually do
with it is a closed set - replace a range, insert a snippet, move the cursor,
read a character - so `AssistTarget` names those and `apply()` takes one.
`WidgetAssistTarget` is the widget's, and the widget path is unchanged.

It was much smaller than it looked: one interface declaration, six overrides and
**one call site**, in `CodeAssistant`. What made it look bigger was a handful of
file-static helpers in clangd and C++ completion that passed the widget around
purely to reach `characterAt()` and `textAt()`. Two things genuinely still need
a widget and were left alone: `IAssistProposal::makeCorrection()`, and pasting
in the circular-clipboard assist, which reaches the widget through a
`dynamic_cast` on the target.

**Asking for the proposals needed nothing new.** `CodeCompletion` builds an
`AssistInterface` from a cursor and the document's path, asks the document's
`completionAssistProvider()`, and hands the words to QML as a `QStringList`;
taking one applies it through a `DocumentAssistTarget`. A provider that has to
go and ask - a language server does - answers through the async handler rather
than from `start()`, and both paths land in the same place.

The cursor position has to be remembered from when the completion was asked
for. An item replaces from the proposal's base position to where the cursor
was, and by the time a server answers the view's own cursor may have moved on.

**`CodeEditor.qml` is the four pieces assembled**: a `TextEdit` over a
`CodeDocument`, the file's colours from `CodeHighlighting`, line numbers, and
Ctrl+Space asking `CodeCompletion` with a popup to pick from. Ctrl+S saves.
A page gets an editor over a file in one line.

**Attaching to a TextEdit means attaching to whichever document it has.**
`CodeDocument` swaps that for the file's, and QML declares its children in
order, so a highlighter declared first was left colouring a document nothing was
showing. `CodeHighlighting` and `CodeIndenting` follow
`QQuickTextDocument::textDocumentChanged` now.

**Two things the editors got wrong, both reported from the running
application rather than found by a test.**

- *A ScrollView is as big as what it scrolls.* `ScrollView` reports the
  implicit size of its content, and a `Frame` around it sizes to that - so a
  preview holding more code than fits grew the whole page, and the page
  scrolled instead of the editor. The editors give the ScrollView an implicit
  size of nothing and take the height the layout hands them.
  `testACodeEditorScrollsInsteadOfGrowingThePage` measures it on the real page:
  the editor's content is taller than the editor, and the page's content is not
  taller than the page.
- *Tab typed a tab character.* Not "moved the focus", which is what it looks
  like: a `TextEdit` inserts `\t` for Tab whatever the code style says, so on
  the default spaces-only style it typed something the style would never
  produce. `CodeIndenting::indentAt()` types what the style calls an indent.
  Shift+Tab needed nothing - a control aimed at it stayed green, because the
  text control already takes the indent back.

**Sending the key to the right thing is most of testing this.**
`QTest::keyClick(item->window(), ...)` reaches the Quick focus item directly and
steps over the widget focus chain, which is exactly what takes Tab. The key has
to go to the `QQuickWidget`, and the widget needs the focus as well as the item:
a `QQuickWidget` hands a Tab to the scene from `focusNextPrevChild()`, which is
only reached when the widget is the one being typed into. Two controls read as
"not detected" that way before this was understood.

What is left of the editor: nothing the last page needs. Line numbers are drawn
against the lines rather than stacked as rows, which is what keeps them with the
text when a line wraps - that is layout, so a screenshot is what checks it, not
a test: `TextEdit.lineCount` is 0 until something lays the text out.

#### Font && Colors: twenty aspects and one name collision

`FontSettingsAspect` was one `Custom` aspect that owned the whole page. It is
twenty siblings now - the font group, the scheme group with four
`ActionAspect`s, and the ten properties of the selected format as a detail pane
over the formats list, each shown only where `FormatDescription::showControl()`
says the format has it.

Three things worth carrying forward:

- **A modal dialog in a constructor hangs the tests.** `maybeSaveColorScheme()`
  asks whether to keep changes when the scheme is switched. Building the page
  fills the scheme picker, which counts as switching, so it asked before there
  was anything to ask about - and `exec()` blocked with nobody to answer. The
  guard that says "this is the list being filled, not the user choosing" has to
  cover the whole refresh, not just the assignment.
- **Two classes with one name in one namespace is an ODR violation, and the
  linker picks one.** The new formats model was called `FormatsModel`, which
  `colorschemeedit.cpp` already had. The build was clean and the page looked
  almost right, but calls landed in the other class. It surfaced as a *negative
  control that would not bite*: sabotaging the model changed nothing, because
  the sabotaged code was not the code being run.
- **Check that the sabotage compiled.** The first pass at those controls piped
  `ninja` to /dev/null without testing its exit code, so a sabotage that failed
  to build ran the previous binary and looked like a passing control. Three
  "controls" were meaningless for that reason before the ODR bug was even
  visible.

One thing the test cannot cover: `showFormat()` fills the ten controls with
`setValue()`, which does not emit `volatileValueChanged` - the signal they write
back on. So showing a format cannot edit it, and there is no guard to sabotage.
The assertion is still worth having; it just holds by construction rather than
by a check.

#### A row read in its own colours

Font && Colors lists the syntax formats, and each row is drawn *in the format it
describes* - that is how the list is legible at all. `Qt::ForegroundRole`,
`Qt::BackgroundRole` and `Qt::FontRole` are not in the default `roleNames()`, so
QML could not see any of them; `withRoleNames()` names them (`foreground`,
`background`, `cellFont`) and `TableDelegate` honours them per cell, painting
the background behind the cell rather than tinting its text.

Unset means the form's colours, not black on transparent - which is why the
cell reads `?? undefined` and falls back to the tokens rather than defaulting the
role to a colour.

#### A delegate with no descriptor reads nothing

`ColorDelegate` had no `pres` property at all. Everything its aspect said was
therefore ignored, and the symptom was invisible: `ColorAspect` asks for a reset
button *by default* - it is how a syntax format's colour is unset - and the Quick
picker simply had none. `alphaAllowed` and `minimumSize` were being dropped the
same way.

Worth knowing how it hid. The first fix bound `visible:
root.pres.withResetButton ?? false` to a `pres` that did not exist, so the
binding threw a TypeError, failed, and left `visible` at its default of **true**.
A button appeared, which looked like success. A failed binding does not fall back
to the right-hand side of `??` - it leaves the property alone.

`BaseAspect::resetToDefault()` is what the button calls, going through the
volatile value so a page's Cancel still undoes it. That is what both widget
renderers already did by hand.

#### Give the model a parent

`BaseAspect::tableModel()` is `Q_INVOKABLE` and returns a `QObject *`. A QML
engine takes JavaScript ownership of an unparented `QObject` it is handed, so
its garbage collector deleted `SourcePathMappingModel` out from under the
aspect, and the aspect's destructor then wrote through the freed object at
shutdown. Parent the model to the aspect.

Worth knowing how that showed up: **31 passed, 0 failed** - and exit code 134.
The crash is in the teardown after the totals are printed, so the totals are
useless on their own. The negative control for the fix produces the same thing:
every test green, process aborted.

#### Debugger General: an aspect that owned its value already

`SourcePathMapAspect` was on the blocked list as "a two-column table", and it
was the cheapest of the three widget-state aspects because it was not one:
being a `TypedAspect<SourcePathMap>` it already had `m_value` and
`m_volatileValue`, and only `guiToVolatileValue()` and `volatileValueToGui()`
went through the widget. Moving the rows from the widget into the aspect kept
the pull-based contract exactly as it was - the framework asks, the aspect
answers - so there is no write loop to guard against.

The rows cannot be the value. `SourcePathMap` is a `QMap` keyed by source path,
so a row that is only half typed has no key yet, and two of them are one entry.
The widget solved that by writing `<new source>` into a new row and recognizing
an unfinished one by its angle brackets, which made any path starting with `<`
look unfinished. The model keeps a row list instead and leaves out any row that
is not filled in on both sides.

What went, besides 543 lines of widget: the master-detail editors below the
tree, since the table edits in place, and with them the target's `PathChooser` -
its browse button, history completer and variable chooser. Every other path on
every ported page is a plain field for the same reason, and that is the
`PathChooser` port, not this one. "Add Qt sources..." is an `ActionAspect` now,
and `registerForPostMortem` is registered on every platform rather than only on
Windows, because one shared `.qml` names it either way.

#### Testing a TableView needs a window

`createGenericAspectForm()` is enough to exercise every other delegate without
showing anything, and that is how the rest of these tests work. A `TableView` is
not: it creates its cells when it lays out, and it lays out when it has a
window. In a form that was never shown the cells appear for a moment and then
come away from the model, so `model.display` reads `undefined` inside a signal
handler, `TableView.itemAtIndex()` returns nothing, and `TableView.rows` keeps
whatever the abandoned pass left.

The first version of these tests read those orphans. It went green, and its
negative controls bit, so nothing said the writes it checked were not happening
at all - `writes` stayed where it was and the assertion compared two numbers
that were both wrong. `showForm()` shows the widget and waits for exposure,
which is a real event rather than a delay. Two lessons: assert on the model the
view was given rather than on `TableView.rows`, which does not follow the model
after the first layout without a render pass; and find a cell control by
`objectName` rather than by type, because a non-editable `ComboBox`'s content
item is itself a `TextField`.

#### What the 23 remaining layouters are

Worth knowing before hunting for dead ones, because most of these are not pages:

- **1 is Code Style for ClangFormat**, which is not built here - no LLVM, no
  `libClangFormat.dylib`, and it is absent from the running-plugin list. Its
  factory takes over `CPP_SETTINGS_ID` and replaces C++'s, so where it *is*
  built the C++ Code Style page is the one page still on widgets. It is not
  blocked on
  aspects - `ClangFormatSettings` is already an `AspectContainer` of five, and
  `ClangFormatGlobalConfigWidget` is 312 lines of widgets hand-wired to them.
  It is blocked on the middle of the page being **a whole Qt Creator editor**:
  `ClangFormatConfigWidget` builds one with `IEditorFactory::createEditor()`
  over the `.clang-format` file, registers it with `LanguageClientManager` for
  completion, and binds Ctrl+Space and Ctrl+S. `CodeHighlighting` and
  `CodeIndenting` give a Qt Quick `TextEdit` colours and indenting; they do not
  give it a document, a language client or completion. This page needs the
  TextEditor-to-Quick work first, and is the reason that item is on the list.
- **5 are live nested containers**: `TabSettings`, `TypingSettings`,
  `StorageSettings`, `ExtraEncodingSettings` and `BehaviorSettings`. The Behavior
  page lays their aspects out itself now, but the *per-project* Editor page
  (`editorsettingspropertiespage.cpp`) still embeds all five as containers, so
  their layouters are reached. This was assumed for several rounds; it is
  checked now.
- **The rest are item and panel containers** - an ACP server, a language model,
  a Docker port mapping, an Axivion path mapping, the profiler samplers, Lua's
  scriptable settings, the MCP listen address, per-project Building and Running -
  reached from run configurations and project panels, none of which this
  migration has touched.

So there is no dead-layouter cleanup waiting: the count comes down only as pages
and panels move.


**One page that cannot be ported as it stands,** noted so nobody rediscovers
it: Testing's "Active Test Frameworks" is a `FrameworksAspect` with no
`presentation()`, so it is `Custom` and would vanish. (Display was the other
one, until `named()` above; it draws aspects from two containers.)

**A palette is not a form row.** `ColorDelegate` reserved the full label column
whether or not the aspect had a label, so Terminal's sixteen ANSI swatches would
each have claimed 200 pixels for nothing. It hides the label when there is none,
as the text delegates already do. The four named colors carry the labels the
closure spelled out beside them; the palette stays swatches only, eight to a
row, as it was drawn.

**An `InfoLabel` is a `TextDisplay` with an icon type.** WebAssembly's closure
built six of them - is this an emsdk directory, is an SDK installed, activated,
which version, and two warnings - plus a `QTextBrowser` showing the SDK
environment. All seven are aspects now: six `TextDisplay`s whose `setIconType()`
carries what `InfoLabel::setType()` did, and one read-only `TextEditDisplay`
string for the environment, which is what motivated the text-area delegate
above. `updateStatus()` moved to the constructor and is connected to the
directory changing, rather than being run on the way past by whatever built the
layout.

**A multi-line string was getting a one-line editor.** `TextEditDisplay` is
what an aspect asks for when its value is several lines - GDB's extra dumper
commands, the C++ code model's ignore pattern, a build step's effective call -
and the Quick side folded it into the same `String` kind as a line edit. Two
pages had already been ported when that was noticed. `TextAreaDelegate` draws it
now, writing back when the editor loses the focus rather than per keystroke.

Splitting that `case` is worth a word: `LineEdit` and `PasswordLineEdit` were
*fall-through* labels on the `TextEdit` line, so changing what that line
returned quietly changed all three. The password test caught it.

**A spin box's unit was being dropped.** An `IntegerAspect` can name a prefix
or suffix - the version control timeouts say "s" - and the widget renderer sets
them on the `QSpinBox`. Qt Quick's `SpinBox` has neither property, so the
delegates put them beside it. Until now they were simply not drawn, on pages
already ported. Application Output's closure did the same thing by hand,
splitting "Limit output to %1 characters" around the box; the aspect says it
itself now.

**A radio button is not a check box.** `AspectControls::RadioButton` folded
into the check box on the Quick side, so a page offering a choice of two showed
two check boxes. `RadioDelegate` draws it properly. Which of a set is on stays
the aspects' business - they keep each other in step in C++ - so the delegate
sets `autoExclusive: false` rather than grouping itself and fighting them.

**A destroyed editor still speaks.** Removing a row of the string list editor
destroys its `TextField`, which loses focus, which emits `editingFinished` -
carrying the text of the row that is going away and an index that by then
belongs to a different row. Writing that back undid the removal: taking out the
first entry put its text on the second. A field only has something to say when
the user changed it, so the write-back is guarded on `text !== modelData`. The
test for it looked flaky for a while, which is what a real bug that depends on
whether focus moved looks like.

**Look for the summary-and-a-button shape.** `EnvVarSeparatorAspect` built an
eliding label showing the separators and a Change... button opening a dialog -
which is exactly `AspectControls::TextWithAction`, already described and already
drawn. Three overrides (`presentation()`, `displayText()`, `triggerAction()`)
and it works on both renderers. `EnvChangeAspect` turned out to be a whole class
existing only to re-implement its base's `addToLayoutImpl` slightly worse, so it
is gone and `SystemSettings` uses `EnvironmentChangesAspect` directly.

**Read the widget before designing its replacement.** The plan said Clang Tools
needed a combo with a manage action, and that the config id would have to accept
a choice id back. Reading
`ClangDiagnosticConfigsSelectionWidget` said otherwise: it is one button
carrying the current configuration's name, opening a dialog that both picks and
edits. That is `TextWithAction` - already described, already drawn - so the
aspect needed three overrides and no new control at all. The guess would have
cost a delegate and a kind for nothing.

**Does a page leave anything out?** The gate above exists because the generic
form shows *every* aspect while a layouter picks. A hand-written page picks too,
so it has the mirror-image failure: it can quietly omit a setting. The page test
walks each container and reports every labelled, visible aspect that no delegate
and no group check box drew.

It is reported, not asserted, because the exceptions are real: GDB's throw and
catch breakpoints are edited in the Breakpoints view, Valgrind's cycle detection
in the Callgrind toolbar, and plenty of aspects are stored settings that no page
ever showed. Filtering on "has a `labelText`" removes most of that noise - an
aspect nobody wrote a label for was never meant for a form - and collecting
`AspectGroupBox.checkAspect` removes the rest of the false positives.

What is left is five lines, each checked against the closure the page replaced
and each correct: GDB's `BreakOnThrow` and `BreakOnCatch`, Catch's `WarnEmpty`,
CVS's `LogCount`, FakeVim's `TextWidth`, and Valgrind's `CycleDetection` and
`ShortenTemplates`. A *sixth* line is worth checking the same way. So the 64
ported pages are faithful to what their closures drew, which is the first time
that has been checked rather than assumed.

**Naming the wrong delegate is not an error anywhere.** A check box bound to a
string aspect draws unchecked and writes `true` into it; a one-line field bound
to a multi-line one shows a third of it. Nothing complained, and the pages had
been reviewed. So the page test now checks two things it could not see before:

- The delegate a page named must be one the aspect's kind is drawn by, mirroring
  the choices in `AspectItems.qml`.
- A check box must have text beside it. That is the defect that started this
  whole gate - a page of boxes with nothing next to them - and no other
  assertion could see it.

Between them they found five defects in pages already ported and reviewed:
`showBreak` drawn as a check box on Display (it is a string), and four
multi-line aspects drawn as one-line fields - GDB's two command fields, the
debugging helper commands, and the C++ ignore pattern - plus Extension
Manager's repository list asking for the editor and getting the plain list. The
lesson is not to look harder; it is that a page's QML says what to draw and
nothing was checking it against what the aspect asked for.

**When a page is drawn is not when its settings are constructed.** Two things
came out of Copilot's sign-in button, which is a `Button` whose label *is* the
state - "Sign In", "Checking status...", "Sign Out <user>".

A descriptor read once is not enough: the delegates hold `pres` and nothing told
them to re-read it, so `setActionText()` changed nothing on screen.
`ActionAspect` says `controlConfigurationChanged()` now, and every delegate that
reads a descriptor re-reads it on that signal - which the widget renderer has
always done.

And starting the sign-in check belongs to the page being *shown*.
`AuthWidget` did it in its constructor, so opening the page started a language
server; doing the same from `CopilotSettings`' constructor started one at every
Creator launch - and worse, deadlocked, because `CopilotClient` reads
`settings()` back while that function-local static is still being constructed.
`ActionAspect::setOnShown()` runs when the action is first drawn, on either
renderer, which is where the widget did it.

The spinner is gone; the button disabling itself while it works is what says so
now.

**A page action is an aspect now.** Several closures ended in a `PushButton`
with an `onClicked` - Reset Version Control Cache, qbs's Reset, Install
Extension - which nothing but a layout could see. `Utils::ActionAspect` holds
no value and carries only a label and a callback; its control is
`AspectControls::Button`, drawn by `ButtonDelegate` on the Quick side and by a
`QPushButton` on the widget side. So the button is reachable by name from a
page's QML like everything else, works before and after a page is ported, and
needs no second seam beside `aspects`. Give it a `setQmlName()`: it has no
settings key to derive one from.

One more thing a closure can be: shared. `CppcheckSettings::layouter()` is a
method, and the manual-run dialog builds a widget from it too, so the page
naming QML does not free it. Check for other callers before deleting one.

Measured by loading every plugin into the QuickUi test (`-test QuickUi -load
all`, minus `QmlDesigner` and `UpdateInfo`, see below): **100 aspect-driven
pages, all 100 with their own QML and rendered with Qt Quick, none still on
widgets.** The count goes up as widget-creator pages become aspect-driven, so
it is a running total rather than a target. Gerrit is the first of the widget-creator pages below to have joined
that count: it became aspect-driven and then got a form, which is the shape the
rest of them take.

Nothing said that until now. A page that names no QML gets no `QQuickWidget`,
and the test compared the two against each other - which agrees with itself
whichever way a page goes, so a page falling back to widgets passed. Now that
the backlog is empty the test asserts it is: a declined page is a regression.
The one allowance is C++'s page where ClangFormat is built, since that factory
replaces C++'s with a self-managed editor and no form; it is named by page id,
so a rename fails safe rather than silently widening the hole.

**A header bar for columns that have no names.** `TableDelegate` built a
`HorizontalHeaderView` for every table, including the one-column lists that are
most of them, and the style's own heading binds its label to a name the model
does not have - one QML warning per column, on every such page. Making the
header invisible does not help: the heading is incubated before `visible` is
evaluated and warns on the way, so it is a `Loader` that is not active at all.
What "has a name" means is whether `headerData()` answers a *valid* variant:
`QAbstractItemModel`'s own default answers the column number, and only a model
that overrides it - every model in this tree - says nothing.

The warning was found by showing each page in a window and resizing it, which
is the same measurement the `AspectGroupBox` note describes. Worth repeating
after a batch: it is the only thing that sees a warning the page census cannot,
and it also confirmed that no page has a binding loop left.

### The Toolchains page, and the nine kinds behind it

All nine are converted and `ToolchainConfigWidget` is gone; the page is
`ToolchainsPage.qml`. The extension point is `createConfigurationAspects()`
now, and `toolchainconfigwidget.{h,cpp}` is `toolchainconfigaspects.{h,cpp}` -
what is left in it is the shared aspects and nothing that draws.

The order was: the pieces first (`ToolchainConfigAspects`, `AbiAspects`,
`TargetTripleAspects`), then GCC, then the eight that were smaller. Converting
the pieces first meant the widget subclasses kept working after each step, so
every step built and ran; converting a subclass first would have meant nine
broken pages until the last one was done.

Four of the nine never had a widget of their own: WebAssembly, Android,
HarmonyOS and iOS all delegate to `GccToolchain::createConfigurationAspects()`,
so converting GCC converted them. MSVC has two - the MSVC page and clang-cl's -
and neither is reachable here, which is what the coverage test below is for.

**The page's own controls needed one new thing.** Add is not a button that
does something; it is a button that offers one entry per kind. `ActionAspect`
takes `setChoices()` and hands what was picked back by id through
`triggerChoice()`, and both renderers draw it - a `QMenu` on the push button,
a `Menu` in the delegate. A menu built beside a closure could not be seen by
either renderer; this one is in the descriptor like everything else.

Testing a Quick `Menu` is not like testing the rest of a page: a popup is not
in the item tree, so `findQmlComponents()` sees nothing. `ButtonDelegate`
exposes it as `readonly property alias menu` and the test asks the menu for its
items.

**What a page cannot test, the descriptor can.** Only two kinds of toolchain
exist on any one machine, so a test that looks at what is installed proves
almost nothing. `testEveryKindOfToolchainAsksWithAspects()` makes one toolchain
of each *registered* kind by hand and checks that its factory answers with
aspects, that nothing in them asks for a control no renderer knows
(`AspectControls::Custom`), and that the QML names are there and differ. Ten
kinds on macOS, including all three BareMetal ones and QNX.

**Running the page found what no test did.** The census builds every page and
asserts every delegate found its aspect, and it was green while the ABI was
drawn as six combo boxes stacked one under the other, each on its own row -
because "six aspects, six delegates" is exactly what the generic form is for.
The widget drew them as one row, and nothing in the descriptor said so.

`AspectContainer::setInlineRow()` says it: a container whose aspects read as
one value is drawn as one labelled row with no group box, by
`InlineGroupDelegate` rather than `GroupDelegate`. `AspectItems` became a
`GridLayout` so that one file serves both directions - one column is a column,
as many columns as there are aspects is a row.

The row has to be told to fill: six form-width controls side by side are wider
than the page, and a row that keeps its implicit width pushes the page out
rather than being shrunk into it. That is what made the toolchain list's last
column disappear off the right edge, and it is invisible to a test that only
asks whether the delegates exist - so the test asks where they are and how
wide.

**A page that still uses widgets can host a converted kind.** That was the
staging plan and it turned out not to be needed - the last kind and the page
landed together. It still holds for Kits (16 `KitAspect`s) and Devices (13
`IDeviceWidget`s), which are too big for one step: a settings page that is
itself a `QWidget` can show `Core::createAspectForm(aspects)` for what has
converted and a widget for what has not. Only the *page* has to wait for the
last of them, because a Qt Quick page cannot host a `QWidget` at all.

### The ABI, which six toolchains and qmake all pick

After the shared settings, the next thing every toolchain page is made of is an
`AbiWidget` - six kinds of toolchain use one, and so does the qmake step. It is
`AbiAspects` now, and `AbiWidget` draws those aspects; no call site changed.

What is being picked is one of the ABIs the toolchain reported, or the five
parts of a custom one when none of them is right. Three things about it are
easy to get wrong and none of them had a test, because none could be reached
without a window:

- the five parts are the user's only in custom mode,
- which flavours are offered follows the operating system, and changes while
  the page is open - the case `SelectionAspect::clearOptions()` exists for,
- the `<custom>` entry keeps what was being assembled there while another ABI
  is looked at.

**Two of those tests were green whether or not the code did anything**, and
were only found by aiming a control at each. One of them asserted behaviour the
widget never had: going back to `<custom>` restores what was being *built*
there, not the ABI picked in between. A conversion is not the place to change
that, so the test says what the widget does.

### Starting Toolchains from the bottom

Nine `ToolchainConfigWidget` subclasses had to move before the Toolchains page
could, because a Qt Quick page cannot host a `QWidget` for the ones that have
not. There was no first batch that ported the page - but there was one that
made every later batch smaller.

What *every* toolchain is asked is the same: what it is called, where its
compilers are, and whether the C++ one was given by hand rather than derived
from the C one. That lived in the widgets showing it. `ToolchainConfigAspects`
holds it now, and `ToolchainConfigWidget` draws those aspects into the same
`QFormLayout` the subclasses add their own rows to - so none of the nine
changed, and each can move on its own.

This is the second workstream's shape, not the first's: no page moved, but the
state stopped living in a widget. The same step is what Kits (16 `KitAspect`
implementations) and Devices (13 `IDeviceWidget`s) need first.

**The part worth testing was the part with no test.** Deriving the C++ compiler
command from the C one is the only real logic in the base, and it is easy to
get subtly wrong - it must not run when the user has said "provide manually",
and it must run when the C command changes. Aspects made it reachable without
a window, and the first version of the test was vacuous: it compared the
derived value to what the factory would derive, which is true whether or not
anything derived it. Clobbering the field first is what made the control bite.

### Starting Kits from the bottom, the same way

Sixteen `KitAspect` implementations stand between the Kits page and Qt Quick,
and the first batch is the one that makes every later batch smaller: what every
kit aspect *is*.

Every one of them is one row - what it is called, what it holds, and the page
that manages the things it offers. That is an inline-row `AspectContainer`, so
`KitAspect` is one now, and the two pieces the base built as widgets are
aspects: the list is a `SelectionAspect` instead of a `QComboBox` the aspect
held a pointer to, and "Manage..." is an `ActionAspect` instead of a
`QPushButton`. `addToLayoutImpl` keeps its shape, so all sixteen still draw and
each can move on its own.

Six of the sixteen needed no change at all: their whole contract was already
model plus getter plus setter. Two more shrank, because what they were doing to
the combo box - poking its size policy - was about the widget rather than about
the value. The toolchain one lost its hand-built `QGridLayout`: each of its
lists carries its own label now, so the grid is what they land in rather than
something built around them.

**Sixteen of the eighteen describe themselves now.** The five that were left
after the base moved were all a control that already existed: the sysroot is a
`FilePathAspect`, the mkspec a `StringAspect`, and qbs properties, the CMake
generator and the CMake configuration are each a summary of a value edited in
a dialog plus the one button that opens it - which is what `TextWithAction` is
for. `ActionAspect` grew a summary provider and answers `TextWithAction` when
it has one and `Button` when it does not, the same way `SelectionAspect`
chooses between a combo box and radio buttons.

`KitAspectControl<T>` is what made that cheap: any aspect, plus the row's own
"Mark as Mutable", so a kit aspect writes `addControl<FilePathAspect>()` and
gets a control that both renderers draw and that carries the right-click.

Environment followed: two `ActionAspect`s and, on Windows, a `BoolAspect`.
Seventeen of the eighteen rows describe themselves; the last is MCU
Dependencies, which deliberately draws nothing.

**Saying what control you want is not the same as a renderer having a case for
it.** The widget renderer had no `TextWithAction` case at all: the five aspects
that used it each built their own label and button in `addToLayoutImpl`, so the
gap was invisible. The three kit rows converted to it drew a label and nothing
else, and every test that read the descriptor was green. What said so was a
`QTC_CHECK` in the running application - three soft asserts on
`renderAspect()`, one per row - and looking at the page.

So the test builds each row's widgets and asks whether anything on it can be
clicked or typed into. That is the assertion the descriptor-level ones could
not make, and it bites when the renderer case is removed. The renderer has the
case now, which the five hand-built ones can drop into later.

**Embedding was the last thing only a widget could do.** A Qt row shows the
mkspec inside it and a device row shows its type, and that worked by one aspect
reaching into another - `other->addToInnerLayout(box)` - plus a poke at the
resulting combo box's size policy. Nothing about it was visible to a second
renderer.

The row takes the controls over now: `setAspectsToEmbed()` moves the embedded
aspect's controls into this one's container, and `embedIndex()` says where -
a device shows the type before the device it narrows down. `AspectContainer`
grew `insertAspect()` for that. Both `addToInnerLayout` overrides are gone.

Two things had to be got right, and the first version of each was wrong:

- **The row draws what it holds, not what it made.** `addListAspectsToLayout()`
  iterated the aspect's own list specs, so the embedded controls were in the
  container and never drawn: the device rows showed a label and nothing.
- **Several controls are one cell, not several.** Added to the detail grid as
  separate items they gave those two rows four columns where every other row
  has two, and a `QGridLayout` sizes a column across all of its rows - so the
  whole form got wider and the buttons beside the kit list fell off the edge.
  They go in one `Row` between the label and the Manage button.

Neither was visible to a test that reads descriptors, and neither produced a
warning. Both were found by opening the page and comparing it with the
screenshot from before the change.

**Two things travelled in the model and would have been lost silently.**

- **Icons.** The device types are told apart by a picture as much as by a name,
  and that came in as `Qt::DecorationRole`. A descriptor with no icon in it
  would have dropped them with nothing to say so, so
  `AspectPresentation::Choice` carries one, and QML gets it as a URL through
  the provider that already existed for `AspectItemListModel`.
- **"Mark as Mutable".** Whether a kit aspect may be changed per run
  configuration is about the *setting*, not about which item is picked, so it
  is a right-click rather than a control. It was a `QAction` put on a
  `QComboBox`, which no other renderer could see. It is
  `AspectPresentation::contextActionText` now, and `AspectContextMenu.qml` is
  the Quick half. A subclass that still draws its own widgets keeps
  `addMutableAction()`.

**A container frees only what it was told to own.** `new SomeAspect(container)`
registers the aspect there and nothing more: no ownership, and not a QObject
child either, so every one of those in a container that is not a singleton
leaks. Reading `registerAspect()` was not enough to be sure of this, so
`tst_aspects` says it out loud. `registerAspect(aspect, true)` is the fix, and
it is now used where this migration created the pattern. There are others in
the tree that predate it - `codestyleeditor.cpp`, `qmlprofilersampler.cpp`,
`combinedsampler.cpp`, `idevice.cpp` - which are not this batch's to change.

### The Kits page, and the bug that had been there all along

The page moved: `KitsPage.qml`, a `GroupedListAspect` for the list, three
`ActionAspect`s for Add and the two settings filters, and a `ContainerAspect`
over the eighteen kit-aspect rows. The kit's name and the icon that stands for
it are one inline row. Kits, Toolchains and Qt Versions are the same page three
times now.

Two things it needed that did not exist:

- **A button can show a picture instead of a name.** The kit icon button was a
  `QToolButton` with an icon and a tool tip and no text.
  `AspectPresentation::actionIcon` carries it. The page census asserts that no
  button is nameless, because a nameless button is invisible to every other
  check; it takes an explained icon as a name now, and skips a button that is
  not shown at all.
- **A hook for "the page is being shown".** The plan said there was none and
  that it blocked the Android SDK page. `AspectContainer::pageShown()` is it,
  called by the widget that draws the form - which the census never shows, so
  work that costs something still does not happen there. The Kits page uses it
  the way its `showEvent` did: another page may have changed what a kit can be
  pointed at while this one was off screen.

Its icon menu also swallowed the button's context menu: "Reset to Device
Default Icon" is one of the entries now rather than a right-click of its own,
since a button that already opens a menu having a second, different menu is
not worth keeping.

**And the page found a bug in every combo box the Quick renderer draws.** A Qt
Quick `ComboBox` puts `currentIndex` back to 0 when its model changes, and the
binding to the aspect only runs again when the aspect's *value* changes -
which refilling a list with the same entry selected does not do. So a list
that is refilled while the page is open quietly showed its first entry while
the aspect held the right one. The kit's device type showed "Android Device"
for a desktop kit.

`SelectionDelegate` binds `currentIndex` again on `onModelChanged`. This was
never about Kits: the toolchain ABI lists, the parent-toolchain combo and the
device lists all refill themselves. Nothing caught it because every test set a
value and read it back, and that path was never broken. The test that catches
it refills with the *same* value selected, which is the only case that fails.

### Devices, and how few of them there turned out to be

The plan said thirteen `IDeviceWidget` implementations. There are **seven**,
and four of them are already nothing but a `Layouting::Form` over the device's
own aspects - `IDevice` is an `AspectContainer`, and Linux, Windows, Docker and
Qdb (which rides on Linux) had already been written that way. Three more
devices return no widget at all.

So the batch that mattered was the one widget that kept its state outside the
device: the desktop one hand-rolled a `QLineEdit` with a port-list validator,
seeded it from `freePorts()` and pushed it back in `updateDeviceFromUi()` -
while `IDevice::freePortsAspect` already existed and the Linux widget was
using it directly. It is the aspect now, and the only non-empty
`updateDeviceFromUi()` in the tree is gone.

**Two widgets were working out the same thing and drawing it themselves.**
"You will need at least one port" was a `QLabel` in one and an `InfoLabel` in
the other, with two different texts and two different conditions, neither
visible to any renderer but the one that built it. Which ports a device has is
the device's business and so is saying that it has none:
`IDevice::freePortsWarning` is a `TextDisplay` beside the port field, and both
widgets just place it.

### A device with nothing to set still has something to say

The iOS and Android device widgets were read-only: a `Form` of five label/value
pairs in one, a hand-rolled `QFormLayout` of five to nine in the other, both
built from plain getters. `IDevice::deviceInformation()` already returns
exactly that shape and is what a target's overlay icon shows in its tool tip -
but iOS returned a *different* list and Android returned nothing, so the two
disagreed with themselves.

There is one list now. `deviceInformation()` says what a device reports,
`refreshDeviceInfoAspects()` turns it into one read-only row each, and the
settings page draws that where a device has no widget. Both widget classes are
gone, and Android's four static message-box helpers - which had been sitting on
the widget class with nothing to do with it - are free functions.

`AspectContainer::clear()` is what made that possible: a container whose
contents are not fixed, since a device that has come up reports more than one
that has not.

**A labelled `TextDisplay` drew only its value in the widget renderer.** The
Quick delegate has always drawn the name beside it; `renderTextDisplay()` added
the `InfoLabel` and nothing else, so "Serial number:" was simply missing. It
went unnoticed because every aspect that sets a label on one - the debugger's
type and version, Axivion's build date, qbs profiles - is on a page that had
already moved to Quick. The renderer uses `addLabeledItem()` now, like every
other control.

### The last widget a device reached outside itself for

BareMetal picked its debug server provider with `DebugServerProviderChooser` -
a combo box and a Manage button in a `QWidget` of its own, populated from
`DebugServerProviderManager`, kept in step with the device by hand in
`debugServerProviderChanged()` and `updateDeviceFromUi()`. It was the one
device widget that embedded something from outside the device, and the direct
analogue of the kit-aspect embedding solved earlier.

The device offers the choice itself now: `m_debugServerProviderId` is a
`StringSelectionAspect` - the same stored value, the same settings key, so
nothing about saved devices changes - filled from the manager and refilled when
`providersChanged` fires. Manage is an `ActionAspect`. The widget is a
`Layouting::Form` over those two, like Linux's and Windows's, and its
`updateDeviceFromUi()` is empty like everyone else's.

The chooser widget stays: the *wizard* that adds a bare-metal device still uses
it, and a wizard is not a settings page.

Every remaining `IDeviceWidget` is now nothing but a choice of which of the
device's aspects to show and in what order. That is what has to become a
per-device QML file - or a list of names the page reads - before the Devices
page itself can move.

**A test on a machine with none of the thing it tests proves nothing.** The
first version asserted "one entry per valid provider" on a machine with zero
providers configured, so the loop never ran and the control that broke the id
lookup passed. It registers a provider of its own now, and skips honestly if
the plugin cannot make one.

### What the Docker widget knew that the Docker device did not

Docker's form was already all device aspects except for four things the widget
worked out and drew itself, none of which any other renderer could see:

- **Whether the daemon is up.** A `QLabel`, a `QToolButton` whose icon changed
  with the state, and an `updateDaemonStateTexts()` the widget called from
  four places. That is a summary plus the button that has it looked at again,
  which is what `TextWithAction` is - and the icon that changes with the state
  is what `actionIcon` was added for.
- **That there is nothing to mount.** An `InfoLabel` standing in for the
  `mounts` aspect's own label, turned to Warning when the list was empty. It is
  a warning row beside the field now, like the free-ports one.
- **What the `docker create` call comes to.** A word-wrapped label kept in step
  with `volatileValueChanged`.
- **Re-asking the daemon for its networks.** A tool button with a reload icon
  and no text.

All four are aspects on `DockerDevice`, and so is the fifth thing: the
detection recipe, its three buttons and the log they write to. That one is
genuinely runtime state rather than settings, and the widget was the wrong
owner for a different reason - closing the settings dialog took the running
recipe with it. The device is the thing being detected on, and it outlives
whatever is drawing it.

**Every `IDeviceWidget` left in the tree is now a list of which of the
device's aspects to show and in what order.** Nothing else. That is what has
to become a per-device QML file - or a list of names the page reads - before
the Devices page itself can move, and it is the same question the toolchain
kinds answered with `createConfigurationAspects()`.

### The two closures every device carried

`IDevice` handed out two `std::function<void(Layouting::Layout *)>`s that built
widgets: `deviceToolsGui()` made three group boxes - what a device can run
tools with, what it builds from, and how it finds those out - and
`autoDetectGui()` made a "Run Auto-Detection Now" button beside a read-only
log. All four device widgets dropped both into their forms, and no other
renderer could see either.

They are aspects now: three labelled `AspectContainer`s that *list* the tool
aspects rather than owning them - the aspects stay on the device with the
settings keys they had - plus an `ActionAspect` and a read-only `StringAspect`,
the same shape Docker's detection log took.

**A labelled container drew no group box in the widget renderer.** `GroupDelegate`
has always drawn one. The widget side emerged a bare `Column`, so the three
groups ran into the settings above them with no titles at all. That is the
third renderer disagreement this migration has turned up by moving something
across - after the missing `TextWithAction` case and the unlabelled
`TextDisplay` - and the pattern is the same each time: the Quick delegate is
the one that was right.

**A `br` is part of the closure, not decoration.** `deviceToolsGui()` emitted
one after each group; replacing the call with three container names dropped
them, and the groups packed two-across into the form with a horizontal
scrollbar. Nothing failed - it was only visible by opening the page and
comparing it with the screenshot from before.

### The last two widgets in a device form

Linux and Windows each built the same `QPushButton` labelled "Create New..."
and wired it to the same `SshKeyCreationDialog`, and Linux and Desktop each
worked out "Physical Device" or "Emulator" from `machineType()` and drew that
row themselves - Desktop only ever saying "Physical Device", because that was
all it expected to be.

Both are aspects now. `IDevice::machineTypeDisplay` follows `setMachineType()`,
so a device says what kind of machine it is once. The key button is
`SshParametersAspectContainer::createKey`, which is interesting for where it
had to be split: the *descriptor* belongs with the SSH settings in
ProjectExplorer, but the dialog that makes a key is the remote plugin's. So the
offer starts invisible and `Remote::Internal::setupSshKeyCreation()` installs
the action and shows it - a container with no plugin behind it offers nothing,
rather than a button that asserts when pressed.

With that, **no device widget contains a `new` anything.** All four are a list
of aspect names and nothing else, which is what has to become a per-device QML
file - or a list the page reads - before the Devices page can move.

### IDeviceWidget is gone

A device says which of its aspects a page shows, and in what order:
`addSettingsRows()` fills a container, `addRow()` makes a row that holds
several controls - the SSH host, its port and the host key check are one line -
and `addToolGroups()` appends the three groups a kind that runs tools wants.
The Devices page draws that container, and `IDevice::createWidget()`,
`IDeviceWidget` and all seven of its subclasses are deleted.

This is the same answer the toolchain kinds got with
`createConfigurationAspects()`, and it took the same route: every widget had to
stop holding state, stop building controls and stop embedding foreign ones
before the extension point could be replaced by a list of names.

**The census found a page that had always been blank.** Asking every device
kind that can be constructed here - fifteen of them - whether it says anything
turned up `IosSimulator`, which returned no widget *and* no information, so
selecting it showed an empty box. That has been true for as long as the page
has existed; it says its name, type and ports now.

**A control that cannot fail, again.** The first version asserted that the rows
can be drawn - `createAspectForm()` returning a widget - which is never false,
because `AspectWidgets::layouter()` falls back to a column for a container that
says nothing. The assertion that bites is that the rows are drawn *as rows*:
the emerged widget's layout is a `QFormLayout`. That is the second time this
exact fallback has made an assertion vacuous; it is worth remembering that
"the form exists" and "the form is what the container asked for" are different
claims.

### A button that acts and offers at the same time

The Devices page needs one control nothing before it did. Its **Add** runs the
new-device wizard when clicked, and the arrow beside it is a shortcut straight
to one kind. `ActionAspect` could describe an action *or* a menu, never both:
`setChoices()` replaced the click rather than joining it, because the only
caller so far - the Toolchains **Add** - is a menu and nothing else.

`AspectPresentation::actionIsDefault` says which of the two a button is, and
each renderer already had the right control to draw it. Widgets has
`Utils::OptionPushButton`, which pops its menu from the indicator only; Quick
gets a second `Button` beside the first, visible only when there are choices
*and* the button acts.

Two things worth writing down:

- **`setOptionalMenu()` does set the button's `menu()`.** It differs from
  `setMenu()` by disconnecting `pressed`, which is what stops the menu from
  eating the click. An assertion that the split button has no `menu()` is
  therefore false, and the first version of the test failed on it. What
  separates the two in a way a test can see is the type, and whether
  `clicked()` reaches the action at all.
- **The offer-only button needs an action for the test to mean anything.**
  Asserting that clicking it runs nothing is vacuous while it has nothing to
  run. Give it one, then assert the count does not move.

### A container that is not the same list all the way through

`AspectContainerModel` read `container->aspects()` in its constructor and
never again, and `AspectModels.container()` hands out one model per container
and keeps it. Together those mean a container refilled while its page is open
goes on being drawn as it was when the page was built.

Nothing had needed it yet. Every page so far either lists a fixed set of
aspects or, where the set changes with a selection, swaps `ContainerAspect`'s
*pointer* - which is what Toolchains does with `Configuration`, and why the
stale model was never visible. Devices cannot: what it shows about a device is
rebuilt in place each time the selection moves, and one container per device
kept alive for the session is the wrong shape.

`AspectContainer::aspectsChanged()` says the membership changed - not the
values, which `changed()` already covers - and the model resets on it. The
signal goes out of `clear()` **before** the owned aspects are deleted, so
whatever was drawing them has let go by the time they go away.

### The Devices page, and the state a font used to carry

The last of the three extension-point pages, and the one with the least left
to do by the time it came round: the devices had already stopped holding
widgets, so the page is a combo, a group of four read-only rows, the device's
own container, and four buttons.

Three things it needed that nothing before it did.

**A button that acts and offers.** Add runs the wizard; the arrow beside it is
a shortcut to a kind that needs none. See above.

**A container that is refilled rather than swapped.** What the page says about
a device is rebuilt each time the selection moves. See above.

**A place to put what the font used to say.** `DeviceProxyModel` existed for
one reason: to answer `Qt::FontRole` with italic for a device that is new and
strike-out for one marked for removal. A descriptor has no font, and giving it
one to carry two states would be the wrong shape - so the entry says it:
`Name (new)`, `Name (to be removed)`. That is a visible change, and a
deliberate one. A strike-out is invisible to anyone reading the list out, and
the two states are exactly the ones a user has to know about before pressing
Apply. The proxy model is gone with it, and the two sets it kept are the
page's own.

**What the device already answered.** The General group is the device's own
display-name aspect - registered into the group, not copied out of it, so
typing in the page is what makes the device dirty - plus three `TextDisplay`s.
The last of them used to be a `QPixmap` the device handed over
(`deviceStateIcon()`); it is an `InfoType` now, which is what a renderer needs
in order to draw it in its own idiom. `IDevice::addDisplayNameToLayout()`, the
last `Layouting` call left on `IDevice`, is replaced by
`displayNameAspect()`.

**A test that asks for Remove and gets Add.** `AspectContainer::aspect<T>()`
hands back the first aspect of a type, and a page has four `ActionAspect`s.
The first version of the page test triggered `Add` - which opened the
new-device wizard and sat there until the 300-second timeout. Ask by qmlName
on a page with more than one of anything.

### A button the widget renderer drew and Quick did not

`StringAspect::setUseResetButton()` puts a **Reset** beside the field, and four
settings ask for it: Build & Run's build- and working-directory templates and
Clangd's two index paths. All four pages are drawn with Qt Quick, and
`StringDelegate.qml` never looked at `withResetButton` - so on the shipped
Quick UI there was no way back to the default short of typing it out.

Nothing said so. The descriptor field existed, `aspectmodels.cpp` exported it,
and `ColorDelegate.qml` used it; only the line-edit delegate did not. A census
that asks "does the page draw" cannot see a missing button on a control that
does.

The default value goes into the descriptor with it. A renderer holds a
`BaseAspect`, and `defaultValue()` belongs to the typed subclass, so
"what should this go back to" has to be handed over rather than asked for -
the same reason `displayText()` exists.

`FilePathAspect` gained the flag too. It is what a Qt for MCUs package path
needs: a per-package **Reset** was one of two buttons its hand-built widget
put beside a `PathChooser`.

### MCU, and an extension point with one implementation

`McuAbstractPackage::widget()` looked like the toolchain and device extension
points and was not one: `McuPackage` is the only implementation, and
`McuToolchainPackage` derives from it without touching the widget. So there was
no round of "convert every implementation first" - the one implementation and
the page moved together.

A package says its rows now, the way a device does: `addSettingsRows()` appends
a `FilePathAspect` labelled with the package name and a `TextDisplay` for what
is wrong with it. The two `QWidget` pointers it used to hold - a `PathChooser`
and an `InfoLabel`, built on demand and kept for `updateStatusUi()` to write
into - are gone.

**Reset, and who knows the default.** The chooser had a reset button on the
line edit whose click the *page* caught, because only the page could expand the
target's macros in `defaultPath()` before putting it back. That is a value, not
an event: `setExpandedDefaultPath()` hands it over when the page lists the
packages, `FilePathAspect::setUseResetButton()` draws the button, and the
`reset()` signal on the interface is gone.

**The download button became a link.** A package with a download URL had a
tool button with an ONLINE icon beside the chooser, whose only explanation was
its tooltip. The status line says it in words instead - the same rich-text
link mechanism the page's own "add a CMake tool" message already used. That is
a visible change, and a deliberate one.

**A page that is a list.** Every element of the page is one aspect in
declaration order, so `McuSupportPage.qml` is eight delegates and no layout;
the two package groups are `AspectContainer`s refilled per target, which is
what the container-refill fix above was written for. `showEvent()` - which
asked the SDK what it had every time the page appeared - is `pageShown()`.

### A container that only orders still says how to apply

`AspectContainer::registerAspect()` hands the container's `autoApply` down to
the aspect, and a bare `AspectContainer` starts out **auto-applying**. So a
container made only to say *in what order to draw these* turned every aspect it
listed into one that writes itself through as it is typed - on a settings page
with Apply and Cancel, which is the opposite of what those buttons mean.

`IDevice::fillSettingsAspects()` and `addRow()` did exactly that, and had since
they replaced `IDeviceWidget`. Every device aspect a page listed came back
auto-applying, so editing an SSH host and pressing Cancel kept the edit.

Nothing caught it. The census asks whether a page draws; a page that draws and
applies too early looks identical. The assertion that finds it is on the
aspects themselves - `!isAutoApply()` for every row a device lists - and it is
cheap, so it is worth writing for every new "ordering container".

The same trap in the other direction: an aspect that *is* under a settings page
does not emit `changed()` while it is being typed into, only on Apply. A Qt for
MCUs package listened to `changed()` to update the status under the field and
so said nothing until Apply, where the `PathChooser::textChanged` it replaced
had reported every keystroke. `volatileValueChanged` is the signal, and a test
that builds its rows in an auto-applying container cannot tell the two apart -
`rows.setAutoApply(false)` is what makes it the page's configuration.

### Language servers, and five kinds of client

`BaseSettings::createSettingsWidget()` was the extension point, with **five
implementations** - the base, `StdIOSettings`, and one each for Java, Lua and
qmlls. Four of the five were pure aspect layout: a `Form` over aspects the
settings object already owned, wrapped in a `QWidget` that held nothing. They
say `addSettingsRows()` now, and `BaseSettingsWidget` is deleted.

`applyFromSettingsWidget(QWidget *)` took a widget it never looked at - two of
the three overrides passed it straight to the base and ignored it. It is
`applySettings()`.

**The fifth had a page inside it.** `QmllsClientSettingsWidget` was two
labelled groups, a "Download latest standalone qmlls" button on the executable
field, a `QSingleTaskTreeRunner` to run the download, and one piece of
behaviour - the custom path is only editable when the custom option is chosen.
All four move to `QmllsClientSettings`: the groups are two `AspectContainer`s
it owns, the button an `ActionAspect`, the runner a member, and the behaviour
goes in the constructor where it belongs. With that, the only caller of
`FilePathAspect::addButton()` had no callers left, so it is deleted along with
its widget-renderer branch: an API the Quick renderer never drew, removed
rather than given a counterpart.

**What a Qt Quick view cannot read off a cell.** The tree came up with a
column header saying "1" and no check boxes. Both are things a `QTreeView` gets
without asking: `setHeaderHidden(true)` was a property of the *view*, and
check state comes out of `flags()`, which QML cannot reach. A model drawn by
`TreeDelegate` has to answer `headerData()` with nothing and
`AspectTable::CheckableRole`/`EditableRole` as roles, and hand back
`AspectTable::withRoleNames()`. That is the third model to need this and it is
worth saying plainly: **a model that worked in a QTreeView is not ready for a
Quick view until it answers those three.**

Still widget-shaped here: `attachProjectSpecificSettingsToLayout(Project *,
QLayout *)`, which is the project panel rather than this page.

### Bare Metal, and eleven widgets that were the settings

The largest extension point left, and the one where the widgets did not lay
aspects out - they **were** the storage. `IDebugServerProviderConfigWidget`
and its ten subclasses held a `QLineEdit` per setting and copied values into
the provider on `apply()`; the provider held plain members. Nothing in that
hierarchy was a list of what to draw.

So every provider had to grow aspects before any of it could move: the root
(name, host, port), the GDB base (startup mode, peripheral description file,
init and reset commands, extended remote, executable, arguments), the uVision
base (tools.ini, device, driver) and eight leaves. `toMap()`/`fromMap()` go
through the aspects now, with the settings keys that are already on disk -
which is what the round-trip test guards, because renaming one silently empties
every configured provider on upgrade.

**A summary and a button, twice.** The uVision device and driver pickers were
`Utils::DetailsWidget`s: a summary line, a tool-panel button that opened a
modal chooser, and an expander with the details. The summary and the button
are one `TextWithAction` aspect; the details are aspects beside it, so the
expander is gone and the memory table, the flash algorithm and the SVD path
are simply rows. The chooser dialogs stay as they are.

**Two aspects the page had to define.** `TextWithAction` and `Table` are
controls, not aspect classes - a page that wants one writes a small
`BaseAspect` subclass reporting that control, as External Tools does with
`EnvironmentChangesAspect`. Bare Metal needed both, so `UvSelectionAspect` and
`UvTableAspect` live next to the provider. Three pages have now written the
same two by hand; a fourth is the point at which they belong in Utils.

**Reading a selection with no options asserts.** `TypedSelectionAspect<T>::
value()` maps the index back through `itemValueForIndex()`, which soft-asserts
on an empty list. The GDB startup mode is filled from `supportedStartupModes()`
- a virtual, so not callable from the base constructor - and filling it when
the *page* asked was too late: `isValid()` and `command()` read it with no page
in sight. Each kind fills it at the top of its own constructor instead. The
same shape caught the ST-LINK speeds, whose list depends on the port.

Nothing here can be exercised on this machine - it is all for hardware that is
not attached - so what is verified is that every kind says what it asks, that
what it holds survives a save and reload, and that the page draws.

### Update, and a page that cannot be reached from a test run

Small and mechanical: a checkable group, a paragraph, an interval combo, two
dates and a check box, plus a message and a **Check Now** button. The message
and the button are one `TextWithAction` aspect - the third page to want one -
and the group's check box is `AspectGroupBox`'s `checkAspect`.

**The progress spinner is gone.** While a check ran the page overlaid itself
with a `Utils::ProgressIndicator`, and the message beside the button said
"Checking for updates..." at the same time. The message says it on its own now.

**Nothing here runs in a test.** The plugin refuses to initialize without a
maintenance tool - `Could not determine location of maintenance tool` - so it
is `-noload`ed in the suite, its page is not in the census, and a plugin test
would not run either. What verified it instead:

- **`qmllint`.** `qt_add_qml_module` generates a `<Target>_qmllint` target per
  plugin, and it does resolve the delegate types and their properties. Worth
  running on every new page, not just this one.
- **A real Creator, with the plugin made to start.** Writing any path into
  `Updater/MaintenanceTool` in a scratch `-settingspath` is enough to get past
  the check, after which the page draws and can be looked at.
- **A negative control that is a screenshot.** Renaming the aspect's
  `qmlName()` turns the button into an empty stub and puts two
  `Unable to assign` lines in the log. That is the failure mode a page census
  cannot see and `qmllint` cannot either: `aspects.Foo` is a property map
  lookup, so a name that does not exist is not a type error.

### A summary is a group whose title is the verdict

`Utils::SummaryWidget` is how a page that has to be *set up* - a JDK here, an
SDK there - says whether it is: a line of prose and a list of check marks,
inside a `DetailsWidget` that collapses once everything is in order. Two pages
used it, Android and the Windows App SDK; both have moved and the class is
gone.

There is nothing in it a container cannot say. `Utils::SummaryAspect` is an
`AspectContainer` whose **label text is the verdict** and whose rows are
`TextDisplay`s with an `InfoType` each, so a page draws it with the group
delegate it already has and neither renderer needs to know it is a summary.
A failing check reads as *why* it failed rather than as what it was checking -
the whole point of the control - and `allRowsOkChanged()` fires once when the
verdict flips rather than once per row.

The collapse is gone with the `DetailsWidget`. A group that says "Android
settings are OK." and then lists eight green lines is not worth hiding, and a
group that says they have errors is exactly what should stay open.

### Android, and the last page that could be reached from here

The biggest remaining page and the last `setWidgetCreator` one that this
machine can open. Its parts were already close to aspects - two path choosers
with async validation, a check box, eight buttons - so most of it is a
declaration. Three things needed thought:

**The NDK list said two things with a font and an icon.** A lock icon meant
"installed by the SDK manager, not yours to remove" and italics meant "every
kit is forced onto this one". A Qt Quick cell draws neither, so the source is
a second column - **NDK** | **Source**, reading "SDK Manager" or "Custom" - and
the forced one has `(default)` after its path. The path the buttons act on is
kept beside the text rather than parsed back out of it; a negative control
that removes that distinction produces the file path `/ndk/22 (default)`.

**The icon-only tool buttons became words.** Three `QToolButton`s with a
download icon and a tooltip are `Download JDK`, `Download SDK` and
`Download NDK`.

**`showEvent()` was doing a package scan.** It is `pageShown()` now, for the
reason that hook exists: the census builds every page, and building this one
used to be a `sdkmanager` run.

What made this one verifiable where Bare Metal was not: the SDK is actually
installed here, so the page comes up with its eight checks green, its NDK list
filled and its summary reading "Android settings are OK. (SDK Version: 21.0)".

### ClearCase, and the first page shipped without being looked at

Three groups over the plugin's own settings struct - the shape the doc
describes above, and the twelfth page to take it. Read at construction,
written back on apply.

**A check box that had never done anything.** "&Prompt on check-in" was
created, laid out and connected to the dirty trigger, and then neither
initialized from the settings nor read in `apply()`. There is no ClearCase
setting behind it: what it looks like it controls is
`VersionControlBase::promptBeforeCommit()`, which is a VCS-wide setting with
its own check box on the Version Control page. It is gone. Anyone who ticked
it was changing nothing.

**This one could not be looked at.** The plugin's metadata says
`"Platform" : "^(Linux|Windows)"`, so it does not load on a Mac at all - not
with `-load all`, not with `-load clearcase`. Its page is not in the census
and its plugin tests do not run. What was done instead:

- it compiles, and `ninja ClearCase_qmllint` is clean;
- the names its `.qml` reaches for - `aspects.Configuration`, `aspects.Diff`,
  `aspects.Misc` - were checked against the `setQmlName()` calls in the `.cpp`.
  Everything below those three is drawn by `GroupDelegate` from the container,
  which needs no names at all, and that is why the page is written that way.

That is weaker than every other page in this document, and it is a departure
from the earlier decision to revert two Windows-only pages rather than ship
them unverified. It is here because converting everything and testing later
was asked for explicitly; it is the page to look at first on a machine that
can open it.

**A sweep worth building properly.** Comparing `aspects.<Name>` in a `.qml`
against `setQmlName()` in the neighbouring `.cpp` catches the one failure mode
`qmllint` cannot see. Run across all 155 pages it reports 35, almost all false:
an aspect that never calls `setQmlName()` derives its name from its settings
key, and a text search cannot follow a key held in a constant. The real source
of truth is what the delegates ended up bound to at runtime - and **the census
already asserts that**: every delegate it finds must have a non-null `aspect`,
and it fails on any QML warning. Renaming one `setQmlName()` on the Android
page makes it fail with "ButtonDelegate_QMLTYPE_25 has no aspect". So the guard
exists for every page the census can reach; a script is only needed for the
pages it cannot, which are the six left.

### Eighteen bodies that said the same thing

Once the widget renderer took over control construction, every
`addToLayoutImpl()` in `aspects.cpp` had collapsed to the same four lines:

    QTC_CHECK(renderAspect(*this, parent));

Eighteen of them, one per aspect, and each one a thing to remember when adding
the nineteenth. The base does it instead, and the overrides are gone.

**What decides is the descriptor, in one place.** The base renders when the
aspect names a control, and returns when it says `Invisible` - a container
whose contents the page lays out itself - or `Custom`, which means it has not
been described yet and builds its own. `Custom` is also the *default*, so an
aspect that never described itself behaves exactly as before: nothing draws it
and nothing complains.

**The assertion that made this testable was not the obvious one.** "Nothing is
drawn for an Invisible aspect" passes with or without the guard: the renderer
declines them anyway, because it has no case for them. What the guard actually
prevents is the *complaint* - `QTC_CHECK(renderAspect(...))` failing, which is
a soft assert here and a `qFatal` under `QTC_FATAL_ASSERTS`. So the test
installs a message handler and asserts nothing says SOFT ASSERT. The first
version of it passed against a build with the guard removed, which is the only
reason it was rewritten.

**What this does not do - and now does.** The virtual was kept because aspects
still overrode it. None do, and none may: it is `final`. Every control in every settings page, project panel, build
step, device configuration and run configuration is drawn from its descriptor
by one of the two renderers. They are not one
group. Most are run-configuration, build-step and kit aspects - project
panels rather than preferences pages, which is a surface the migration has not
reached. There were six more on pages that *have* been migrated,
which described a control and kept a closure to draw it anyway -
`SecretAspect`, `EncodingSelectionAspect`, `ClangDiagnosticConfigIdAspect`,
`SuppressionAspect`, `MimeTypesAspect` and `EnvVarSeparatorAspect`. All six are
done; see below.
`aspects.{h,cpp}` reach no QtWidgets header and no `layoutbuilder.h` either;
what keeps them on the widget side of the split metric is that virtual's
signature naming `Layouting::Layout`, and the project panels are what has to
move before it can go.

### A widget class out of Utils

`Utils::SummaryWidget` had two users. Android took `Utils::SummaryAspect`
instead; the Windows App SDK page took it too, and the class is deleted. The
page itself is still widgets - it is Windows-only and cannot be opened here -
but an `AspectContainer` renders as a group through the widget renderer, so
swapping the control did not mean converting the page.

Two things fell out of it.

**The summary was in the layout three times.** `Span(4, winAppSdkDetailsWidget)`
appeared in all three groups. A widget lives in one layout, so the last one
took it and the first two did nothing: the checks have always been drawn under
"Windows App SDK Settings" and never under "Download Path" or "NuGet". Now it
is added once, after the three groups, which is what was actually happening.

**`updateUI()` was only the collapse.** With `setSetupOk()` gone - the
`DetailsWidget` expander went with `SummaryWidget` - the function's whole body
was a variable nobody read. Five call sites and the function are gone.

The Utils split metric moves by one widget class, which is the point: the
end state is a Utils that publishes no `Qt::Widgets`, and every widget helper
that stops having callers is a file that no longer has to be on that side.
A sweep for others found none - the two headers with no includer,
`guitest.h` and `widgetprompts.h`, are both reached from `src/app/main.cpp`.

### A check box for something that is not a bool

The first of the ten `addToLayoutImpl()` bodies outside Utils, and the one that
shows why the others are still there.

`TerminalAspect` already described itself: `presentation()` returned
`CheckBox` with the label "Run in terminal". It still built its own check box,
kept a `QPointer` to it, and pushed values into it from four places -
`fromMap()`, `calculateUseTerminal()`, `setVariantValue()` and the preferences
callback. The reason was one line in the renderer:

    if (auto boolAspect = qobject_cast<BoolAspect *>(&aspect)) { ... }
    return false;

`TerminalAspect` is not a `BoolAspect` and cannot be one: it holds a bool *and*
whether the user set it, so a run configuration that has never been touched
follows the global terminal mode. Failing that cast meant the renderer declined
and the aspect had to draw itself.

**The descriptor is the contract, not the type.** The bool path reads and
writes through `volatileVariantValue()`, which every aspect has, so the cast
is gone: whatever asks for a check box gets one. That is what the Quick side
already did - `BoolDelegate` binds `aspect.value`, which is that same variant -
so this is the widget renderer catching up with it, the same shape as the Reset
button and the `TextWithAction` case before it.

With that, `TerminalAspect` needed only the volatile pair forwarding to the
plain one (a run configuration applies as it is edited, so there is no separate
value being typed), and `emit volatileValueChanged()` where it used to poke the
widget. The closure, the `QPointer` and the four pushes are gone.

**Eight to go, and they are not all the same.** `LauncherAspect`,
`RunAsAspect` and `WorkingDirectoryAspect` went the same way as this one - see
below - leaving six. What is left holds controls that are more than their
value: `ArgumentsAspect` has a chooser plus an expand button and a multi-line
editor. Each needs its control described before its closure can go, which is
workstream 2 applied to the run-configuration surface rather than to a
settings page.

### A combo box for whatever asked for one

The same shape again, one control along. `LauncherAspect` described itself as a
`ComboBox` and listed its launchers as choices, and then built its own combo
because the renderer's combo path began

    if (auto selectionAspect = qobject_cast<SelectionAspect *>(&aspect))

and it is not one. It cannot be: a `SelectionAspect`'s value is a **position**
in the list, and a launcher list is refilled whenever the device's launchers
change, so a position means nothing across a refill. Its value is the
launcher's id.

`AspectPresentation::valueIsChoiceId` already said which of the two an aspect
means - the Quick delegate has read it since the selection work - and the
widget renderer now does too. Two helpers, `indexForValue()` and
`valueForIndex()`, sit between the control and the aspect, and the combo and
the radio-button group both go through them. So the cast is gone: whatever
asks for a combo gets one, keyed the way it said.

`StringSelectionAspect` keeps its own path. Its entries come from a callback
and are refilled while the page is open, which is a live model rather than a
list of choices, and merging the two would be a rewrite rather than a
generalisation.

`LauncherAspect` lost its closure, its `QPointer<QComboBox>`, and the two
methods that existed only to push the list and the selection into it -
`updateComboBox()` and `updateCurrentLauncher()`. Refilling the launchers is
`controlConfigurationChanged()` now, which is what every other aspect says
when its descriptor changes.

**The assertion worth keeping** is the one about a value that is not on offer:
a stale launcher id selects *nothing*, not the first entry. An index-keyed
combo cannot express that, and silently showing the wrong launcher is exactly
what the id keying is for.

### setInlineRow() had never done anything in widgets

The widget renderer has a branch for a container that reads as one value:
label once, then the controls, then a stretch. It was guarded by

    if (pres.inlineRow && !AspectWidgets::layouter(container))

and **`layouter()` never returns an empty function.** It falls back to "lay the
aspects out in order, which is all a Column of them would have done" - which is
right for its callers, and makes the guard permanently false. The branch has
been unreachable since it was written, and `setInlineRow()` has been a Qt Quick
setting only: on the widget backend those ten containers drew as titled group
boxes instead of as one row.

`hasLayouter()` answers what the guard meant to ask - whether one was *set* -
and the branch is reachable. Nothing else changes: `layouter()` keeps its
fallback, because the AspectList details pane relies on it.

**This is the third time that fallback has cost something.** It has already
made two assertions vacuous - "the form was built" is always true when the
builder cannot fail - and the migration plan says so twice. This time it hid a
whole branch. The lesson is narrower than "beware fallbacks": a function that
cannot fail must not be used as a question, and if callers need both answers,
they need two functions.

Found while converting `RunAsAspect`, whose layout closure was exactly what
the branch draws - a label, a combo for which user, and a line edit for the
name when it is "Other". With the branch reachable, `setInlineRow(true)` in the
constructor replaces the closure, and the widget and Quick backends draw the
same thing for the first time.

**And a container that says nothing draws its aspects in order.** That is the
`layouter()` fallback, and it is what `AspectItems` does on the Quick side, so
an aspect that wants two plain rows can rely on it rather than writing them.
`ExecutableAspect` is the next candidate for that, but its `fromMap()` and
`toMap()` currently read and write its sub-aspect by hand; registering the
sub-aspect would put `AspectContainer` in that path too, and that is a change
about persistence rather than about layout.

### A working directory is a path with a reset button

`WorkingDirectoryAspect` was a `BaseAspect` holding two `FilePath`s, a
`PathChooser` and a `QToolButton`, and pushing values between them. All four
are one `FilePathAspect`: the directory is its value, the default is its
default value, and `setUseResetButton()` - added for the Qt for MCUs packages -
is the button. The closure is gone.

The tool button becomes a **Reset** with a word on it rather than an icon with
a tooltip, which is the same trade the Android download buttons made.

**`setDefaultValue()` writes the value too**, and that is what made this worth
a test rather than a read-through. `TypedAspect::setDefaultValue()` is

    m_default = value;
    m_value = value;

because it is meant to be called once, before anything is set. But
`setDefaultWorkingDirectory()` is called *again* whenever the template on the
Build & Run page changes, and a run configuration that had chosen its own
directory would have had it silently replaced. The aspect reads the current
value first and puts it back unless it was following the default anyway.

The first version of this conversion had that bug. The test found it, and it
is the assertion worth keeping: set a directory, change the default, and the
directory is still what was set.

**What the storage still says.** A directory that equals the default is stored
as nothing, so that a later change to the default reaches a configuration that
never overrode it. That is why `toMap()` and `fromMap()` stay: the value is
conditional on the default, which `AspectContainer`'s own persistence does not
express.

### A secret is a control, not a closure

`SecretAspect` described itself as `AspectControls::Secret` and *also* kept a
50-line `addToLayoutImpl()`, because the widget renderer had no case for that
control. Five aspects are in that position; this is the first of them. The
renderer has the case now and the closure is gone.

Writing it once turned three differences between the two backends into one
answer.

**Who knows the secret has arrived.** The QML delegate kept an `arrived` flag
and set it on the first `displayTextChanged()`. That is the delegate deciding
something the aspect knows, and it got the failure case wrong: a keychain that
refuses also emits the signal, so a secret that could not be read became an
editable empty field - and typing into it would have stored nothing over a
secret that is still there. The aspect answers it now. It starts `readOnly`,
`requestDisplayText()` lifts that only on success, and both renderers just read
`isReadOnly()`. The flag is gone from the QML and the QuickUi test asks the
aspect rather than the delegate.

**Why it could not be read.** The closure put the error in the field's
placeholder; the delegate showed nothing. `placeholderText` moved from
`StringAspect`'s signals to `BaseAspect`, with a property so QML can bind it,
and the aspect sets it when the fetch fails.

**That there is nowhere safe to put it.** The closure drew a warning icon
beside the field when no keychain was available; the delegate dropped that too.
It is `presentation().infoType` and the aspect's tool tip now, so the widget
renderer draws the icon and Quick shows the text.

**The test is the aspect's shape without a keychain.** `FetchedSecretAspect` in
`tst_aspectrenderer.cpp` fetches nothing: the test delivers the value, or
fails, by hand, so the moment before it arrives is a moment the test can look
at. That is the moment the field must be read-only in, and an empty field is no
evidence of it - an empty secret is a legitimate answer, which is exactly what
made the old `arrived` flag necessary in the first place.

### An encoding is a combo box with a long name

`EncodingSelectionAspect` already described itself as a combo box whose value
is the chosen entry's id - the encoding's name, not its position - and still
built a `CodecChooser` of its own. The two lists were the same list:
`TextEncoding::availableEncodings()`, shown by `fullDisplayName()`. The closure
is gone and the `CodecChooser *` member with it; the class stays, because the
binary editor uses it as a plain widget.

**One thing the descriptor said and nobody read.**
`AspectPresentation::sizeAdjustPolicy` and `minimumContentsLength` were applied
by `renderStringSelection()` only - the generic combo path built by
`renderSelection()` ignored both, so every `SelectionAspect` combo got
`QComboBox`'s own `AdjustToContentsOnFirstShow` no matter what its aspect
asked for. That had gone unnoticed because no aspect set them; `CodecChooser`
set `AdjustToMinimumContentsLengthWithIcon` by hand, which is exactly the kind
of thing that stops being possible when the closure goes. The generic path
applies them now, and the aspect asks for it in its descriptor.

This is the same shape as `setInlineRow()`: a field on the descriptor that one
renderer honoured and the other quietly dropped. So the rest were checked, by
matching every field of `AspectPresentation` against both backends. What is
left unread is unread for a reason: `pathKind`, `promptDialogTitle`,
`completions` and `defaultValue` are read by the widget backend off the typed
aspect instead, and `filterPlaceholderText`, `showsDefault` and
`allowReordering` belong to `Table`, `GroupedList` and `AspectList`, which that
backend does not draw at all. One genuine hole is left: **`spanY` is read by
neither**, so `setSpan()`'s second argument has never meant anything. `spanX`
is honoured, in `AspectWidgets::addLabeledItem()`.

### The same dialog, written twice

`ClangDiagnosticConfigIdAspect` described itself as `TextWithAction` - a name
and a **Change...** that opens a dialog - and `triggerAction()` opened it. Its
closure built a `ClangDiagnosticConfigsSelectionWidget` whose button opened
*the same dialog*, out of a second copy of the same forty lines. The widget is
gone and with it `guiToVolatileValue()`, `volatileValueToGui()` and the
`QPointer` they moved values through.

**The widget was also doing bookkeeping nobody had noticed.** Its
`customConfigs()` was where `ClangdSettings::m_data.customDiagnosticConfigs`
came from when the page was applied - so *opening* the page was what filled the
list in, and the aspect never held it otherwise. Delete the closure without
looking and applying the clangd page would have written an empty list over
every custom diagnostic configuration the user had. The old code guarded that
with `hasWidget()`, which reads as a lifetime check and is really "has anything
put a list in here yet". It is `customConfigsAreKnown()` now, set where the
list is actually set, and both call sites ask it.

That is the second time in this migration that a closure turned out to be
holding page state rather than drawing - `WorkingDirectoryAspect` was the
first. It is worth assuming: before deleting one, ask what reads the widgets it
builds.

### A list of paths that knows what kind

`SuppressionAspect` said `FilePathList` in its descriptor and drew something
else: a `QListView` over a `QStandardItemModel`, with **Add...** opening a
filtered file dialog and **Remove** taking the selection out. Neither backend
drew that. The widget renderer refused the aspect outright - the case tested
`qobject_cast<FilePathListAspect *>` and Valgrind's aspect is a
`TypedAspect<FilePaths>` - so the closure was the only thing that ever ran, and
Qt Quick gave it a semicolon-separated field with no way to browse at all.

Three separate things had to be true before the closure could go.

**A path list is a variant like everything else.** The case now takes any
aspect and reads `volatileVariantValue().toStringList()`, the same
generalisation the check box and the combo box got.

**The list knows what it holds.** `pathKind`, `promptDialogTitle` and
`promptDialogFilter` were already on the descriptor for `PathChooser`; a list
of paths wants exactly the same three. `PathListEditor` browsed for a directory
and only a directory, so it gained a file filter, and it starts the dialog in
the directory of the last entry - the list is its own memory of where these
things live, which is what the deleted `lastSuppressionDirectory` setting was
for. `FilePathListDelegate.qml` grew an **Add...** that opens the file or
folder dialog accordingly, so Qt Quick can now do what only the closure could.

**The label was the closure's, not the descriptor's.** The widget backend added
the bare editor, so `Paths to mount:` never appeared on the docker and remote
device pages while Qt Quick drew it. It goes through `addLabeledItem()` now and
both say the same thing.

**What the closure was really for.** Every one of its four handlers ended in
`q->guiToVolatileValue()` and then `apply()` or `markSettingsDirty()`, because
the value lived in a `QStandardItemModel` the aspect did not own and nothing
else would have noticed it change. Writing through
`setVolatileVariantValueFromGui()` makes all of that the ordinary path: the
project instance auto-applies because `setProjectSettings()` told it to, and
the global one marks the page dirty like any other aspect.

### The last of the five

`MimeTypesAspect` was a `QLabel` holding the picked types joined by `;`, a
**Set MIME Types...** beside it, and a dialog. It was also **its own storage**:
`guiToVolatileValue()` split the label's text back apart, so the value lived in
the label, and a row with no label built had no value to give.

That is `TextWithAction`, which the descriptor has had all along and which
`ClangDiagnosticConfigIdAspect` uses for the same reason: several hundred MIME
types cannot be listed in place. `displayText()` is the join, `triggerAction()`
is the dialog, and the label is gone.

The list is worth spelling out, because the shape recurred: the five aspects
that described a control *and* kept a closure to draw it were all in that
position for one of two reasons. `SecretAspect` and `SuppressionAspect` asked
for something the widget renderer could not draw (`Secret`; a `FilePathList`
from an aspect that was not a `FilePathListAspect`).
`EncodingSelectionAspect`, `ClangDiagnosticConfigIdAspect` and
`MimeTypesAspect` asked for something it *could* draw and kept the closure
anyway, because the closure was holding state - a widget's `customConfigs()`, a
label's text - that nothing else did. Both are now gone. `EnvVarSeparatorAspect` was
a sixth of exactly the same kind - see below.

What still overrides `addToLayoutImpl()` in `src/plugins` is six aspects,
and they are mostly the project surface: run configurations, build steps and
build settings. Three are not - `KitAspect` and `DeviceToolAspect` on the Kits
and Devices pages, and `LibrarySelectionAspect` - and those are the ones to
look at next if the preferences pages are what matters.

### A sixth, and the one thing that had never been tested

`EnvVarSeparatorAspect` had `presentation()`, `displayText()` *and*
`triggerAction()` - the whole `TextWithAction` description - and a closure that
built the same summary label and the same **Change...** button beside it, with
its own copy of the string formatting and its own copy of the dialog call. It
was dead the day the descriptor was written. It is gone.

Which is six of these, and none of them had a widget test:
`renderTextWithAction()` was the one control path in `aspectwidgetrenderer.cpp`
with no coverage at all, and it is now what MIME types, clangd's diagnostic
configuration, the environment separators and the environment changes are all
drawn by. `tst_AspectRenderer::textWithAction()` covers it: the summary follows
`displayTextChanged()`, the button calls `triggerAction()`, and the aspect is
asked for its summary as soon as there is somewhere to put it - the three
things every one of those four depends on and none of them can test, because
none of them can be asked to open a modal dialog.

### A warning is a row, not a widget the setting owns

`QmlDebuggingAspect` and `QtQuickCompilerAspect` are tri-state settings on the
build configuration, each with a warning underneath. Both built that warning in
their closure - an `InfoLabel` added as a second row - and hung the whole
decision off it: whether the kit can do this at all, what to say about it, and
**whether to force the value back to `Default`**.

Splitting what they list from what they do moved all three out of the layout.
The warning is a `Utils::TextDisplay` registered next to the setting, which is
the row the closure was building and which both backends already draw;
`MakeStep` was already doing this with its `MAKEFLAGS` note. The decision runs
in the constructor and again on `KitManager::kitsChanged`,
`BuildConfiguration::kitChanged` and the aspect's own `changed`.

**That last move fixes something.** The decision used to run only when somebody
opened the build settings page, because that is when `addToLayoutImpl()` was
called. A kit that cannot debug QML left the setting reading *Enabled* -
and the build would be configured that way - until the page was opened. Nothing
about the closure said that was a layout-time question; it was there because
that was where the label was.

**Two free functions, so the answer can be tested.** `qmlDebuggingWarning()`
and `qtQuickCompilerWarning()` take a `Kit` and the settings, and return the
text plus whether it is supported. The aspects are what is left over. The
alternative was a test that needs a `Project`, a `Target` and a
`BuildConfiguration` to ask one question of a kit.

The test builds its kit from `QLibraryInfo::path(BinariesPath)` - the Qt this
Creator was built against, the only one a test run can count on. It has to
`QtVersionManager::addVersion()` it: a kit stores its Qt's id and looks it up
there, so a Qt the manager has not heard of is no Qt at all, and the first
version of the test skipped itself for that reason rather than failing.

### The same shape again, in the qbs build step

`ArchitecturesAspect`'s closure was one line of drawing - the base class - and
twenty lines deciding whether to show the row at all: only a Qt built for
several Android ABIs at once leaves an architecture to choose. The decision
moved to the constructor, to `setKit()` and to `KitManager::kitsChanged`, and
the closure is gone.

It carried the same latent bug as the Qt build aspects, one step worse:
`setVisibleDynamic()` also sets `m_isManagedByTarget`, which
`QbsBuildStepConfigWidget` reads in four places to decide whether qbs is told
the architectures at all. That flag was false until `addToLayoutImpl()` ran. It
happens to work, because the only readers are inside the widget that triggers
the layout - but nothing said so, and an aspect that reports "not managed by
the target" until somebody looks at it is not a thing to leave lying around.

`architecturesAreChosenPerBuild(const Abis &)` is free, for the same reason the
QML debugging warnings are: a list of ABIs can be written down in a test, and a
kit with an Android Qt on it cannot. It is also where the interesting case
lives - a universal macOS build has two ABIs and no choice at all, which the
`abis.size() <= 1` check alone would get wrong.

### A combo box that is refilled while you are looking at it

Chasing the next closure turned up a bug in the path all of these now go
through. `SelectionAspect::addOption()` and `clearOptions()` emit
`controlConfigurationChanged()`, and `renderSelection()` - the generic combo -
never listened. It read `presentation()` once, captured the entries in its
lambdas, and kept them for the life of the control.

Three things follow from that, and only the first is visible:

- the control goes on showing a list that is gone;
- picking an entry writes back **the id at that position in the old list** -
  so choosing "gamma" could store "alpha";
- a value set from elsewhere is looked up in the old list, and lands on the
  wrong entry or on none.

The lambdas ask the aspect for its descriptor each time now. The refill needs
a `QSignalBlocker`: `QComboBox::clear()` moves the current index, and without
one that arrives as though the user had picked it - which is a second way to
lose the value, and the one the test catches by asserting the aspect still
holds what it held.

`renderStringSelection()` had been following the signal all along, which is why
`StringSelectionAspect` exists as a separate path: it is the aspect for a list
that refills. It did not need to be. This is the same bug the Qt Quick side had
in `SelectionDelegate.qml`, where a `ComboBox` reset its `currentIndex` on a
model change and the binding never re-ran - so both backends have now been
wrong about the same thing, separately.

**The test that catches the write-back is the one that clicks afterwards.**
Three of the four assertions here pass with the old captured-entries code left
in one lambda; only picking an entry *after* a refill tells them apart. Worth
remembering: a control that follows a refill is not the same claim as a control
that writes back correctly after one.

### The chooser that had nothing to choose from

`QmlMainFileAspect` said `ComboBox` in its descriptor and built a `QComboBox`
over a `QStandardItemModel` of its own, because the entries are rebuilt
whenever the project's files change and the generic combo path could not follow
that. It can now - see above - so the model and the combo are gone. What is
left is `presentation()` filling `choices` from a `QStringList` of ids, with
`valueIsChoiceId` set: the entries are rebuilt, so a position in them means
nothing across a rebuild.

**The list was empty, and had been all along.** Pulling the file-gathering out
into `mainFileChoices()` so it could be tested made the first test fail with
one entry where four were expected. The reason is one line:

    relativeFiles += projectDir.relativeChildPath(fn);

`relativeChildPath()` is `child.relativeChildPath(parent)` - it asks whether
*this* is below the argument. Called the other way round it asks whether the
project directory is inside `Main.qml`, answers an empty path for every file,
and the `.qml` filter then drops all of them. **Qt Creator's "Main QML file"
chooser has never offered anything but `<Current File>`.** The same file gets
the idiom right three lines away, with `mainScript().relativePathFromDir(
projectDir)`.

That is worth spelling out as a method, not a curiosity. The conversion
transcribed the bug faithfully - it is a straight copy of the loop - and it was
lifting the logic into something a test could call *with a list of paths it
wrote down itself* that surfaced it. A test that had gone through a real
project would have agreed with the code: both would have asked the same wrong
question.

**One deliberate behaviour change.** When the stored file is no longer in the
project, the old code left the combo on entry 0 - showing `<Current File>`
while storing something else. It now selects nothing, which is what
`comboBoxForAnAspectValuedByChoiceId` already establishes as the answer for a
stale id.

### Two warnings under the build directory, and the filter that would have eaten them

`BuildDirectoryAspect`'s closure built four widgets - two spacer `QLabel`s and
two `InfoLabel`s - and then did three things that were not drawing: it wired up
the shadow-build check box, it asked the build device whether paths from a
device are allowed, and it computed the warnings. The two warnings are
`TextDisplay` rows now, the check box is wired in `allowInSourceBuilds()` where
the source directory is actually known, and the build device is followed from
the constructor and on `BuildConfiguration::kitChanged`.

The shadow-build check box is the third of these to have been doing nothing
until somebody opened the page: `connect(this, &StringAspect::checkedChanged,
...)` lived in the closure, so unchecking it before the build settings had ever
been drawn changed nothing.

**The filter that made this not work.** `BuildConfiguration::createConfigWidget()`
laid out `aspects()` with `if (aspect->isVisible())`. A warning starts hidden,
so it was left out of the form, and nothing could bring it back while the page
was open - which is the only time a warning about what you are typing is any
use. The filter is gone: every aspect goes into the form, and the control
handles its own visibility. `registerSubWidget()` has connected
`visibleChanged` to `setVisible` all along.

That filter also silently defeated `ArchitecturesAspect` and
`QtQuickCompilerAspect`, both of which hide themselves and expect to come back
when the kit changes.

**Two assertions that were not assertions.** The first version of the test
checked `label->isHidden()`. Nothing in a renderer test is ever shown, so every
widget in the tree answers `isHidden() == true` and the check cannot fail;
`isVisibleTo(topLevel)` is the predicate that means "would be visible if this
were shown". And even then, removing the initial hide from
`registerSubWidget()` did not make the test fail, because
`renderTextDisplay()` hides it a second time. Neither control bites alone.
Removing both does - which is how the redundancy was found, and it is worth
knowing that a control that does not bite can mean two guards rather than a
weak test.

### A container with no box of its own

`ExecutableAspect` is two `FilePathAspect`s - the executable, and on a device
the alternative to it - held by a `BaseAspect` that laid them out itself,
read and wrote them itself, and forwarded their `changed()` itself. All three
are what `AspectContainer` does, so it is one now. `fromMap()`, `toMap()`, the
`connect`s and the closure are gone, and the alternative is owned by the
container rather than deleted by hand.

What stopped this being a container before is that a nested one draws in a
widget of its own - the group box, or a bare `Column` where it has no title -
and these two are *rows of the page around them*. A box would indent them and
stop them lining up with the settings above and below.

So `AspectPresentation` gains **`flattened`**, the other answer to the question
`inlineRow` asks. `inlineRow` means "one row, no box"; `flattened` means "my
rows, no box". A container that was given a layouter of its own means the
layouter: saying both says the layout, because there is nowhere else for it to
go. On the Qt Quick side it is `FlattenedGroupDelegate.qml`, which is
`GroupDelegate` without the `GroupBox`.

**Three things the tests had to be argued into saying.**

`findChildren<FancyLineEdit *>()` counts every field twice, because
`FancyLineEdit` holds another one. Counting labels instead is not enough
either: a line edit brings its own label along - the macro expander's *Select a
variable to insert.* - so the test names the labels it is looking for.

And the first version rendered into a `Column`, where a missing row break
cannot be seen. `parent.flush()` after each child only matters in a `Form`,
which is what a build or run panel is, so the test builds one and counts
`QFormLayout::rowCount()`. Without the flush the two rows become one, and a
control that had not been biting starts to.

### A check box whose label is a link

`UseGlobalAspect` is the "Use *global settings*" row at the top of a dozen
project panels, where the words are a link to the page those settings come
from. Its closure built a `QCheckBox`, a rich-text `QLabel`, a stretch and a
rule, and this was the last of the aspects blocked on something neither
backend could say.

The obstacle is small and exact: **a check box draws its own text and draws it
plain.** `QCheckBox::setText()` takes no markup and neither does QtQuick's
`CheckBox`, so a label with an `<a href>` in it cannot be the box's text. It
has to be a label of its own, beside the box.

That is a placement, and `LabelPlacement` is where placements live, so it is a
fifth value - `BesideControl`, mirrored as `BoolAspect::LabelPlacement::
BesideCheckBox`. The widget renderer draws `Row { box, label, st }` and reports
clicks through `BaseAspect::activateLink()`, the virtual that exists so a
renderer holding nothing but a `BaseAspect` can say what happened. Qt Quick
gets `BoolWithOwnLabelDelegate.qml`, chosen by kind - so every other check box
in Creator is left exactly as it was, which is the same trick
`FlattenedGroupDelegate` uses.

**The rule moved to the pages.** The closure ended with `hr(&parent)`, so every
panel using this aspect got a divider whether it wanted one or not. A rule
between sections is the page's furniture, not the setting's, so it is one `hr,`
in each of the eleven `Column`s that list the aspect. More lines, but each one
is where somebody can see it and change it.

**A control that did not bite, for a change that was not real.** The delegate
first read `aspect.labelText` rather than `plainLabelText`, on the theory that
the plain one would strip the markup. It does not - `plainLabelText()` strips
the mnemonic `&` and nothing else - so the two behave identically here and the
control could not fail. The delegate uses `plainLabelText` like every other
one, and the assertion moved to what actually matters: the label's `textFormat`
is `StyledText`, because shown as plain text the link is angle brackets on the
page and nothing to click.

### Two lines that did nothing, and how to be sure

`DeviceToolAspect::addToLayoutImpl()` was

    FilePathAspect::addToLayoutImpl(parent);
    parent.flush();

- the base class, and a row break. The three groups that list these tools
already put a `br` after each aspect, and `Layout::flush()` returns immediately
when nothing is pending, so the second break was a no-op. That is easy to say
and easy to be wrong about.

So it was measured instead. `DesktopDeviceTest` already renders
`runToolsGroup` into a form; it now also asserts one form row per tool. The
assertion was added **first**, with the closure still in place, and passed;
then the closure was deleted and it passed again with the same count. That is
the evidence that the flush did nothing - not the reading of `flush()`.

The negative control cannot be on the deletion: nothing can prove a no-op
bites. It is on what the deletion now depends on - take the `br` out of the
group's layouter and the two tools collapse into one row, which the assertion
catches. So the row break has one owner, and the test says which.

### The two in Utils that described themselves and drew themselves anyway

`EnvironmentChangesAspect` said `TextWithAction` and kept a closure that built
an `ElidingLabel` set to `ElideRight` with an Expanding size policy, a
**Change...** button set to Maximum, `createLabel()` with `addEmpty == false`,
and a connection keeping the label in step with the value. That is
`renderTextWithAction()` line for line - written twice, once in Utils and once
in Utils. The closure is gone.

`PortListAspect::addToLayoutImpl()` was

    StringAspect::addToLayoutImpl(parent);

and nothing else. An override that calls the thing it overrides is a comment
that the compiler enforces, and `StringAspect` has not had a body of its own
since the base learned to render itself.

**The test uses the real aspect.** The generic `TextWithAction` path already
has one - `tst_AspectRenderer::textWithAction()`, written for a stand-in when
the MIME types and the diagnostic configuration moved onto it. What that test
cannot say is whether *this* aspect still says the same thing, which is the
only question a deletion raises. So `environmentChangesReadAsASummary()`
renders an `EnvironmentChangesAspect` and reads the summary off it.

**And it had to be told to have two values.** The first version's control -
make `displayText()` read the applied value rather than the volatile one -
could not fail, because an aspect with no container auto-applies, so the two
are never different. `setAutoApply(false)` is what makes "the summary shows
what is about to be applied" a claim rather than a tautology. Same trap as the
MIME types test, one page up.

### Moving a view rather than describing one

`GroupedListAspect` is the toolchains, the Qt versions, the debuggers and the
kits: items in named groups, one of them current, with Clone, Remove and Make
Default beside them. It already said `AspectControls::GroupedList`, and the
widget renderer had no case for it - the same position `SecretAspect` was in -
so it kept a closure that built a `GroupedListWidget` and wired the current row
both ways.

This one is not generalised, it is **moved**. A `GroupedView` is a view of its
own, not something to assemble from a descriptor: the tree, the three buttons
and their enabling all belong together, and pulling them apart into
`AspectPresentation` fields would describe nothing that any other aspect could
use. What the move buys is only where the code lives - which is the renderer's
own stated purpose:

> the bespoke ones key on the aspect type and read the rest through friend
> accessors - the point is where the widget code lives, not that it be generic.

So `case AspectControls::GroupedList:` casts to the aspect and puts the view
in whole. `GroupedListAspect` no longer includes `layoutbuilder.h`,
`groupedview.h` or `QWidget`, and its header no longer has a public
`addToLayoutImpl()` for callers to wonder about.

**The test was already there and already bit.** `tst_AspectRenderer::
groupedList()` asserts the tree, its model, three buttons, and that the aspect
and the view agree on what is current whichever one set it. Deleting the
closure without adding the case leaves it with no tree at all, which is the
first thing it checks. Nothing new had to be written for this one - which is
the point of having written it when the aspect was made.

`AspectList` is the last of this kind in Utils, and it is not a move: its two
layout bodies run to about a hundred and ninety lines and hold state between
them.

### The terminal row, where the two backends showed different things

`TerminalCommandAspect` had a 64-line closure building an eliding summary, a
**Customize...** button that opened a dialog over its three fields, and a
**Presets** button with a menu of the emulators the machine has. The Qt Quick
page did not have any of that: `SystemSettingsPage.qml` drew a group box with
the three fields in it. Two backends, two different settings pages, and
whichever one you saw depended on `QTC_QUICK_SETTINGS`.

Converging meant choosing, and the choice was the widget one: the Presets menu
fills all three fields at once, and dropping it would have been a functional
loss to save writing some QML.

**Nothing new had to be invented.** `ActionAspect` already carries a summary
(`setSummaryProvider()`) and a menu (`setChoices()`, `setOnChoice()`), and
`setInlineRow()` already means "these read as one row". So the aspect is a
container of a summary-and-button, a menu-button, and the three fields - and
the page is one line, `InlineGroupDelegate { aspect: aspects.Terminal }`.

**The fields are hidden, not absent, and that distinction is the whole design.**
They cannot be children of the row - they would be drawn in it - and they
cannot be `setVisible(false)` themselves, because the dialog draws *them* and
would draw them hidden. A container in between solves it: hide the container
where it is listed, and the dialog still draws what it holds. That is what the
nested `command` container is for, and the test says so, because it is the sort
of thing that reads like an accident.

**One test found its own blind spot twice.** Counting the buttons on the page
found three, not two: a `PathChooser` brings a **Browse** along, and the hidden
fields are still in the widget tree - so the count is of *visible* buttons.
And the control for "one row rather than a column" did not bite until the test
actually said what a row is: the summary and both buttons in one
`QHBoxLayout`, checked with `indexOf`. Asserting the parts exist is not
asserting the shape they are in.

### The row that read as one answer but did not line up

Not a closure this time - a divergence the last batch walked into and this one
goes back for.

`setInlineRow()` says a container's aspects are one answer and get one label:
an ABI is five choices and one ABI. The widget renderer built
`Row { label, controls..., st }` and added the *whole row* to the layout. In a
form that is a single item, so Qt makes it **spanning**, and the controls start
after however wide this row's own label happens to be - while every other row's
control starts at the field column. A "Name:" row above an "ABI:" row did not
line up.

`InlineGroupDelegate.qml` never had the problem: it puts the label in a column
of `Metrics.formLabelWidth` and the controls after it. So the widget side was
the odd one out, and the fix is to do what every other control does - build the
row from the *children*, and hand it to `addLabeledItem()`.

Eleven aspects are drawn this way: the kit name row, `AbiAspect`, `KitAspect`,
a device's rows, the Qt for MCUs kit group, four BareMetal rows and the
terminal command.

**The test that had to be rewritten was asserting the bug.**
`containerThatReadsAsOneRow()` checked that the label was inside the row layout
and came before the controls - a faithful description of what the code did, and
of what was wrong with it. It now checks the opposite: the label is *not* in
the row. A test can only tell you that behaviour changed; whether the old
behaviour was the point is not something it knows.

**How the defect was found is worth more than the defect.** It was written down
at the end of the last batch as a thing that "would" misalign, from reading
`flush()`. That is a guess. The first thing this batch did was print
`QFormLayout::itemAt(row, LabelRole)` for a form holding a plain row and an
inline one: `"Name:"` against `<none>`, and `SpanningRole` set on the second.
Ten lines of throwaway code turned an argument into a fact, and the same ten
lines became the test.

### Which simulator to run on

`IosDeviceTypeAspect`'s closure built a `QComboBox` over a `QStandardItemModel`
of simulators, a label, and an **Update** button that asked the system to look
again - and `updateValues()` did the filling, the selecting *and* the deciding
whether the row should be shown at all.

Every piece of it already existed by the time this batch reached it, which is
the point:

- `StringSelectionAspect` is the combo whose entries are refilled while the
  page is open, with a fill callback and `refill()`. Its value is the chosen
  item's *data*, so the list shows names and stores identifiers.
- `ActionAspect` is the Update button.
- `setInlineRow()` puts the two in one labelled row - and since the batch
  before this one, in a row that lines up with the rows around it.

So the aspect is a container of those two, and the closure is gone. Whether the
row is shown follows the kit, in `updateVisibility()`, called from the
constructor, from `fromMap()` and from `deviceChanges()` - not from the page
being drawn, which is the third time this migration has moved that particular
decision out of a layout.

**Names are shown, identifiers are stored.** `simulatorItems()` is free, so a
test can hand it two simulators it made up rather than whatever Xcode has
installed on the machine running the tests. The two things worth pinning are
that the text says which iOS as well as which device - "iPhone 16" alone is
several simulators - and that what goes in the `.user` file is the identifier,
because the names are not unique and change between Xcode versions.

**A cut in the wrong direction, caught by an assertion.** Removing three
functions between `addToLayoutImpl()` and `fromMap()` assumed they were in that
order; `fromMap()` is a hundred lines *earlier*, so `s[:start] + s[end:]`
would have duplicated a chunk of the file. The script asserted that what it was
about to remove contained the names it expected, and stopped. Worth doing on
any edit that deletes a range rather than a match.

### The net before the move

`AspectList` is the last closure in Utils and the only one left that is a
*move* rather than a port - about a hundred and ninety lines across two bodies,
one per display style. `GroupedListAspect` went the same way and went smoothly,
because it had a test asserting the tree, the buttons and the two-way current
row before anything was touched.

`AspectList` had no such test. Nothing exercised its widget path at all: the
Qt Quick side is covered by QuickUi, and the widget side - the one that ships
by default - was covered by nothing. So this batch is the net, and the move is
next.

`aspectListWithDetails()` and `aspectInlineList()` pin what the two bodies do:
the tree and its model, Add making the new item current, Move Up taking the
current item with it, Remove, and - for the inline style - an editor per item
with a remove beside each and one add at the end.

**Three things it got wrong first, all of them the code being right.**

`sync()` does not take a removed row out of the model. It leaves it, struck
through, until the page is applied, because a removal is something to undo
rather than something to do twice - the same rule `GroupedSelection` follows.
The test asserts the strike-out now, which is worth having written down.

A `FancyLineEdit` contains another `FancyLineEdit`, and a *grand*child at that,
so counting editors counts twice and counting buttons counts the macro
expander's. Both are filtered by asking whether anything above them is a line
edit.

And the inline list rebuilds itself wholesale on `volatileItemListChanged`
through `replaceLayoutOn()`, so a button held from before a click is not the
button on screen afterwards. The test asks for them again after every change.

**The control that matters** is the one that empties `addToLayoutImpl()`
altogether: that is exactly what a move gone wrong looks like, and it fails
both tests.

### The move, with the net under it

`AspectList` is out of Utils. Both bodies - the inline list and the list with a
details pane - are in `aspectwidgetrenderer.cpp`, and `addToLayoutImpl()` is
gone from the last aspect in the library that had one.

**What made it a move rather than a port** is that neither body describes
anything: a tree with Add, Remove, Move Up and Move Down beside it, and a
details pane that is whatever the current item is made of, is not a set of
descriptor fields that another aspect could reuse. It is a view, like
`GroupedView`. So it goes where the widget code lives.

**What made it possible** is three lines of new API. The bodies reached into
`AspectListPrivate` for its `AspectListModel` - `findItemAtLevel<1>()`,
`indexForItem()`, `itemForIndex()` - and that class only exists inside
`aspectlist.cpp`. `itemModel()`, `itemForRow()` and `rowForItem()` are the same
trio `GroupedListAspect` already exposes as `displayModel()`, `rowForIndex()`
and `indexForRow()`, and once they existed the bodies needed nothing private at
all. Everything else they wanted - `isOrdered()`, `createAndAddItem()`,
`removeItem()`, `extraButtonTexts()`, `triggerExtraButton()`,
`moveCurrentUp()` - was already public, because the Qt Quick side had needed it
first.

That is the pattern worth naming: **a widget body that cannot be moved is
usually a widget body reaching past the aspect's own API, and the Qt Quick
delegate has already had to solve it.** Look at what the delegate calls.

**A row of the model is not an index into the items.** `itemForRow()` says so
in its comment because the two really do diverge: a removed item keeps its row,
struck through, until the page is applied. The old body knew this implicitly by
searching the model; the new one has to be told, and now so does the reader.

The net written last time caught nothing, which is the point of writing it
first - and the control that empties the renderer's case fails both tests, so
it would have.

### The kit row, which had been describing itself all along

`KitAspect` is an `AspectContainer`, and its constructor already said

    setInlineRow(true);
    setLabelText(factory->displayName() + ':');
    setToolTip(factory->description());

- a complete description of a labelled row whose controls sit side by side.
And then it overrode `addToLayoutImpl()` to draw a label, call a virtual, add
the Manage button and flush, so the inline-row path was never reached. Every
kit row in Creator - the Qt version, the compilers, the debugger, the device,
the sysroot, the generator - was drawn by the closure while the descriptor sat
there saying the same thing.

Deleting it took `addLabelToLayout()`, `addToInnerLayout()`,
`addListAspectsToLayout()`, `addManageButtonToLayout()` and
`addControlsToLayout()` with it, and then **seven subclass overrides that were
the default written out**: `layout.addItem(m_mkspec)` is what
`addControlsToLayout()` does with one control. `EnvironmentKitAspect`'s was the
same for three, down to the trailing stretch. `McuDependenciesKitAspect`'s was
empty and it has no controls.

**One row genuinely differs, and it is the interesting one.** The compilers are
one list per language, each with its own label, and they have to stack where
every other row lays its controls in a line. That is not a descriptor field -
it is a layout, and a container may have one. So `ToolchainKitAspectImpl` puts
its selections in a container of its own with a `Grid` in it, and the row holds
that container as its single control.

The one piece of new API is `setControlContainer()`: `addControl<T>()` inserted
into the row itself, so a subclass had no way to say "mine go over there". Two
lines in `addControl()`, and the row's own framing - label, controls, Manage -
keeps working because it comes from the renderer now rather than from a virtual
each subclass had to remember to call.

**The net was already there**, which is why this was one commit rather than
three. `testEveryKitAspectDrawsSomethingToActOn()` renders every kit aspect
there is and insists something actionable comes out; it was written when three
rows drew a label and nothing else. Two more were added for what this change
is actually about: every row draws its own name, and the toolchain row stacks
one list per language. Removing `setInlineRow(true)` fails four tests,
including both new ones.

### Two requests one backend answered and the other dropped

The four closures still in `src/plugins` all want a control that does not exist
yet - an environment editor over a fetched base, a library-and-version picker,
a field that changes from one line to many while you look at it. Those are
designs, not ports, and inventing one to make a count go down is the wrong
trade. So this batch went looking for the other thing instead.

`AspectPresentation`'s fields were audited earlier - which of them each backend
reads. This is the same audit over the **signals**: what an aspect asks a
renderer to do, and whether both do it. Most of the asymmetry is a false alarm,
because Qt Quick binds where the widget side connects: nothing in the QML reads
`labelTextChanged` or `enabledChanged`, and nothing needs to - `aspect.labelText`
and `aspect.enabled` are properties with NOTIFY behind them. Two were real.

**`controlFocusRequested`** - `setFocusToInputField()`. Five callers, and the
one that shows what it is for is `kitoptionspage.cpp`: add a kit and the cursor
lands in its name so you can type. Qt Quick did nothing at all. It sets
`focus`, not `forceActiveFocus()`: active focus needs the *window* to be the
active one, which is neither the delegate's business nor something a test can
count on - the QuickUi suite already has one test that flakes for exactly that
reason.

**`controlValidationRequested`** - `validateInput()`. The widget side re-runs
its validator; the QML asks the aspect again through `validationMessage()`, and
the tick that makes it re-ask was bumped only by `validationMessageChanged`.
So an aspect told from outside what is wrong with its value - a build system
reporting an unreachable build directory, which is `BuildDirectoryAspect::
setProblem()` - said so in widgets and stayed silent in Qt Quick.

Both are three lines of QML. Finding them was the work, and the method is worth
keeping: **list what one side handles, list what the other handles, and explain
every difference.** A difference that turns out to be a binding is fine. A
difference nobody can explain is a bug.

### Where the migration stands, and why the last four are different in kind

**The settings pages are done.** `testAspectDrivenPagesRenderWithQuick()`
reports 107 aspect-driven pages, 107 rendered with Qt Quick, **none declined** -
the "still on widgets" list is empty, and the same test refuses to let it grow
again: a page that has aspects and no QML fails it. `createAspectForm()` is
what makes that final rather than a setting, because a page naming a QML file
renders through it whatever `QTC_QUICK_SETTINGS` says; the variable only
previews the pages that have none, and there are none.

Four `addToLayoutImpl()` overrides remain, and none of them is on a page. They
are a run configuration's arguments, a build step's command helper, a device's
environment and the compiler explorer's library picker - project panels and an
editor pane, which is a surface this migration has not been about.

**They are also not movable, and the reason is structural.** Ten closures went
into `aspectwidgetrenderer.cpp` over this migration; the last two,
`GroupedListAspect` and `AspectList`, were *moves* rather than ports - the
widget code went where widget code lives and nothing was redesigned. That
worked because both aspects live in **Utils**, so the renderer can name their
type:

    if (auto list = qobject_cast<AspectList *>(&aspect))

Every remaining closure lives in a plugin, and `Utils` depends on no plugin. A
bespoke case cannot cast to `ArgumentsAspect`, `CommandBuilderAspect`,
`LibrarySelectionAspect` or `DockerDeviceEnvironmentAspect`, and two of them
build widgets Utils cannot even see: `EnvironmentWidget` is ProjectExplorer's
and `Api::Library` is the compiler explorer's.

So for an aspect outside Utils there is exactly one way out of a closure:
**describe it with descriptor fields both backends already understand.** Where
no such description exists, the way out is to invent a control - and that is a
decision about what the thing should look like, not a port. Each of the four
needs one:

- **`ArgumentsAspect`** wants a field that changes from one line to many while
  you look at it. Neither backend supports a control changing kind at runtime -
  `AspectContainerModel` never emits `dataChanged` for `KindRole`, so Qt Quick
  would not swap the delegate either - and the expand toggle wants a disclosure
  control. `AspectControls::Toggle` is a dead enum value: Qt Quick maps it to
  `Bool`, the widget renderer has no case, and nothing sets it.
- **`CommandBuilderAspect`** is three ordinary rows - a combo, a path chooser
  and a line edit - so it needs no new control. What it needs is a hook: it
  migrates from a preceding make step *the first time it is drawn*, because
  that is the only moment when "this step was created fresh" is known.
  `BuildStepFactory::restore()` is `create()` plus `fromMap()`, so a fresh step
  is one `fromMap()` never reached, and nothing else can tell. The hook exists
  in spirit - `requestDisplayText()` is what `ActionAspect::setOnShown()` rides
  on - but only two control kinds call it.
- **`DockerDeviceEnvironmentAspect`** wants the environment editor over a
  *fetched* base environment, plus Fetch. The generic
  `EnvironmentChangesAspect` is a summary and a dialog, and
  `runEnvironmentItemsDialog()` takes only the changes - it cannot show what
  they are changes to. Converging would lose that.
- **`LibrarySelectionAspect`** is a summary that swaps in place for a pair of
  combos and Clear All: `TextWithAction` where the action reveals an inline
  editor rather than opening a dialog.

The honest summary is that the porting is finished and what is left is design.

### Being drawn is something every aspect can be told

`CommandBuilderAspect` is three ordinary rows - a combo for the helper, a path
chooser for the command, a line edit for the arguments - so nothing had to be
invented to describe it. What kept it a closure was *when* one thing happens.

A freshly added IncrediBuild step adopts whatever preceding make step it can
build for, disables that step and saves the project. It may only do that for a
step that was **created rather than restored**, and
`BuildStepFactory::restore()` is `create()` followed by `fromMap()` - so a
fresh step is one that `fromMap()` never reached, and nothing can tell at
construction time. The closure knew because it ran when the step was first
drawn, which is late enough for the answer to exist.

That hook was already in the codebase and already named:
`ActionAspect::setOnShown()`, documented as *"called when the action is first
drawn, for one whose label reports state that costs something to find out.
Copilot starts a language server here, which is why it must not happen when
the settings are constructed."* It rides on `BaseAspect::requestDisplayText()`,
a virtual every aspect has - and which **only three controls ever called**:
`Secret`, `TextWithAction` and `Button` in the widget renderer, and only two
delegates in Qt Quick. `TextWithActionDelegate` did not, so an action with a
summary got its `onShown` in widgets and not in Qt Quick.

It is asked in one place per backend now - at the end of
`BaseAspect::addToLayoutImpl()`, and in `AspectItems`' repeater as each
delegate appears - so it means what it says: *you are on screen; if you have to
go and find something out, now is the time.* The five call sites that used to
do it piecemeal are gone.

**What is verified and what is not.** The hook is tested on both sides: an
aspect that counts being told, drawn as a check box, which no control used to
tell. Both controls bite. `CommandBuilderAspect` itself is not tested - it
needs a `BuildStep`, which needs a `BuildStepList` and a `BuildConfiguration`,
and IncrediBuild has no test suite to hang that from. It builds, its three rows
are ordinary aspects, and the one thing that was hard about it now rests on a
mechanism that is tested. That is worth saying plainly rather than leaving to
be inferred.

### A control kind nothing could use

`AspectControls::Toggle` was declared, mapped to `Bool` by the Qt Quick model,
had no case in the widget renderer, and **nothing set it**. An aspect that
asked for it would have drawn a check box in Qt Quick and nothing at all in
widgets - the backend that draws every surface except the settings pages. It is
gone, so asking for it is a compile error rather than a blank row. That is a
better guard than a test: a control kind that only one backend draws cannot be
reached by accident if it does not exist.

**Removing it broke every check box in Creator, and the tests said so
immediately.** The switch read

    case AspectControls::CheckBox:
    case AspectControls::Toggle:                 return Bool;

so `CheckBox` had no `return` of its own - it fell through to `Toggle`'s.
Deleting the `Toggle` label deleted the group's answer, and `CheckBox` fell on
through to the next one instead: every check box became a tri-state. The
compiler had nothing to say, because an empty fallthrough is exactly what that
is for.

Eleven QuickUi tests failed and the census named the aspect and the kind it got
- *"BoolDelegate drew General/ShowShortcutsInContextMenu, which asked for kind
1"* - which is what turned a confusing 96-second run into a one-line fix. Worth
keeping in mind when deleting a `case`: **the label you remove may be carrying
the `return` for the labels above it.**

### The library picker, and a UI change made on purpose

`LibrarySelectionAspect` drew a `QStackedWidget`: a summary and an **Edit** on
one page, a library combo, a version combo and **Clear All** on the other, with
Edit swapping between them in place.

There were two ways to convert it and they are worth writing down, because the
cheap-looking one was the dangerous one.

**Keeping the in-place swap** means five child aspects with visibility toggled -
the shape `TerminalCommandAspect` uses. That needs the aspect to become an
`AspectContainer`, and its value is a `QMap<Library.Id, Version.Id>` held by
`TypedAspect`, which brings `m_value`, `m_volatileValue`, and apply, cancel and
dirty-tracking with them. All of that would have to be written again by hand,
in a plugin with no tests, where getting the dirty tracking subtly wrong breaks
Apply and says nothing.

**Making it a summary and a dialog** keeps `TypedAspect` untouched:
`TextWithAction` needs only `displayText()` and `triggerAction()`. The value
plumbing is not rewritten, it is not even read. The cost is that Edit opens a
dialog instead of swapping in place - and that is what
`EnvironmentChangesAspect`, `MimeTypesAspect`, `EnvVarSeparatorAspect` and
`ClangDiagnosticConfigIdAspect` all already do. It is the house style for a
value edited elsewhere, not a new idea.

So: the dialog. Said plainly because it is a visible change and nothing else in
this migration has changed what a control looks like without saying so.

**Filling the list is what being drawn is for.** The libraries come from the
compiler explorer server, so the closure asked for them when it built the
combo. `requestDisplayText()` - made universal in the last batch - is now that
moment, and `ensureFilled()` hangs off it. The aspect asks for nothing until
something draws it.

**A plugin that had no tests has two.** `displayText()` is a pure function of
the model, so the test hands it libraries it made up rather than talking to
compiler-explorer.com. What it pins is worth having: a version is *stored* by
its id and *shown* by its name, and a version the server no longer offers is
shown as the id rather than dropped - so what is stored stays visible. Both
controls bite, along with one for the fill-on-draw.

### One field or two, and a stretch that turned out to be innocent

`ArgumentsAspect` was the one I had twice written down as needing a mechanism
neither backend has: a control that **changes kind at runtime**, from a line
edit to a multi-line editor and back. That was true of the closure, which built
one editor, and on the expand button deleted it and put the other in its place
with `QLayout::replaceWidget()`.

It is not true of the setting. Both editors can exist and one can be hidden -
`aHiddenAspectIsStillBuilt()` established that a hidden aspect's control is
built and can be shown later, and both backends honour it. So the row is four
aspects: a line edit, a text edit, the button that swaps which is shown, and -
where a run configuration knows what the arguments ought to be - the button
that puts them back. Both editors hold the arguments at all times, so swapping
moves nothing.

The value never moved either: `ArgumentsAspect` was a `BaseAspect` with its own
`QString` and hand-written `fromMap()`/`toMap()`, so becoming an
`AspectContainer` cost nothing. That is the same trade `ExecutableAspect` and
`IosDeviceTypeAspect` made, and the opposite of `LibrarySelectionAspect`, whose
value lives in `TypedAspect` and could not be moved cheaply.

The expand button is an `ActionAspect` with an icon that flips between
`Icons::EXPAND` and `Icons::COLLAPSE`, rather than the `ExpandButton` the
closure built. `AspectControls::Toggle` would have been the honest control for
it - a two-state button - which is exactly the value that was removed for being
drawn by only one backend. An icon that reports the state is what is left, and
it is what the row looked like anyway.

**A stretch I was wrong about.** `setInlineRow()` puts a stretch after the
controls, and an arguments field must fill its row, so I expected the stretch
to squash it and was ready to make it conditional. Ten lines of throwaway code
said otherwise: with the widget resized and the layout activated, the field
takes 640 and the stretch takes 0 - Qt gives an `Expanding` widget the space
before a stretch item. Nothing needed fixing, and the rows converted before
this one - the ABI row, the terminal command, the iOS simulator, the kit rows -
were never squashed either. **Measuring took less time than the argument I was
about to have with myself.**

### A failing test I called a flake for a whole migration

`tst_AspectRenderer::filePathFocusRequest()` has failed on **every run** of
this work. It was written off each time as the known key-window flake, on the
strength of a note saying that focus tests fail when another application is
frontmost.

A flake that fails every time is not a flake. Four lines of `qDebug()` in the
failing test:

    focusWidget: Utils::FancyLineEdit(name="LineEdit")
    app focus:   QWidget(0x0)
    window active: false
    chooser hasFocus: false

The aspect did exactly what it promises - the field is where typing would go.
`QWidget::hasFocus()` is *application-wide*: it asks whether this widget is
`QApplication::focusWidget()`, and no widget is, because the test's window is
not the active one and never will be in a test run with anything else on the
desktop. The assertion could not pass. The code was never wrong.

`QWidget::focusWidget()` is the per-window answer, and it is the one that
matches the promise: *the field is where typing would go in this window.* The
test asks that now, and passes three runs out of three where it had passed
none. The control still bites, and says where the focus went instead:
*"focus went to QLineEdit, not to the path chooser"*.

**The Qt Quick sibling is a real flake and stays as it is.**
`testMultiLineStringGetsATextArea()` waits on `hasActiveFocus()`, which has the
same application-wide problem - but what it goes on to check is that leaving
the editor commits the value, and leaving is an `activeFocus` change. Making
the assertion deterministic would mean making the delegate commit on plain
`focus`, which is not what focus means in a running application. It passes
consistently here and it is left alone.

The lesson is not about focus. It is that **"known flaky" is a claim with a
failure rate attached**, and one that fails 100% of the time is a different
thing wearing the same label. Re-deriving it cost ten minutes at the end; it
could have cost ten minutes at the start.

### The last two, and a grep that had been lying

**`DockerDeviceEnvironmentAspect` is done, and the route was one I had twice
described wrongly.** I had said it needed a new control kind, because
`EnvironmentWidget` lives in ProjectExplorer and the renderer lives in Utils.
Both true, and both beside the point: the aspect does not have to be drawn by
the renderer at all. `EnvironmentChangesAspect` already describes itself as
`TextWithAction`, and the docker plugin already depends on ProjectExplorer - so
Docker inherits the summary and the button, and overrides only
`triggerAction()` to open a dialog holding the `EnvironmentWidget` it always
built. No new control, no change to Utils, and the base environment is still
shown against the changes, which was the whole objection to the plain changes
dialog.

The environment table moves from the device page into that dialog. That is the
sixth aspect to make the same move, and it is what "a value edited elsewhere"
looks like here.

**`WebBrowserSelectionAspect` was the last one, and I did not know it existed.**
Every count in this document, for the whole migration, came from

    grep -rn "void .*::addToLayoutImpl" src/plugins --include=*.cpp

which matches an out-of-line definition and **silently misses a body written
inside the class**. WebAssembly's browser picker was one, so every "N closures
left" reported here was one short. It was also already three quarters converted
- `presentation()` said `ComboBox` and listed the browsers - and needed only
`valueIsChoiceId` and the two variant accessors, which is the shape
`comboBoxForAnAspectValuedByChoiceId()` has covered generically for months.

The count to trust is

    grep -rn "addToLayoutImpl.*override" src tests --include=*.cpp --include=*.h

and it now returns nothing. **No aspect in Qt Creator builds its own widgets.**

**What the miss is worth remembering for.** The grep was written once, early,
and every subsequent count trusted it without re-deriving what it could not
see. It was checked the moment a number mattered - "zero" is a claim worth
testing where "seven" was not - which is exactly backwards: a measurement is
easiest to check while it is still routine.

### Closing the hatch

`BaseAspect::addToLayoutImpl()` is `final`. Nothing overrode it any more, and
now nothing can: writing one is

    error: declaration of 'addToLayoutImpl' overrides a 'final' function

which is the whole of the ratchet. It is worth having as a compile error rather
than a note in this file, because the failure it prevents is silent - an aspect
that builds its own widgets works perfectly in the widget backend and shows
nothing at all in Qt Quick, and every page that ever did this looked fine to
whoever wrote it.

`final` rather than removing the virtual: a non-virtual method of the same name
in a subclass would compile, never be called, and draw nothing. The point is to
be told.

**What is left, said accurately.** No aspect builds controls. That is not the
same as no widgets:

| escape hatch | uses | what it is for |
|---|---|---|
| `AspectWidgets::setLayouter()` | 29 | a container arranging *its own aspects* in the widget backend - the toolchain grid, a device's tool groups. Legitimate and expected. |
| `setWidgetCreator()` | 18 | a whole panel that is not aspects |
| `setConfigWidgetCreator()` | 11 | the same, per aspect |

The first is part of the design: a container may say how its aspects are
arranged without building any of them. The other two are the surface this
migration has not touched - hand-written panels and dialogs, which is a
different kind of work from anything here. The options pages are done, and the
count that says so is the census, not a grep.

### Verifying the whole branch, not one suite at a time

Every batch here was checked against the suites it plausibly touched. That is
not the same as checking the branch, so this is the whole thing: all 56 plugins
that register tests, run one after another.

**2463 tests pass.** Seven groups fail, and each is accounted for:

| plugin | fails | what it is |
|---|---|---|
| CppEditor | 9 | the documented flake - the same binary has given 3, 4, 9, 11 and 25 across this work |
| QtSupport | 19 | `QtProjectImporterTest`, which needs `KitManager::defaultKit()` to have a Qt; a test run has none. Baselined against unmodified sources. |
| ProjectExplorer | 2 | `RunWorkerConflictTest` and `testSourceToBinaryMapping(qbs)`, both baselined |
| Debugger | 1 | `testStateMachine` opens a `.pro` file and needs a kit; baselined |
| FakeVim | 2 | pre-existing. One compares `/private/var/...` with `/var/...` - macOS canonicalises the path and no code change can affect it. |
| Profiler | 3 | pre-existing: flame graph selection and a colour model |
| QmlPreview | 1 | pre-existing, and conclusively so - **this session touched no file in that plugin** |

The last three had never been baselined, and "it looks unrelated" is not
evidence. What settles it is `git diff --name-only <session start>..HEAD --
src/plugins/qmlpreview`, which is empty while its test fails, and the same for
every failing test *file*: none was touched.

**One thing this turned up that no per-suite run could.** `libQmlDesigner.dylib`
in the app bundle fails to load:

    Symbol not found: Utils::AspectContainer::setLayouter(...)

That method became the free function `AspectWidgets::setLayouter()`, and
QmlDesigner's source already calls the new one. The dylib is dated **16 July**
against a libUtils from **26 August**: it is a leftover from a different build
configuration, not produced here and not rebuilt here. The half of QmlDesigner
that uses aspects - `QmlDesignerSettings`, which owns `designersettings.cpp` -
*is* built by this configuration and compiles clean.

It is worth writing down because the command every batch used says
`-noload QmlDesigner`, so a plugin failing to load was invisible for the whole
migration. No plugin anywhere in `src` uses an API this work removed; that was
checked by grep rather than assumed.

### How far the silent-typo hazard actually goes

The Code Style panel showed that a misspelled aspect name can leave a test
green, so the obvious next question is how many other places have it. There are
54 call sites that pass `aspects.X` straight into an `AspectModels` function,
across 41 files - which looks alarming and is not.

Measured rather than assumed, by misspelling one and running the census:

- `readonly property var settings: AspectModels.named(aspects.Settings)` then
  `root.settings.Foo` - **caught**. `named(undefined)` returns null and reading
  a property off null is a `TypeError`, which both censuses collect as a QML
  warning. Tried on `ProjectCommentsPanel.qml`.
- `model: AspectModels.container(root.aspects.Plugins)` followed by
  `visible: root.aspects.Plugins.visible` - **caught**, for the same reason: the
  second line reads through it. Tried on `PyLSSettingsPage.qml`.
- The delegates in `qtcquick` derive their models from a typed `aspect`
  property, so a wrong name there never gets that far.

So the silent shape is narrow: the result has to feed a *view's model* and be
read nowhere else, because a `Repeater` with a null model builds nothing and
therefore reads nothing. Exactly one site was like that - the Code Style
panel's - and it is typed now. Nothing else to change; recorded so the count of
54 does not send someone else looking.

**A false positive in the report, and a fix that was worse.** The report says
"Code Style does not draw: /Preview" for each language, and that is wrong: the
preview is drawn. The census knows an aspect was drawn by finding an item with
an `aspect` property, and `CodeStylePreview.qml` binds the aspect's properties
onto a `TextArea` directly, so there is nothing for it to find.

Giving the component `readonly property Aspect aspect: root.aspects.Preview`
removes two of the three lines and produces a binding loop on `aspect` -
reported against the pages that instantiate it, three tests failing and 36
warnings. Reverted. The cause was not obvious and the report entry is cosmetic,
so it stays as a known false positive: three lines that mean nothing, which is
worth knowing before trusting a fourth.

### Code Style: the last panel, and a control that did not bite

Seventeen of seventeen. No project panel builds a widget any more.

This one was a `QStackedWidget` of per-language editors, filled through a seam
that handed over widgets. It hands over containers now - `createProjectAspects()`
beside the `createSettingsAspects()` the page already had - and the default is
the selector above a live preview, which is what the widget showed.

Getting there needed the selector to stop being the page's. `setupSelectorAspects()`
built the Style combo, the four buttons and the read-only note against
`m_pageCodeStyle`, so only a page could have them; they are
`CodeStyleSelectorAspects` now, told which `ICodeStylePreferences` they are for.
The page passes its page-local copy and gets exactly what it had - checked by
the census report, which still names only the three pre-existing `/Preview`
lines.

The page names no language. It repeats the form over the container the factories
filled and shows the one that is current, so there is no list here to fall out
of date with the factories.

**The control did not bite, and that was the finding.** Misspelling
`aspects.Forms` left the test green: `undefined` reaches
`AspectModels.container()` as a null model, a `Repeater` with a null model
builds nothing, and nothing is assigned to anything, so QML says not one word.
An empty panel and a passing test.

The fix is to make the name land in a *typed* property:

    readonly property Aspect forms: aspects.Forms

Assigning `undefined` to an `Aspect` fails loudly; assigning it to a `var`, or
passing it to a function, does not. That is worth knowing wherever a page
reaches an aspect for anything other than a delegate's `aspect` property - the
delegates are typed already, which is why every other control here bit first
time.

**ClangFormat's project block is not ported.** Its selector overrides import
and export to understand a `.clang-format` file and carries a read-only state
of its own, and none of it can be compiled or run in a configuration without
clang-format - only syntax-checked. Rather than guess at a rewrite that cannot
be tested, C++ gets the default form on that panel and the ClangFormat page
keeps all of it. Written down as a gap, not as a finish.

### Language Server: a panel that cannot know what it shows

Sixteen of seventeen. Three of the four things on this panel are ordinary - a
link to the global page, a combo per language server that needs a project, and
the workspace JSON. The fourth is not: any client may add settings of its own,
so the page cannot name them.

The extension point was `attachProjectSpecificSettingsToLayout(Project *,
QLayout *)` - a client drew into the panel. It hands over a container now:

    virtual Utils::AspectContainer *projectSpecificSettings(Project *)

which is the change made everywhere else on this branch, arriving at the one
place where it is not a convenience but the only way the panel can work at all.
`QmllsClientSettings` is the sole implementer, and it was already building an
`AspectContainer` to draw from - it returns it, with the group's title as its
`labelText`.

The panel puts what it is given into one container with `setFlattened(true)`
and draws that with `FlattenedGroupDelegate`: each contributed container keeps
its own title, and there is no box around the lot. So the page lists three
aspects by name and one from the model, which is what "does not know what it
shows" looks like in practice.

The JSON keeps its highlighting without anything new: `SnippetEditor` is
already "a string aspect edited as code", and a workspace configuration is
that. It is imported across modules the way the Editor panel imports the
TextEditor forms.

### Project Environment: two surfaces, and a dialog that stays a dialog

Fifteen of seventeen. This was the largest panel and the first done under a
stated rule: the Qt Quick UI must match the widget one in functionality. So
nothing was left out.

The widget showed *one thing in two surfaces* - a tree of the resulting
environment and a text editor of the changes, kept in step - with eight buttons
beside them. That second surface is easy to miss; it is one
`horizontalLayout->addWidget(&d->m_editor)` in the middle of the constructor,
and a port that only saw the tree would have quietly dropped the way most
people edit an environment.

Both are aspects now: a table over the same `Utils::EnvironmentModel`, and a
multi-line `StringAspect` whose text is
`EnvironmentItem::toStringList(changes.itemsFromUser())`. A `Guard` keeps a
write to either from coming back round as a change to the other.

**A dialog is a window of its own, whichever backend opens it.** Path-list
variables are edited in `PathListDialog`, and there is no reason for that to
become QML - it is not part of the page. It only needed a seam to be reachable
from something that is not `EnvironmentWidget`:

    PROJECTEXPLORER_EXPORT bool editEnvironmentPathList(
        Utils::EnvironmentModel *model, const QModelIndex &index, QWidget *parent);

That is worth remembering for the two panels left, which are both blocked on
embedded editors: the question is always whether the thing is *part of the
page* or a window the page opens.

**The first behavioural test of a converted panel.** The census says a panel
renders and that its names resolve; it says nothing about whether it works.
This one drives the panel through `ProjectPanelFactory::aspects()` - typing a
change reaches the table and the project, and editing the table rewrites the
text - and removing either direction fails it. The seam added for the census
turns out to be what makes a panel testable at all.

### What a build without Qt::Quick shows now

The previous section turns on a configuration worth stating outright:
`find_package(Qt6 OPTIONAL_COMPONENTS Quick QuickWidgets QuickControls2 ...)`.
Quick is optional, `QtcQuick` and `QuickUi` are built only where it is present,
and without them nothing installs the aspect-form factory. So that build exists
and it is what the layouters are for.

It has a consequence for the panels, and it is a change rather than an
accident. A settings **page** that was converted kept its layouter, so both
backends still draw it as designed. A **panel** did not have a layouter to
keep - it had a hand-written `QWidget`, which the conversion replaced. So in a
Quick-less build the fourteen converted panels fall back to
`AspectWidgets::layouter()`'s generic `Column` of their aspects.

For most of them that is what they already were: the widget form of the
"flag plus settings" family was literally `Column { flag, hr, *settings }`, and
the fallback is the same list without the rule. Where it differs is the panels
with real structure - Clang Tools, Testing, GitLab, the Editor's margin row -
which come out stacked rather than arranged.

Writing widget layouters for those would restore them and would also be ten
files of layout code duplicating the QML beside it, which is the thing this
work removes. The instruction for this branch is to replace widgets rather than
keep them, so they are not being written. Recorded here so that the choice is
visible instead of being discovered.

### A layouter is not dead because nothing puts it in a Column

Six layouters were deleted across three batches - the four TextEditor settings
objects, Clangd's project settings, and the build-and-run ones - on the
strength of a grep that found nothing adding those containers to a
`Layouting::Column`. All six went back.

The grep asked the wrong question. A container's layouter is also reached
*generically*: a build without Qt::Quick has no `s_aspectFormFactory`, so
`Core::createAspectForm()` falls back to `AspectWidgets::layouter()` on the
page or panel container; that container's own fallback is a `Column` of its
aspects; and the renderer draws each aspect that is a container with **its**
layouter. Nothing in that chain is written down at any call site, so no grep
for `Column {` will find it.

What it cost was visible once looked for. The group titles - "Tabs And
Indentation", "Typing", "Cleanups Upon Saving", "File Encodings" - lived inside
those layouters rather than in the containers' `labelText`, so without them the
widget backend drew no group box at all. The build-and-run one was worse: its
fallback is a `Column` of *every* build-and-run setting, where the layouter
lists the nine a project may set.

The tell was there the whole time: every other page on this branch keeps its
layouter beside its `qmlSource`, which is why 21 of them remained after 107
pages were converted. Six deletions against that pattern were inconsistent, not
principled. The count is 27.

**What this means for the rest.** Deleting a layouter is only right when the
widget backend is going away, and it is not - `tst_utils_aspectrenderer` is 54
tests that say so. Adding QML beside a layouter is the whole job; removing the
layouter is a separate decision that belongs to whoever retires the widget
backend.

### Building and Running, and what a shared widget was hiding

Fourteen of seventeen, and 22 layouters down to 21.

This panel did not build a widget of its own: it asked
`GlobalOrProjectAspect`'s shared one, which draws a check box, a Restore Global
button, and `AspectWidgets::layouter(aspect->projectSettings())()` for whatever
is underneath. That looked like infrastructure worth porting - a new control
kind and a delegate, benefiting every user.

It was not, and the reason is worth writing down. The three things that widget
draws are the flag, the restore action and the settings, which is *exactly* the
shape every panel in this batch already has. Copilot, Documentation Comments,
Vcpkg, To-Do, Clangd, Testing, GitLab and the Editor all assemble those three
by hand because each one wires them slightly differently. So the panel became
one more of those, and needed no new machinery at all - only
`UseGlobalAspect`, which already knows how to make "global settings" a link to
the page they come from.

**The layouter was where a decision lived.** The settings container holds every
build-and-run setting there is - the global page shows all of them - and the
panel showed nine. Which nine was written down only in the layouter, so it had
to be read before the form could list it. It was deleted here and put back
later; see below.

`GlobalOrProjectAspect`'s widget stays, because run configurations still use
it; that is the surface it was written for, and it is not a settings panel.

### Dependencies, and icons in a table

Thirteen of seventeen. The panel itself was small - a `QTreeView` over a model
that already did all the work, and two check boxes - but converting it needed
something the Quick table did not have.

**A table could not show an icon.** `Qt::DecorationRole` gives a `QIcon`, which
is exactly what a widget view wants and exactly what QML cannot carry;
`AspectTableCell` did not read the role at all, so a straight conversion would
have quietly dropped the project icons. The list delegate had solved this years
ago with `QtcQuick::iconUrl()` and an image provider, but that lives in
QtcQuick and `DependenciesModel` lives in ProjectExplorer, which does not link
it.

So the cell asks: `AspectModels.decorationUrl(model.decoration)`. One helper,
no proxy, no new role for a model to implement, and no plugin has to link
QtcQuick to have icons in a table. Every existing table gets them for free the
moment its model answers the role.

**A trap on the way.** Calling it `iconUrl()` compiled the header and broke
`AspectModels::presentation()` three functions away: a member of that name
shadows the free `QtcQuick::iconUrl()`, so a lambda that had been calling the
free function for years suddenly needed `this`. Renaming to `decorationUrl()`
is clearer anyway - it takes what the role holds, not an icon.

The model needed nothing but `roleNames()` and the two flag-derived roles. It
already answers the check state from `ProjectManager::hasDependency()` and
writes it back through `addDependency()`, circular-dependency warning included.

The `DetailsWidget` around it is gone; it was in `NoSummary` mode, so it was a
frame and nothing else.

### The Editor panel, and the four layouters it was keeping alive

Twelve of seventeen, and 26 layouters down to 22. This was the panel worth
doing: `TabSettings`, `TypingSettings`, `StorageSettings` and
`ExtraEncodingSettings` each had a `setLayouter()`, and the Editor project panel
was the only thing still drawing them as widgets.

What made it tractable is that the panel shows *the same five settings objects
the Text Editor Behavior page shows*. So the page's five group boxes became
five forms - `TabSettingsForm`, `TypingSettingsForm`, `StorageSettingsForm`,
`EncodingSettingsForm`, `BehaviorSettingsForm` - and the page and the panel are
each a list of five lines around them. The panel adds the margin settings, the
flag and Restore Global.

**A form can cross a module.** The forms live in `QtCreator.TextEditor` and the
panel is in `QtCreator.ProjectExplorer`, so its QML does
`import QtCreator.TextEditor`. That works because ProjectExplorer already
plugin-depends on TextEditor, and it is worth knowing: a panel does not have to
live in the same plugin as the settings it shows.

**They were deleted here and put back.** The check was "does anything add one
of these to a `Column`", and nothing does - but that is not how they are
reached. A build without Qt::Quick has no `s_aspectFormFactory`, so
`Core::createAspectForm()` falls back to `AspectWidgets::layouter()` on the
page's container, whose fallback is a `Column` of its aspects, each of which is
a container the renderer draws with *its* layouter. That is the path, and it
does not appear in any grep for `Column {`.

What it cost was visible: the group titles - "Tabs And Indentation" and the
rest - lived inside those layouters, not in the containers' `labelText`, so
without them the widget backend drew no group box at all. Every other page on
this branch keeps its layouter beside its `qmlSource` for exactly this reason;
these four were the only ones deleted, and that was inconsistent rather than
principled. The count is 25, not 21.

Two things stayed where they were, because moving them would have been wrong:

- The tab settings are the panel's own, not the configuration's. They follow
  whichever code style the project uses, which is what `setPreferences()` wires
  up, and Restore Global has to call it again after cloning.
- The flag disables each of the six containers rather than the container holding
  them, because the flag lives beside them - the same shape as Testing.

### GitLab, and the last of the "flag plus settings" family

Eleven of seventeen. This panel had no settings container at all -
`GitLabProjectSettings` is a plain `QObject` keeping whether the project is
linked and to what - so everything on it was state the widget held: two combo
boxes, three buttons whose enabled states were recomputed in one place only the
widget could reach, and an `InfoLabel`.

The two combos carry their choice as item data, which is what the combo boxes
did with a `QVariant`, so `checkConnection()` asks
`m_linkedServer.itemValue()` rather than `currentData()`. `SelectionAspect`
already had everything needed for that - `addOption(Option)` with item data,
`clearOptions()`, `indexForItemValue()` and `optionCount()` - which is what
made this a translation rather than a redesign. `InfoLabelType` and
`Utils::InfoType` have the same six values, so the message is a `TextDisplay`
with no mapping to write.

`updateEnabledStates()` is unchanged in what it decides. It sets it on aspects
instead of widgets, so the rule that a project without a git repository can do
nothing at all is said once and both backends read it.

**What is not covered.** The census checks that the panel renders, that every
name resolves and that not everything is disabled - misspelling
`CheckConnection` fails it - but nothing tests the enable rules themselves.
They depend on a git repository, a configured server and the linked state, and
there is no harness here that can arrange those. That is worth saying plainly
rather than leaving the passing census to imply more than it checks.

### Testing: two tree widgets over aspects that already existed

Ten of seventeen. This panel had two `QTreeWidget`s, and neither held anything
`TestProjectSettings` did not already have:

- The path patterns were a `StringListAspect` the whole time. The tree drew
  them, Add appended `"*"` and started an edit, Remove deleted the item, and
  the value was written back by *reading the items out of the tree again*. The
  aspect has an editor of its own; it just had no label and none of the
  `setUiAllow*` flags set, so nobody had used it.
- The active frameworks and tools are two `TypedAspect<QHash<...>>`. They are a
  table aspect now, which writes through to `activateFramework()` instead of
  going through a tree item's check state.

**Not every flag disables its whole container.** The widget form had a comment
- "explicitly outside of the global settings" - on the path patterns group,
because it is the project's whatever the global settings say. So the panel
disables the two aspects that *are* global, rather than the container. The
census's "one enabled aspect" invariant would not have caught getting this
wrong; reading the comment did.

The three-second timer that rebuilds the test tree after ticking is behaviour
and moved to the constructor with the rest.

**The census caught a real mistake, not a planted one.** The QML module is
`QtCreator.AutoTest` and the panel asked for `QtCreator/Autotest`, which is a
resource path that does not exist. Nothing about that is a compile error; the
panel would simply have been empty. The test failed with
`status` 3 where 1 was expected - `QQuickWidget::Error` - which is exactly the
assertion added because `source` alone would have matched.

### Clang Tools: the first panel that needed an aspect written

Nine of seventeen now, and this is the one that stopped being a conversion and
became a port. The suppressed diagnostics were a `QTreeView` over a model, with
Remove Selected and Remove All kept in step by a selection model - state that
lives in a view and that nothing else can see, which is workstream 2's shape.

They are a table aspect now. It owns the model, and *which row is current is
the aspect's answer*, so the two buttons are enabled from the container:

    onCurrentRowChanged: root.aspects.SuppressedDiagnostics.setCurrentRow(currentRow)

**The model needed `roleNames()`, and the reason is a trap.** `AspectTableCell`
reads a cell with `model.editable ?? cell.editableByDefault`, and the default is
*true*. A model that does not name its roles has no `editable` for QML to find,
so its rows offer themselves for editing - which is why `ClangdSessionsModel`
works without one, and why this one cannot: a suppressed diagnostic is removed,
never edited. `Utils::AspectTable::withRoleNames()` plus an `EditableRole`
answer of `false` settles it. Its overrides also had to become public: they were
private, which is legal for an override of a public virtual and means whatever
holds the model cannot ask it how many rows there are.

The rest is the usual split. The two "Go to" labels are one `TextDisplay` with
two links - the same shape as Clangd's note about configuration files - and
Restore Global is an `ActionAspect`. What enables them, and what the flag
disables, moved to the constructor. The run options are the same list the global
page shows, so they moved into a form both use.

**Controlled by typo.** Misspelling `SuppressedDiagnostics` in the panel's QML
fails the census with
`ClangToolsProjectPanel.qml:41:17: Unable to assign [undefined] to
Utils::BaseAspect*`, which is what says the panel is really being exercised
rather than merely built.

### Clangd: the widget layout answered the question

This panel was left waiting on "which of the page's aspects should a project
show", and the answer was already written down in the code it was replacing.
`clangdSettingsLayout()` lists the settings and nothing else, so the sessions
table and the note about configuration files belong to the global page alone.
The panel is the shared form, the flag, and the version warning.

The warning is the interesting part. Whether the clangd that was named can be
used is found out by running it, so it is not one of the settings - the closure
built an `InfoLabel` of its own for it, and `ClangdPageAspects` had already
turned that into a `TextDisplay` it owns. A project needs its own, driven by
its own path, so `ClangdSettingsForm` takes it as a property beside the
settings:

    required property var settings
    required property var versionWarning

`ClangdProjectSettings`'s `setLayouter()` was deleted here and put back later;
see "A layouter is not dead because nothing puts it in a Column" below.

**A limit of the panel census, stated rather than fixed.** It checks that a
panel renders through its QML, that the component reached `Ready`, that QML
made no complaint, and that not everything is disabled. It does *not* check
that every aspect got drawn, because that needs the QML item tree, and a
`QQuickWidget`'s tree is not reachable from the widget - `findChildren` returns
one `QQmlComponent`. The pages census does have that walk, so a panel built
from a shared form is covered through the page that shares it; what is not
covered is a panel-only aspect, of which there are currently three - the flag,
the Quick Fixes button and this version warning. Moving the panel census into
QuickUi would fix it and would cost ProjectExplorer as a plugin dependency
there; not worth it for three aspects, but worth knowing before the count
grows.

### The Quick Fixes panel, and how a form extraction is checked

Quick Fixes was already half-way: the panel embedded the settings with
`Core::createAspectForm()` and kept a widget around them for the flag and one
button. The button is the part the closure *did* rather than listed - it
deletes the custom settings file while the global settings are in use and
resets to them when they are not, saying something different in each case, and
appearing at all only when there is a file. That is an `ActionAspect` told its
text, its tooltip and its visibility from the constructor.

One trap in registering an existing container into a panel: `insertAspect()`
does `aspect->setAutoApply(isAutoApply())`, and `CppQuickFixSettingsAspects`
sets `setAutoApply(false)` for itself because it saves through
`volatileValueChanged` rather than on apply. Registering silently turns that
on. It is put back on the line after, with the reason.

**How the 160-line extraction was checked, and what that revealed.** The
obvious control - delete one delegate from the extracted form and expect a red
test - came back green. The page census walks the container, nested containers
included, and collects what no delegate drew, but it *reports* that rather than
asserting it:

    QuickUi ... Quick Fixes does not draw: /ReturnByConstRef

which is deliberate. A page may legitimately leave out a setting that is stored
but edited elsewhere - GDB's throw and catch breakpoints live in the
Breakpoints view, Valgrind's cycle detection in the Callgrind toolbar - and
each of those lines was checked by hand against the closure it replaced. So the
control does bite; it bites in the report. Reading it is the check, and after
the three extractions here it names none of the pages they touched.

Worth knowing before extracting the next one: a green suite does not mean the
form kept every control. The report does.

### A panel and its page are the same list

C++ Code Model and C++ File Naming are the same `Column { flag, hr, *ps }` as
the three before them, with one difference that matters: what they show is
*exactly* what their global page shows. Writing the panel's QML by hand would
have meant a second copy of the same delegates, and two copies of a list drift.

So the page's body moved into a form of its own -
`CppCodeModelSettingsForm.qml`, `CppFileSettingsForm.qml` - with a
`required property var aspects` for whichever container it is being shown for.
The page and the panel are each a few lines around it:

    AspectPage {
        id: root
        CppFileSettingsForm { aspects: root.aspects }
    }

    AspectPage {
        id: root
        readonly property var settings: AspectModels.named(aspects.Settings)
        BoolWithOwnLabelDelegate { aspect: aspects.UseGlobalSettings }
        CppFileSettingsForm { aspects: root.settings }
    }

The obvious alternative - instantiating the *page* inside the panel - does not
work: `AspectPage` is a `ScrollView`, and one inside another's column nests
scrolling and loses the implicit size, which is the same trap `SnippetEditor`
records. A form is a plain layout, so it composes.

That takes the census to six of seventeen. It is the pattern for most of what
is left, because a project panel that mirrors a global page is the common case.

**Where it stops being mechanical.** Clangd is the next one and is not: a
single `clangdSettingsLayout()` serves both the global settings and a
project's, while the Quick page draws a nested `Settings` container *plus*
page-only aspects - the version warning and the table of sessions with one
clangd instance. Which of those a project should show is a question about the
feature, not about layout. Testing and Clang Tools build real widgets - link
labels, Add/Remove buttons, a frameworks tree - so they are ports rather than
conversions.

### Three more panels, and where the flag has to live

The blocker for the panels after Copilot was that every other project settings
container keeps its `useGlobalSettings` outside itself. The comments in the
code say "excluded from toMap/fromMap", and that part turns out not to be the
reason: `saveToMap()` returns early on an empty key and `fromMap()` on
`skipSave()`, so a keyless aspect inside a container is *already* invisible to
persistence, and only Copilot's flag has a key at all.

The real reason is `setupUseGlobalSettings()`:

    container->setEnabled(!useGlobal->value());

A flag inside that container would be disabled along with everything else the
moment it was switched on, and there would be no way back. So the panel is a
container of its own holding the flag and the settings container, with no
settings key of its own. Order matters when building one: `insertAspect()`
does `aspect->setAutoApply(isAutoApply())`, so the panel sets its own
auto-apply *before* registering anything, or it forces the wrong one onto the
container it takes in.

Documentation Comments, Vcpkg and To-Do went over this way; the census now
reads four of seventeen. Vcpkg disables only its root path rather than the
whole container, so its flag could have gone inside - it did not, because
knowing that means reading what each container's `setEnabled()` reaches, and
one pattern the census checks is worth more than the object it saves. To-Do
was the first whose closure *did* something as well as list: it kept a
`QGroupBox` in step with the flag by hand, because a group box is not one of
the aspects. In QML the box follows the container it holds.

**Two checks came out of it, both controlled by making the mistake.**

- *A panel always has one enabled aspect.* Putting the flag inside the
  container it disables fails with "Documentation Comments is disabled as a
  whole, so there is no way back".
- *No QML complaints while the panels are built.* A name no aspect answers to
  is undefined in QML rather than an error, so the delegate is built and draws
  nothing; `qmllint` cannot see through a `QQmlPropertyMap`, and the only place
  it shows up is a runtime warning. Misspelling `ExcludePatterns` now fails
  with `TodoProjectPanel.qml:26:36: Unable to assign [undefined] to
  Utils::BaseAspect*`.

The obvious version of the second check does not work: walking the panel
widget's children for delegates finds nothing at all. A `QQuickWidget`'s item
tree hangs off an internal window rather than off the widget, so
`findChildren<QObject *>()` returns one `QQmlComponent` and stops. Collecting
the warnings needs no Quick headers and catches binding errors too.

### Panels can say what they show now

`ProjectPanelFactory` could only hand over a `QWidget`, so what a panel shows
was reachable only by building it - which is why there has never been a panel
census the way there is a page census. It now has the seam `IOptionsPage` has
always had:

    using SettingsProvider = std::function<Utils::AspectContainer *(Project *)>;
    void setSettingsProvider(const SettingsProvider &provider);
    std::optional<Utils::AspectContainer *> aspects(Project *project) const;

`createWidget()` prefers the widget creator, then the provider, and builds the
container's form with `Core::createAspectForm()` - Qt Quick where the container
names QML, the widget layout otherwise. It sets the window title from the
display name, which is what a hand-built panel did for its tab.

**Copilot is the first panel through it.** Its widget was a `Column` of two
aspects and nothing else; every decision it makes was already in
`CopilotProjectSettings`, which is an `AspectContainer`.

**The census reports the backlog.** One panel says what it shows; sixteen still
build a widget: Building and Running, Editor, Language Server, Code Style,
Documentation Comments, Dependencies, Project Environment, C++ File Naming,
C++ Code Model, Clangd, Quick Fixes, Clang Tools, To-Do, Vcpkg, Testing,
GitLab. It asks for `QQuickWidget`'s `status` as well as its `source`, and that
is the assertion that matters: a component that fails to load leaves the widget
in place and empty, so pointing the panel at a file that does not exist keeps
`source` right and only `status` notices. Found by class name rather than by
type, so ProjectExplorer needs no QuickWidgets dependency.

**What the next panel runs into.** Only Copilot registers its
`useGlobalSettings` in its container. Every other one holds it deliberately
outside - `// not {this}: excluded from toMap/fromMap` - because the container's
`toMap()` is the project's stored settings and the "use global" flag is written
beside them under its own key. So a panel's QML cannot reach it by name, and
registering it would change what is persisted. The way through is a wrapper
container with no settings key of its own, holding the flag and the real
container; that is a decision about persistence rather than about layout, which
is why it is written down here rather than guessed at.

### What is left is hosts, not pages - and one more divergence

Twenty-eight `setLayouter` calls remain, and none of them is a settings page.
Sorting them by *what would have to change* rather than by plugin:

- **A generic form.** `IDevice` sets three, `ToolchainKitAspect` one, all of
  the shape "every aspect of this container, one per row". The Quick side
  already draws exactly that - `createGenericAspectForm()` - but
  `createAspectForm()` returns nothing without a `qmlSource`, so the widget
  layouter is what gets used. The fallback in `AspectWidgets::layouter()` is a
  `Column`, not a `Form`, so these are not redundant and cannot just be
  deleted.
- **A host that is not a Quick host.** The four profiler samplers are drawn by
  the standalone `qtprofiler` tool, which links Widgets and `Tracing` and no
  Quick at all; `TabSettings` and its three neighbours are drawn by the Editor
  *project panel*. Both need the host ported first.
- **Deliberate.** `lua/bindings/settings.cpp` exposes `setLayouter` as Lua API.

Worth recording that the visible settings really are Quick, checked from the
host end rather than the aspect end: the Devices page has a `qmlSource`, and
what it shows for a device is a `ContainerAspect` that hands the nested
container to QML. So those `IDevice` layouters are the widget path only.

**The one real bug in the pile was Docker's port mappings.** `PortMapping`'s
layouter was `Row{ip, hostPort, containerPort, protocol}`, and an `AspectList`
in its inline style draws each item from the item's *model*, which did not
carry how the item reads. So the widget form drew a row and the Quick form -
the one users see - stacked four fields with a Remove button beside the pile.
`AspectItems` has had an `inRow` property since it was split out of
`AspectForm`; nothing was passing it. The model answers `inlineRow` now, and
`setInlineRow(true)` replaces the closure. The widget renderer needed no
change: its `pres.inlineRow && !hasLayouter()` branch already handled it.

The test for it went red first, and for the wrong reason - four line edits
where two were expected. An inline list replaces its whole layout on
`volatileItemListChanged` and the widgets it replaces are deleted later, so
every field is found twice. The positions were right all along: same `y`,
different `x`. Assert distinct positions, not a count. Both tests check the
row *and* the stacked case, so neither passes if the flag stops being read.

### The last code style page, and a plugin nobody had compiled

Three of the four code style factories set a `qmlSource()` - C++, Nim and
QmlJS. The fourth is ClangFormat, and it is not a QML-writing job: its editor
is hand-built widgets holding their own state, which is the shape workstream 2
names. `ClangFormatGlobalConfigWidget` kept eleven raw widget pointers and
answered `mode()` and `useCustomSettings()` by reading check boxes, while the
values it mirrors already live in `ClangFormatSettings`, an `AspectContainer`.

So it became one: `ClangFormatGlobalConfig`, nine aspects and a
`ClangFormatGlobalConfig.qml`. What it *lists* is the same for the global page
and a project's; what differs is what a project may change, and that is the
container's decision - `useGlobalSettings` is visible only with a project, the
three global-only settings only without one. Both hosts embed it with
`Core::createAspectForm()`, the seam `cppquickfixsettings.cpp` already uses to
put a Quick form inside a widget layout, so no new dependency was needed. The
six `installMarkSettingsDirtyTrigger()` calls survive unchanged: that helper
already accepts a `BaseAspect` and connects `volatileValueChanged`.

**The plugin does not build here, and that turned out to be the finding.**
ClangFormat needs clang-format, and `ninja` has no target for it in this
configuration - so nothing in it has been compiled for the whole branch, and
it had been broken twice over without a sound:

- `FileUtils::getOpenFilePath()` and `getSaveFilePath()` moved to
  `filedialogs.h`, and the call site still included only `fileutils.h`.
- `aspects.h` stopped including `<QComboBox>`, and the config widget had been
  getting the type from there - twelve errors on an incomplete type.

Both are one-line includes, committed separately with `Amends` footers, before
the port that deletes one of the two files.

The way to compile a disabled plugin is the compile database: take a sibling
plugin's entry from `compile_commands.json`, drop `-c`/`-o`, add the source and
`-fsyntax-only`, and point `-I` at `/opt/homebrew/opt/llvm/include`. It found a
real error in the new code on the first run - a `for (BaseAspect *aspect : {...})`
whose initializer list would not deduce - which is the negative control: the
check compiles enough to be worth trusting. `qmllint` takes the same treatment,
run directly against the build tree's `qml_modules` since there is no
`ClangFormat_qmllint` target either.

**What this does not have is a runtime.** Every other batch on this branch was
checked by running the thing. This one compiles and lints and cannot be
launched here, so the page wants opening once in a build that has clang-format
before it is trusted.

**How many other plugins are in that position:** five. Comparing
`ninja -t targets all` with `src/plugins/*/`, and confirming against
`compile_commands.json` that the directory has `.cpp` files and no entries,
gives appstatisticsmonitor, clangformat, effectcomposer, multipropertyeditor
and serialterminal. Only ClangFormat could be checked - the others want
QtCharts, QtSerialPort or QmlDesigner's own include set, none of which a
sibling's flags supply, so their "errors" are all `file not found` and mean
nothing. What can be said about those four is by grep, not by compiler: none
of them names an API this branch moved, and none uses a widget type that
`aspects.h` used to supply. That is weaker than a build and is worth redoing
in a configuration that has those dependencies.

### Typing the delegates that drive one aspect

`aspectcontainermodel.h` registers `Utils::BaseAspect` as the QML type
`Aspect`, under a comment saying that this is what lets a delegate "have
qmllint check every binding against the real properties". Only the base class
was ever registered, so the delegates that drive exactly one aspect kind -
`GroupedListDelegate`, `AspectListDelegate`, `ButtonDelegate` - held an
`Aspect` and called methods only the subclass has. Twenty such calls, each
reported as `Member "moveCurrentUp" not found on type "Utils::BaseAspect"`
and no more than that.

Each of the three has exactly one producing class: control `GroupedList`
comes only from `groupedlistaspect.cpp`, `AspectList`/`AspectInlineList` only
from `aspectlist.cpp`, `Button` only from `ActionAspect::presentation()`.
Registering those three and declaring them in the delegates removes 18 of the
20 warnings.

The warnings going away is not the proof. A QML type that fails to resolve
*also* stops producing member warnings, and that looks identical from the
outside. The control is to rename `AspectList::moveCurrentUp()` in C++ and
leave the QML calling the old name:

    AspectListDelegate.qml:174: Member "moveCurrentUp" not found on type
    "Utils::AspectList"
    Info: Did you mean "moveCurrentUpZZZ"?

The real type is resolved and the new spelling suggested. Before the change,
that same rename produced a warning indistinguishable from the noise already
sitting there - which is the whole failure mode: it would have shipped a page
whose buttons quietly did nothing.

`KeySequenceDelegate` keeps the generic `Aspect`. Its aspect is declared
inside `shortcutsettings.cpp` in coreplugin, which this library cannot see, so
`recording`/`setRecording` stay unchecked; the delegate says so, to stop the
next person trying to register a plugin-private class.

One more thing fell out of reading stderr instead of the totals. The suite
passed 66/66 while logging ten `QQuickImage: Cannot open:
qrc:/qt/qml/QtCreator/Ui/A server` warnings: a test's `listViewDataCallback`
ignored the role it was handed and answered with the item's display name every
time, so a row's decoration became a label and QML resolved it as a relative
URL. Every production callback switches on the role and returns `{}`
otherwise, so this was test-only. But it was the suite's entire warning
output, and a suite that always prints ten warnings cannot show you an
eleventh.

### What the census could not see, and now can

Two holes, both found the hard way in the same session.

**An aspect no renderer knows was walked straight past.** `kindOf()` answers
`Unsupported` for `AspectControls::Custom`, and the census skipped those the
way it skips `Invisible` ones - so a page could lose a whole control and
nothing said so. That is exactly how `AspectList`'s inline style went
unnoticed: it answered `Custom`, and every page using it drew nothing where the
list should be.

Saying nothing and *meaning* nothing are two different answers now.
`AspectControls::Invisible` is what an aspect that means to show nothing says -
`ContainerAspect`, whose contents the page draws itself - and `Custom` is what
one that has not been described yet says. The census reports the second and
passes over the first. Measured before asserting: across all pages the only
`Unsupported` aspects were the three `ContainerAspect`s, so the assertion is
one the tree can actually hold.

**The delegate-to-kind map had not kept up.** `serves` maps a delegate's name
to the kinds it may draw, so that a page naming a check box for a string aspect
is caught. Six kinds added since it was written - `GroupedList`, `Tree`,
`AspectInlineList`, `TriStateBool`, `KeySequence`, `FontFamily` - were not in
it, and a delegate the map does not know is simply not checked. A new kind
belongs in three places, not two: `kindOf()`, `AspectItems.qml`, and here.

### External Tools, and a tree the user arranges

The last page whose only obstacle was a control. Its tools are dragged between
categories and into whatever order they are to be offered in, so the tree
delegate grew that.

**The move goes through the model, not around it.** `mimeData()` and
`dropMimeData()` are how a `QTreeView` does an internal move, and they are
where a model puts whatever else it needs to know - External Tools carries the
category the tool came from, which no generic `moveRows()` would have. So
`AspectModels.moveRow()` builds the mime data from the source index and hands
it back to the model, and the delegate only decides *where*.

The gesture itself is not tested: it needs a window and a pointer. What it ends
up calling is, and so is a tree that never said its order was the user's not
offering to move anything. That is the same line the line-number drawing sits
on - a screenshot checks the gesture, a test checks the mechanism.

**The fourth page that wrote its form back twice.** Every field writes into the
tool as it changes *and* the form was written back when the selection moved on.
Only the first does anything, and a control aimed at the second stayed green.
By now this is the default assumption; the surprise would be a page where both
halves matter.

But the control that stayed green found a real hole next to it: the environment
is edited in a *dialog*, and the aspect said so with `changed()` - which the
container does not forward. So the one field that did need the write-back on
the way out was the one that never got it. It says `volatileValueChanged()`
from its setter now, which is what every other field says and what the page
listens to.

**Removing something you are standing on.** Remove deleted the tool and then
cleared the selection, and clearing the selection writes the form back into the
tool that is being left - which by then was freed. ASan caught it in the test;
nothing in the UI would have, most of the time. The selection is cleared first.

### The Keyboard page, and the last of CommandMappings

Two controls were missing, and one of them was missing in a way nothing said.

**`AspectList`'s inline style had no Qt Quick counterpart.** Only the
list-with-details one did: `presentation()` left the control as `Custom` for
the inline style, which is `Unsupported`, which the page census *skips*. So an
aspect using it drew nothing and no test minded. It is
`AspectInlineListDelegate` now - a column of items, each in full, with a remove
beside it and an add at the end, which is what the widget renderer builds.

That delegate has the same cycle `AspectListDelegate` has: `AspectItems`
instantiates it and it draws `AspectItems`. Loading by URL is the way out, and
the model has to be an *initial* property - `AspectItems.model` is `required`,
and a Loader that assigns it after creation builds the item without one.

**Recording a key sequence is not a control's job.** What is being recorded is
exactly the keys that would otherwise be shortcuts, and those never reach the
control they were meant for - they are taken with an application event filter.
So `KeySequenceAspect` records, and `KeySequenceDelegate` is a field, a Record
button, and nothing else.

**A row that knows what it is.** The widget page reached into the tree from
outside - `setModified()` on the item, `setForeground()` on the colliding
column. The row answers `FontRole` and `ForegroundRole` from what it holds now,
which is what makes a Qt Quick view and a `QTreeView` show the same thing
without either of them being told.

**A command is found by what it is mapped to.** Typing `Ctrl+K` in the filter
finds the commands using it, which is also how "Show conflicts" works - it
writes the colliding sequence into the field. The row does not *show* the
portable text, so `TreeFilterModel` honours `FilterTextRole` now, the way
`TableFilterModel` already did.

With this page, `Core::CommandMappings` has no callers left: its two others -
FakeVim's User Command Mapping and Ex Command Mapping - went earlier. What the
three shared turned out to be a tree, a filter and some buttons, none of which
needed a shared widget once the tree delegate existed.

### A setting that is neither on nor off

The Python Language Server page is a JSON document and a reading of it:
thirteen check boxes saying what the JSON says about each plugin, which rewrite
it when ticked. A plugin the JSON says *nothing* about is the interesting case
- the server's own default applies, and drawing that as unchecked would tell
the user it is off.

`TriStateAspect` existed for exactly this and was always drawn as three named
options in a combo box, which is fine for one setting and unreadable for
thirteen down a page. `setUseCheckBox()` draws it as one check box instead, in
both renderers. Qt's own tri-state cycling goes through all three states;
"neither" is what the setting says *before* anything has been decided and a
click is a decision, so it is shown but never cycled to - `nextCheckState` in
the delegate, `setTristate(true)` plus a two-state `clicked` handler in the
widget one.

**Adding a member to a widely included aspect needs a full build.** The new
`bool` on `TriStateAspect` changed its size, and a targeted build left
ProjectExplorer constructing the old layout - an ASan global-buffer-overflow in
a constructor a mile from anything that was edited. Build everything after
touching `aspects.h`.

**Where an error goes when there is no line to put it on.** The widget editor
marked a JSON parse error on the line it was on, which needs a `TextMark` and a
`TextEditorWidget`. A settings page has one place to say it, so it says it
there, in a `TextDisplay` with `InfoType::Error`. That is the second consumer
of the Qt Quick editor after Snippets, and the first that is not a snippet.

### The Locator, and a tree that can be written to

`ILocatorFilter::openConfigDialog()` had the Locator page on the blocked list,
which was wrong: a dialog stays a dialog, the way Qt Versions' Link with Qt and
Axivion's server editor do. What actually blocked it was that its filters are a
*tree* whose prefixes are typed in and whose defaults are ticked, and
`TreeDelegate` drew labels.

**The cell moved.** `TableDelegate` already asked the model what each cell was
- a check box, a choice, a field, or text - so that logic is `AspectTableCell`
now and both use it. Which cells may be written to stays the model's answer,
read out of the items' own flags. What a cell that says *nothing* about itself
is is the view's answer, and the two differ: a table's rows are the user's to
edit, a tree reports unless its model says otherwise. Without that distinction
every read-only tree in the tree turned into a page of text fields.

**A latent bug the same work uncovered.** `TreeDelegate.currentIndex` said it
was an index into the model the aspect hands out and gave one into the filter
proxy. `BaseTreeModel::itemForIndex()` refuses an index that is not its own, so
the FakeVim Ex page stopped following the selection as soon as anything was
typed into the filter field. Nothing said so, because the filter and the
selection were only ever tested apart - which is worth remembering when a
delegate grows a second feature.

Two smaller things the port needed:

- **Add was a button with a menu**, and there is no menu button to draw. It is
  the two buttons the menu held; what a new filter is made of is a dialog's
  business either way.
- **A category heading answered its name for every column.** A widget view hid
  that by spanning the first column across the row; a Qt Quick tree draws each
  cell where it is, so the heading appeared three times. The item answers for
  column 0 now.

### Two holes the widget-creator pages walked into

The To-Do page was the first of the widget-creator pages ported after the
aspect-driven backlog emptied, and it found two things missing that every page
after it would have found too.

**A list row could only say a name.** `AspectList`'s widget list view asks its
`listViewDataCallback` for any role a `QListView` wants, so a list whose items
are told apart by how they look - To-Do's keywords, and the kits, devices and
MIME types still to come - already had the data. Only the Quick model was
narrower. It now answers `Qt::DecorationRole` and `Qt::ForegroundRole` as well.

QML has no `QIcon`, and the icons a model row shows are built rather than
looked up - two masks tinted with two theme colours, in To-Do's case - so
serving them by resource path was not enough. `QtcQuick::iconUrl()` registers
an icon with the image provider and hands back the URL an `Image` loads it
from. What accumulates is one entry per distinct icon, which for settings pages
is a handful of statics.

The row is also told when its item changes, which nothing did: the name is
edited in the details pane, so renaming an item left the list showing the old
one.

**A field did not validate.** `StringAspect::validationFunction()` is private
to the widget renderer, so `setValidationFunction()` did nothing at all on a
Quick page - the field took any value and said nothing. Around 15 settings
pages set one, several of them already ported. `BaseAspect::validationMessage()`
is the way to ask that does not need the function itself, and `StringDelegate`
shows the reason under the field.

It also keeps a rejected value out of the aspect, which the widget line edit
does not - it colours the field and stores the value anyway. A page whose
modal dialog used to refuse the value outright would otherwise start accepting
it, which is what To-Do would have done with a keyword containing a space.

Some checks cannot be made on the spot, and those needed a second round.
Whether the path a field holds is really a debugger is decided by *running*
it, so `validationMessage()` has nothing to say when it is asked. The aspect
now remembers the answer for the one candidate being asked about and says
`validationMessageChanged()` once it has it; the delegate bumps a counter its
binding reads, because what is wrong with a value is asked for rather than a
property to bind to.

Two rules fall out of holding one answer at a time, and both are worth keeping
in mind if this is ever extended:

- Nothing is reported while a check is in flight, and an answer that lands
  after the value has moved on is dropped. Showing the last answer would be
  showing it about text the user has already changed.
- One aspect, one asker. A test that had a live field on screen *and* called
  `validationMessage()` directly had the two fighting over the one candidate,
  and the direct calls kept losing.

The shape that is still unserved is the one handed a `FancyLineEdit`, which is
the widget renderer's and has nothing to give it.

**What the aspect-driven count does not include, and it is the bigger half.** It counts
pages that hand over an `AspectContainer` through `setSettingsProvider()`.
A page that calls `IOptionsPage::setWidgetCreator()` builds its own
`IOptionsPageWidget` and answers nothing from `aspects()`, so the test skips it
entirely: `isFullyRenderable()` is never asked and the page is not in the 73.
There were **15 such call sites in 14 files**, and none of them is a page that
fits in one batch any more. **Six page call sites are left, in five files**,
after Toolchains, Kits, Devices, MCU, Language Client, Bare Metal, Update,
Android and ClearCase - counted with the grep below, minus
`IMode::setWidgetCreator()` and `ioptionspage.cpp` itself. Three are
Windows-only (CDB twice, Windows App SDK) and three embed a widget from
outside Qt Creator (Designer, Help's Filters, Extension Manager's Browse).
**None of the six can be opened on a Mac**, and none is a page whose form is
aspects. What each is waiting on, checked rather than
remembered:

- **Toolchains** - done. `ToolchainConfigWidget` had **9 implementations**
  across ProjectExplorer, Nim, QNX, Android and BareMetal, and the page could
  not move until every one of them did, because a Qt Quick page cannot host a
  `QWidget` for the ones that have not. Qt Versions' extension point moved in a
  single batch because it had exactly one implementation; this one took four.
- **Kits** - done. All 16 `KitAspect` implementations are converted and the
  page is `KitsPage.qml`.
- **Devices** - done. There were **7 `IDeviceWidget` implementations**, not
  the 13 first counted; `IDeviceWidget` and all seven are deleted, and the page
  is `DevicesPage.qml`. It is the last of the three extension-point pages.
- **BareMetal's Debug Server Providers** - done. **Eleven** config-widget
  classes, not the 30 first counted, and they held the settings rather than
  laying them out.
- **MCU Support** - done. `McuAbstractPackage::widget()` looked like an
  extension point and had one implementation; the package and the page moved
  together.
- **Android SDK** - done, and it was the last of these that can be opened on a
  Mac. Its first-show package reload is `pageShown()`, and the summary widget
  it shared with the Windows App SDK page is `Utils::SummaryAspect`.
- **Designer** and **Help's Filters** embed a widget from outside Qt Creator -
  `QDesignerOptionsPageInterface::createPage()` and
  `QHelpFilterSettingsWidget`. Neither has aspects to draw.
- **Extension Manager's Browse** is a store front, not a settings form.
- **Update** - done, though it cannot be reached from a test run; see below
  for what was used instead.
- **CDB (two pages), Windows App SDK, ClearCase** cannot be reached here:
  Windows-only, or the plugin declines to register its page on this machine.
  Two of them were written and reverted rather than shipped unverified.

They are pure QtWidgets from top to bottom. Counted with

    grep -rn setWidgetCreator src/plugins src/libs --include='*.cpp'

minus the mode files, which are `IMode::setWidgetCreator()` and a different
thing - `debuggerplugin.cpp` is one of those, not a page. Gerrit, To-Do, GitLab, Beautifier's three, Clangd, Debuggers, FakeVim's User
Command Mapping and Ex Command Mapping, MIME Types, Python's Interpreters, qbs
Profiles and the Meson, GN and CMake Tools pages went this way; converting any
of them took an
`AspectContainer` that reads the plugin's own settings struct when the page is
built and writes it back on apply, which is the same shape the Code Style pages
use and needs no change to what the rest of the plugin reads.

Two of the three had a list edited through a modal dialog, and both became an
`AspectList` with the dialog's form as the item's own aspects. That is the
shape to reach for: a page that opens a dialog to edit one of a list is a
list-with-details that has not been written yet. It also removes the dialog's
usual habit of dropping what was typed without saying why - GitLab's Add
silently discarded a bad host - because the check becomes the aspect's
validation.

**Axivion was the next one of that shape, and it needed two things first.**
Its path mappings are a three-column tree with Add, Delete, Move Up and Move
Down and a details form under it - a list of sub-aspects with a details pane,
which `PathMappingDetails` already was.

- *Order.* `AspectList` could neither move an item nor say which one is
  current, so Move Up and Move Down had nothing to act on. Which item is
  current is the aspect's answer now, not the view's, so the widget list and
  the Quick one agree on what may be moved where; a page says `setOrdered(true)`
  and both grow the two buttons. The Quick row order came from the *applied*
  list rather than the volatile one, so a move showed nothing until Apply -
  worth knowing, because that is where an added row is marked and a removed one
  is struck through, and only the surviving rows' order is the user's.
- *Somewhere to put a settings object the page does not own.* Axivion's page
  shows five aspects that belong to `AxivionSettings`, and naming them as
  sub-aspects would re-home them: `registerAspect()` calls
  `setContainer()`, and an aspect's container is what its settings key is
  resolved against, so the settings would quietly move in the file.
  `Utils::ContainerAspect` - the thing Qt Versions grew for the opposite reason,
  extra settings it *does* own - hands a container over either way. It parents
  only what it owns, forwards the container's changes so the preferences dialog
  sees them, and answers `isDirty()` from it.

Its servers stayed a combo box with a dialog behind it, the way Qt Versions'
Link with Qt did: what a server is is the dialog's business, and the page only
picks the default one.

Two of the remaining ones are not this shape at all and are worth knowing
about before picking a batch: **Help > Filters** is a `QHelpFilterSettingsWidget`
from QtHelp with a `QHelpFilterEngine` behind it, and **Designer** embeds Qt
Designer's own option pages. Neither is Qt Creator's UI to rewrite; both need
the widget replaced with a form written against the engine's API, which is a
piece of work of its own rather than a port.

**Beautifier's three** are done, and they are the clearest case yet of one
shared widget holding several pages back. `ConfigurationPanel` became
`ConfigurationsAspect`, and all three moved together.

What the dialog carried was worth keeping: completion over the options the tool
documents, and what the option under the cursor means. Neither existed on the
Quick side.

- **`setCompletions()` was readable only by the widget renderer**, the same way
  the validation function was. It is in the descriptor now, and
  `CompletionPopup.qml` is the completer QtQuick.Controls does not have - a
  one-line field completes against the whole of what it holds, several lines
  complete the word the cursor is in.
- **Where the cursor is is the view's business.** `TextAreaDelegate` reports
  the word it is in, and the page hands that to the aspect, which is the only
  one that knows what it means. That is the same shape as
  `onCurrentRowChanged: aspect.setCurrentRow(...)` and belongs in the page's
  QML for the same reason: nothing in C++ can see a cursor.

### The page whose settings were the widget

C++ Quick Fixes kept every one of its twenty-odd settings in a line edit or a
check box: `loadSettings()` wrote the struct into the controls and
`saveSettings()` read it back out. That is the second workstream in its purest
form, and porting it is mostly *not* about QML - it is about naming twenty
aspects and writing the two functions once.

**The mapping is where the bugs are, so that is where the test goes.**
`testEverySettingSurvivesTheForm` loads a struct with every field set to
something distinctive and saves it back into another. A field left out of
either half is a setting the page silently resets, and nothing else in the
tree notices. Three controls - one dropped save, one dropped load, one
mis-joined list - all bit.

**A list of sub-aspects does not remember what it was loaded with.**
`AspectList` keeps `volatileItems` apart from `items`, and `cancel()` goes back
to `items`, which `addItem()` never touches. Loading the custom templates
without applying them meant Cancel emptied the list. `loadSettings()` calls
`AspectList::apply()` at the end for that reason, and
`testCancellingKeepsTheTemplatesThatWereLoaded` is aimed straight at it.

**A page that shows up in two places needs one form, not two.**
The same settings appear in a project panel, which is not an options page and
so built its own widget. `Core::createAspectForm()` hands out what
`IOptionsPage` already builds - the Quick form where the container names its
own QML, the widget layout otherwise - so the panel and the page cannot drift.
Anything else that embeds settings outside Preferences can use it.

**A nameless check box is a hole in the page census.** The four function-location
rules were a bare check box beside a grid label, and the census asserts a check
box carries its own text - for good reason: a page of nameless boxes is
invisible to every other assertion. Rather than weaken the rule, the check box
took the row label ("Outside class:", "In .cpp file:") and the label column
went.

The same census caught nine line edits that were not: `StringAspect`'s default
display style is `LabelDisplay`, so a field that is never told otherwise is
drawn as text. `setDisplayStyle(StringAspect::LineEditDisplay)` is not
optional, and the widget renderer never made anyone say it.

### The tree the grouped pages needed

**Seven pages share `Utils::GroupedView`**: CMake tools, Debuggers, GN tools,
Meson tools, Kits, Toolchains and Qt Versions. All of them are the same thing -
what was found and what the user added, in two groups, with Clone, Remove and
Make Default acting on whichever is current, and a details form beside it. That
is a fifth of the remaining backlog behind one missing piece, so the piece was
worth building rather than working around.

`GroupedView` turned out to be almost entirely behaviour: which item is
current, whether it may be cloned or removed, where to go once it has been
removed, and how to find the current item again after the tree is rebuilt. None
of that is a view's business, and it is the same answer for a `QTreeView` and a
Qt Quick `TreeView`. It now lives in **`Utils::GroupedSelection`**, which
`GroupedView` drives and which `tst_utils_groupedselection` can check without a
window. **`Utils::GroupedListAspect`** gives it an aspect face -
`displayModel()`, `currentRow`, `canClone`/`canRemove`/`canMakeDefault` and the
three actions - and `GroupedListDelegate.qml` draws it.

Two things bit while doing it, both worth knowing:

- `displayModel()` is a `Q_INVOKABLE` returning a `QObject *`, and the tree it
  returns had no parent, so QML's GC took it as its own and freed it. The
  totals were green and the process exited 134. `GroupedModel::DisplayModel` is
  parented to the model it is a view of now. This is the third time an
  unparented `QObject *` out of an invokable has cost a session; look for it
  first when a page crashes only on shutdown.
- `GroupedView` holds its tree and its three buttons **by value**. Putting them
  into a layout whose widget then owns them means that widget tries to delete
  what it never allocated. The pages get away with it because their
  `GroupedView` is a member destroyed before `~QWidget` runs; anything else has
  to arrange the same, which is what `GroupedListWidget` in
  `groupedlistaspect.cpp` does.

Meson's, GN's and CMake's Tools pages are the first three of the seven. The
first two are the same page twice, so the second took minutes. CMake needed one
thing more: **`DeviceSelectionAspect`**, the device picker four of the seven
use to narrow what they list and to choose what a re-detect runs over. It was a
`DeviceComboBox` - a widget - so it blocked all four.

**Five of the seven are done** - Meson, GN, CMake, Debuggers and Qt Versions.
What the other two still need is not the tree:

- **Toolchains** shows a `ToolchainConfigWidget` per toolchain type. That is a
  plugin extension point returning `QWidget`s, so the page cannot move until
  every toolchain implementation does.
- **Kits** has the same shape with `KitAspect` widgets.
Debuggers was the fourth, and it brought one thing worth repeating: what a
debugger *is* - its ABIs, its version, its engine - is found by running it, and
the widget page read those back out of the labels it had written them into to
decide what to store. That round trip happened to be lossless, so it was not a
bug; it did mean the truth about a debugger lived in a string on screen, and a
page that keeps it as its own state is one less thing to get wrong. Expect the
same shape in the pages that are left.

### Qt Versions, and the settings a page does not know in advance

Qt Versions had the same extension point Toolchains and Kits still have, but
with one implementation: `QtVersion::createConfigurationWidget()` returned a
`QtConfigWidget`, and the only one there was is QNX's SDP path. So the point
moved instead of the pages waiting for it - **`createConfigurationAspects()`**
returns a `Utils::AspectContainer`, `QnxConfigurationAspects` is that container,
and `qtconfigwidget.{h,cpp}` is gone.

That leaves a page having to draw aspects it does not know until a version is
picked. `aspects.<name>` reaches what the container held when the form was
built, which is the wrong shape for something that changes. The page keeps a
small aspect whose value *is* a container:

```qml
AspectItems {
    model: root.details.Configuration.container
           ? AspectModels.container(root.details.Configuration.container) : null
}
```

`AspectModels.container()` already caches one model per container, so this
costs nothing per switch. The container is parented to the aspect that hands it
out - a `Q_PROPERTY` does not give QML ownership the way an invokable does, but
the page deletes it on the next switch and a parent is what keeps those two
from racing. `testAnAspectCanHandOutAContainerToDraw` covers the whole path,
because nothing else can: no QNX Qt version exists on a machine that is not
building for QNX, so the page census never reaches this branch.

**A page census cannot see a details pane that is empty.** Qt Versions hides
nothing now - the form is there with no version picked, as the widget page's
was - and that is what lets `testAspectDrivenPagesRenderWithQuick` report on the
name, the qmake path and the info pane at all. A pane made `visible: false`
until something is current is invisible to every assertion in that test, which
is worth remembering before hiding one.

**Every one of these pages wrote the form back twice**, and only one of the two
ever does anything. Python wrote each field as it changed *and* wrote the whole
form back when the selection moved on; CMake had a `m_loading` bool beside a
cleared id; Beautifier's `reload()` remembered a selection every caller
overwrote. FakeVim's Ex Command Mapping made four.
Each was found the same way, by a negative control that stayed green. Aim one
at both halves before copying the pair across - by now the safe assumption is
that the second one is dead.

**The guard that guards nothing.** All three ported pages load a tool into a
form and store what the user types, and all three carried *two* flags for it -
a `m_loading` bool and a check that a tool is current. On Meson and GN the bool
is what does the work, because `store()` reads the current row, which is valid
throughout a load. On CMake it is not: `showTool()` clears the tool's id for
the duration, so nothing can be written back anyway, and every control aimed at
the bool came back green. The bool is gone there and the id says what it means.
Worth checking which one is load-bearing rather than copying both across.

Removing it also found a bug the flag had been hiding: reacting to the binary
field during a load looked the tool up by an id that had just been cleared, and
hid the form it was in the middle of filling in.

**ClearCase cannot be ported on macOS at all.** Its plugin declares
`"Platform" : "^(Linux|Windows)"`, so the plugin manager filters it out before
anything else applies - neither `-load ClearCase` nor `-test ClearCase` will
make it run, and it registers no page and loads no QML. A port there is
unverifiable here; one was written and reverted. The other two with a
`"Platform"` line are `ios` (macOS only, so fine here) and `incredibuild`
(Linux and Windows, same problem as ClearCase). Read the plugin's `.json.in`
before starting: `DisabledByDefault` only costs the page its place in the
census, but `Platform` costs it everything.

**Update is not portable on a source build, and not because of its UI.**
`UpdateInfoPlugin::initialize()` returns an error when it cannot find the
maintenance tool, before it registers its page - which is what happens on any
build that was not installed. So the page never exists, its QML is never
loaded, and a test written against it does not run at all: `-test UpdateInfo`
reports success having executed nothing. A port there cannot be checked even
once here, which is worse than not doing it; one was written and reverted.

**A page in a DisabledByDefault plugin is not in the census.** GN's is:
`-load all` does not put such a plugin into the running state, so its pages are
never registered and the QuickUi run that walks every page cannot see them -
the count stays where it was and nothing says the new `.qml` was never loaded.
`-test <plugin>` does run it, so the page carries its own check that its QML
reaches Ready instead.

### Three kinds of many-rows

There are now three delegates for rows, and picking the wrong one is a lot of
work to undo:

- **`TableDelegate`** - rows and columns the user edits, with Add and Remove.
  What a cell is comes from the model; see `Utils::AspectTable`.
- **`GroupedListDelegate`** - items in named groups with one of them current,
  and Clone, Remove and Make Default acting on it. `Utils::GroupedListAspect`.
- **`TreeDelegate`** - a tree the page *reports*: no editing, no selection, no
  buttons beyond Expand All and Collapse All. Driven by `tableModel()` like
  the table, because a tree model is one of those too.

MIME Types is two `TableDelegate`s: the types with their handler column, and
the magic rules that used to be `QTreeWidgetItem`s edited in a modal dialog.
Editing them in place gained a Mask column - the dialog could set a mask and
the tree never showed it - and lost the dialog's chance to say *why* a rule was
refused. In place there is nowhere to say it, so the cell keeps what it had.

Clangd was the cheapest of these by a distance, because `ClangdSettings` was
already a container: most of the work was moving the labels out of the
`Layouting::Form` and onto the aspects, which the per-project panel then shares.
**Look for that before assuming a page needs rewriting** - a page whose closure
reads `Form { Tr::tr("Label:"), s->aspect, br, ... }` is one `setLabelText()`
per line away from being renderable.

Its `ClangDiagnosticConfigIdAspect` also turned out to describe itself already,
as a `TextWithAction`. The note further down saying it blocks the Clang Tools
page is out of date.

**A filter matches what the rows show, which is not always what is typed into
it.** Nobody looks for a MIME type by its name; they type `*.cpp`. The glob
patterns are not a column, so `AspectTable::FilterTextRole` is how a model adds
text a row should be found by.

`TreeDelegate` grew a filter and a current row for FakeVim's Ex Command
Mapping, which is a tree of everything Qt Creator can do - unusable without
one. Filtering a tree is not filtering a list: keeping only the top-level rows
that match hides the branch holding what was looked for, so `TreeFilterModel`
keeps a branch whose children match.

That page was built on `Core::CommandMappings`, which the **Keyboard** page
also uses, so it looked like another shared widget holding two pages. It was
not: what the two have in common is a tree of commands and a filter, which the
delegate now provides, and the rest of `CommandMappings` is import/export
buttons the FakeVim page turned off. Keyboard still needs a key-sequence
capture control, and nothing about `CommandMappings` was worth carrying over
for it.

qbs Profiles is the first `TreeDelegate` page, and it found the limit that
decides the shape: **a Qt Quick `TreeView` has no root index.** The widget page
built every profile into one tree and pointed the view at a branch. There is no
equivalent, so the model builds the one profile being looked at - which is all
the page ever showed. Any page that reaches for `setRootIndex()` has to move
that decision into the model instead.

### Two traps in the tree delegates, both silent

Worth reading before writing another one:

- **Do not redeclare `row` or `model` on a `TreeViewDelegate`.** It requires
  both already; redeclaring one shadows the base's, which the view then never
  initialises, so *every cell fails to incubate*. The view still reports the
  right number of rows and the tests still passed - they were looking at the
  model and the row count, not at anything drawn. Both tree delegates shipped
  this way for two rounds. The tests look at the cells now.
- **`TreeViewDelegate`'s own content item binds straight to `model.display`**,
  which is undefined for a cell the model says nothing about - a group heading
  has no second column. Setting the delegate's `text` does not help, because
  the default content item does not read it. Give it one that does.

And the ownership trap for the fourth time: a model reached QML straight from
`BaseAspect::tableModel()`, so the engine took it and freed it. That is why the
delegates go through `AspectModels.tableModel()` now - one place that says the
model is C++'s, for every table and tree there will ever be.

One thing to watch: a `SelectionAspect` over a list that is being edited holds
a *position*. GitLab's default server has to be remembered by id and looked up
again whenever the choices are rebuilt, or it follows the wrong server as soon
as one in front of it is removed. `setUseDataAsSavedValue()` does not do this -
it only changes what `toSettings()` writes, and a page-local container never
saves itself.

So the settings-page migration is complete for the pages that were *already
aspect-driven*. The remaining work is a different shape: those pages have to
become aspect-driven first, which is a rewrite of what each one stores and not
a matter of writing a `.qml` for it. `setLayouter` is down to 21 call sites,
and none of them is a page's own layout any more - they are the containers that
a widget page still embeds (`TabSettings` and the four other Behavior groups in
the per-project Editor page, `CodeStyleAspect` for ClangFormat, the profiler
samplers, Lua-scripted settings).
Before the gate was narrowed, 65 pages rendered generically; the delegate work
that made that possible is all still in place and is what the ports build on:
`StringListAspect` (a real list editor), `IntegersAspect` (`Invisible`, because
it draws nothing in the widget path either), `StringSelectionAspect` (it builds
its own choices now), index-valued selections with no options (an empty combo
box is what the widget editor draws too), and `AspectList`'s
list-with-details style.

All three remaining pages are held by aspects whose control is `Custom`
because they build their own widget in `addToLayoutImpl`. Not all of them need
porting one by one, though: `EnvironmentChangesAspect` turned out to be a
summary plus one button, which is now the `TextWithAction` control, and
`BaseAspect` grew `displayText()` and `triggerAction()` for it. Look for that
shape before writing a bespoke delegate. What is left, from walking each
declined page:

| page | blocked by |
|---|---|
| Clang Tools | nothing any more: the aspect describes itself as a TextWithAction |
| Snippets | the snippets editor |
| General (x4) | `FontAspect`, an `AspectList` inline style, two unnamed `Custom` aspects |

**A finding worth knowing before porting more:** the layouter picks which
aspects a page shows; the generic form shows *every* aspect in the container.
So a storage-only aspect appears as a placeholder and declines its page -
`ProjectExplorer/Settings/EnvironmentId`, a persisted UUID that no layout ever
contained, is one. Those need marking as not-for-display; nothing does that
today, and `isFullyRenderable()` has no way to tell them from a real gap.

Still undescribed and therefore still on widgets by design:
`AspectList`'s inline-list style, which builds a row of controls per item.

To find out what blocks a page, walk its container with
`AspectContainerModel::kindOf()` and print the aspects that come back
`Unsupported`; the pass count alone will not tell you.

`QTC_QUICK_SETTINGS` now means the opposite of what it used to: render every
declined page generically, placeholders and all, so that what a page would look
like is visible before writing its QML. It reads
`Environment::systemEnvironment()`, a snapshot taken at startup, so a test
cannot flip it with `qputenv`.

Two things to know about running that measurement. `-load all` trips over any
stale plugin left in the build directory from an older configure - here a
`libQmlDesigner.dylib` from months earlier, still referencing the removed
`AspectContainer::setLayouter` - and a plugin that fails to load aborts the
whole test run, so `-noload` it. `UpdateInfo` fails in a dev build for
unrelated reasons and needs the same treatment. Also note that the pages a
`-test` run sees are the tested plugin's dependency closure unless `-load` is
given; the count was 4 before, and 21 with one extra plugin.

**A table cell carries its model's tooltip.** `withRoleNames()` names
`Qt::ToolTipRole` as `cellToolTip`, and a read-only cell shows it in preference
to its own text. Documentation lists namespaces and keeps the file path in the
tooltip; without this the path was simply gone.

**The remaining widget-creator pages are list managers, and they needed more
than one row at a time.** `TableDelegate` selected a single row, and the widget
tables it replaces used `ExtendedSelection`: Custom Output Parsers exports what
is selected and removes what is selected, and Kits, Toolchains, Devices and
Debuggers are the same shape. The delegate now sets
`selectionMode: TableView.ExtendedSelection` and answers `selectedRows` in the
aspect's own order; Remove takes them all, backwards, so that removing one does
not move the next.

A page reads the selection through its *aspect*, not through the page container:
`aspects` in QML is the `NamedAspects` map, so `aspects.setSelectedRows(...)`
is not a function and `aspects.Parsers.setSelectedRows(...)` is. That mistake
was caught by the QML-warning check rather than by looking at the page, which is
what that check is for - a missing binding is still not caught, only a
malformed one.

**A page whose value is what should still exist.** Macros deletes and renames,
and the widget page kept two lists of pending changes beside a `QTreeWidget`.
`MacrosAspect`'s value is a name-to-description map of the macros that *should*
be there, so a removal is a key that is gone and a rename is a value that
differs - `apply()` works both out by comparing it with what `MacroManager`
holds, and dirtiness comes from comparing two maps rather than from tracking
edits. Worth reaching for whenever a page manages a list of things that live
somewhere else.

**Pick a page whose plugin runs, and check before writing it.** ClearCase was
the obvious next one - eleven settings in a plain struct, with real behaviour to
move into the constructor - and the port was written and then thrown away,
because the page could not be made to appear. `-load all` does not cover a
plugin whose `.json` says `DisabledByDefault`, `-load ClearCase` leaves it
`resolved` rather than `running`, and a `-test` run uses its own settings path
so `Plugins/ForceEnabled` in a scratch one is not read. With no page there is no
screenshot and no walk over it, and the QuickUi count does not move to tell you
either. `list_plugins` over the MCP server says which plugins are running;
intersect that with the `setWidgetCreator` call sites before choosing.

**A choice can be there without being offered.** `AspectPresentation::Choice`
carries `enabled` and the Quick delegates ignored it, so an option the aspect
had disabled was selectable. `RadioGroupDelegate` reads it now. The combo box
still does not: Qt Quick's `ComboBox` takes a plain list, and disabling one row
of it needs a delegate of its own.

**A path aspect had nowhere to browse from.** `StringDelegate` draws both a
string and a path, and drew them identically: a field and nothing else, on
every page with a `FilePathAspect` on it. `AspectPresentation` carries
`pathKind` (mirroring `PathChooserKind`) and the prompt title and filter now,
and the delegate puts a Browse button beside the field that opens a `FileDialog`
or a `FolderDialog`. What it is not yet is a `PathChooser`: no expansion of the
typed path, no per-kind validation feedback, no "open terminal here".

**A short page spread its rows down the viewport.** `AspectPage` sizes its
column to at least the viewport so that a table can fill it, and a column that
is taller than its content hands the slack to its children - so six settings
came out evenly spaced over 900 pixels. The page holds itself at the top now,
and a page whose content *should* fill says `contentFillsHeight: true`. That is
stated rather than worked out: reading the children's `Layout.fillHeight` to
decide gave the wrong answer, and a filler declared before the default-property
children lands in front of them - re-parenting moves it to the end, but only if
it is taken out of the parent first.

**Labels are written for the widget renderer.** `"Ta&b size:"` means a keyboard
accelerator there; Qt Quick Controls has no mnemonics, so the delegates drew the
ampersand. 45 labels across the ported pages had one. `BaseAspect` answers
`plainLabelText` for the Quick side - `labelText` with `stripAccelerator()`
applied - and the aspect still reports what it was given, because the widget
renderer needs it. Anything new that draws a label should read the plain one.

Remaining delegate gaps, which now decide whether a page is accepted rather
than whether it renders correctly:

One trap when writing a delegate for a list-valued aspect: a `QStringList`
property reaches QML as a sequence that **writes through**. Indexing into
`aspect.value` and assigning an element sets the aspect immediately, once per
element. Copy the list (`.slice()`), edit the copy, assign it once - and check
that removing the assignment fails your test, because otherwise it will pass
on the write-through by accident.

- `StringSelectionAspect` and anything else whose choices come from an async
  fill callback report no choices, so they render as `Unsupported`. They need
  the choices in `AspectPresentation`, or a model role fed from the callback.
  Note this is *not* the same as an aspect storing a choice id rather than an
  index - that one is handled, via `AspectPresentation::valueIsChoiceId`.
- `StringList` (needs `appendValue`/`removeValue` invokable from QML),
  `FontPicker` (a container, not a `TypedAspect`, so it has no bindable value)
  and `IntegerList` (no `QVariantList` -> `QList<int>` conversion) are
  placeholders. The reasons are recorded next to `kindOf()`.
- Row and column arrangement within a group is not expressible: the model is a
  list, and `AspectPresentation` carries `spanX`/`spanY` but nothing renders
  them yet.

### The Quick backend's replacement for setLayouter()

A page points at a QML file:

    setQmlSource(QUrl("qrc:/.../QmakeSettingsPage.qml"));

`createAspectForm()` loads it when set and falls back to the generic
model-driven form when not, so pages migrate one at a time. The page reaches
its aspects by name:

    AspectPage {
        GroupBox {
            title: qsTr("Parsing")
            BoolDelegate { aspect: aspects.RunSystemFunction }
        }
    }

`aspects` is a `QQmlPropertyMap` of `BaseAspect::qmlName()` to aspect.
`qmlName()` defaults to the last component of the settings key (the tree uses
both `/` and `.` as separators, and plenty of keys use neither) and
`setQmlName()` overrides it. An aspect with no settings key has no derived
name and is not reachable until given one; two aspects deriving the same name
assert rather than leaving one silently unreachable.

Writing the first such page immediately showed the delegates were wrong: they
took `labelText`, `toolTip` and visibility as model roles, so a hand-written
page would have had to repeat what the aspect already knows. All three are
`Q_PROPERTY` on `BaseAspect`, so the delegates now read them from the aspect
and those roles are gone. That also fixed a latent bug - the model emits no
`dataChanged`, so a label or visibility change never reached the control.

**Where plugin page QML lives: a QML module per plugin.** Decided, and
`QmakeProjectManager` is the worked example - `qt_add_qml_module` next to
`add_qtc_plugin` with `URI QtCreator.<PluginName>`, `NO_PLUGIN` and
`RESOURCE_PREFIX "/qt/qml"`, giving `qrc:/qt/qml/QtCreator/<PluginName>/`.
This buys qmllint and qmlcachegen over the page files. qbs cannot build QML
modules, so its `.qbs` only lists the files in a `fileTags: []` group - the
same split `qtcquick.qbs` already lives with, and the reason to keep the note
here rather than pretend the two build systems agree.

### The recipe for a page

1. Write `<Page>.qml` with `AspectPage` as its root, addressing aspects as
   `aspects.<qmlName>`.
2. Add the `qt_add_qml_module` block to the plugin's `CMakeLists.txt` and list
   the file in its `.qbs`.
3. `setQmlSource(QUrl("qrc:/qt/qml/QtCreator/<Plugin>/<Page>.qml"))` in the
   container's constructor; leave the layouter alone.
4. Run `-test QuickUi -load <Plugin>`. The page test checks that a container
   naming a QML file renders through it rather than falling back to the
   generic form, so a wrong URI or a missing module fails there.

Loading one plugin into that test took the aspect-driven page count from 4 to
21, which is how the `EncodingSelectionAspect` bug below was found. Expect
each new plugin loaded to surface more: the way to look for them is the
`QWARN` lines, not the pass count.

### Phase 4 - the closures

Then, per page: move the grouping into nested containers or write the page
`.qml`, and once Quick is the default, delete the closure. 65 pages are pure
aspect arrangements; the other 27 mix in widgets the model cannot describe and
need hand-written `.qml` regardless.

Do not try to shortcut Phase 1 by splitting `layoutbuilder.h` - see the note
under step 0 for why that compiles and does not link.

## Next steps

**Where this actually stands.** Everything numbered below is done. What is
open is not more porting but three decisions, none of which should be taken
without the user:

- **The file watcher is fixed.** It was two bugs. `QTC_CHECK_RESULT` named its
  argument twice, so `~DesktopFilePathWatcher` retried a failed removal and the
  retry soft asserted; that macro now binds the result once and
  `tests/auto/utils/result` covers it. The rest was one file under two names -
  the temporary directory is both `/var/folders/...` and
  `/private/var/folders/...`, and `DocumentManager` canonicalises where a
  document does not. The watcher keys by the canonical path now, so one file is
  one entry. `Failed to watch` went from 20 to 0 and the soft asserts from 34
  to 1, and the one left is `qtversionmanager.cpp`, unrelated.

  **A second bug was underneath it.** The key has to be worked out when the
  watcher is built, not again when it is removed: a temporary file is usually
  deleted before the thing watching it, and the canonical form of a path that
  no longer exists is not the one it had. Sweeping the suites after the first
  fix is what found it - the warnings were gone everywhere, and CMake still
  had nine soft asserts about watches it had added. One left now, and it is
  `qtversionmanager`'s.

  **The mechanism was measured, and two guesses before it were wrong.** It is
  not that `QFileSystemWatcher` refuses the second name: a standalone probe
  shows both adds succeed and both names appear in `files()`. It watches the
  file once, so removing either name takes that watch away and leaves the
  other listed, neither watching nor removable - and the next add is then told
  the file is already watched, so that asker gets an error and no watch at
  all. `tst_filepath` reproduces exactly that, and needed three attempts to:
  the two names have to be the real pair, and both watchers have to be dropped
  before the stranding shows.

- **The Qt Quick editor's reach.** It has hover tooltips, follow symbol and
  Ctrl+click, and it draws Code Style, Snippets and Font && Colors. It is not
  Creator's editor. Making it so is a large change nobody has asked for.
- **`ICodeStylePreferencesFactory` lost its widget editor API**, which was
  public on an exported class. Five members went in
  `TextEditor: Delete the code style widget fallback`:

      using ValueEditorCreator = std::function<QWidget *(ICodeStylePreferences *)>;
      QWidget *createValueEditor(ICodeStylePreferences *) const;
      bool valueEditorHasPreview() const;
      void setValueEditorCreator(const ValueEditorCreator &);
      void setValueEditorHasPreview(bool);

  Nothing in the tree calls them. An out-of-tree language plugin that supplied
  a widget code style editor no longer compiles, and its way back is the one
  every in-tree language took: `setSettingsAspectsCreator()` to hand over the
  aspects the form edits, and `setQmlSource()` to name the form. A language
  that names neither now gets `CodeStyleDefaultPage.qml`, so it still has a
  working page without doing anything. Intended under "never keep widgets",
  and the only deliberate source incompatibility on the branch.

The numbered list below is the historical order the work was done in.

0. **`aspects.h` is clean. `aspects.cpp` is not, and what is left in it is
   named below.**

   The forwarding API is gone. `FilePathAspect::pathChooser()` was the bulk of
   it - 14 of the 20 remaining `PathChooser` uses - and it is replaced by what
   its callers actually wanted: `isValid()`, the aspect's own `validChanged`,
   `volatileValueChanged` in place of the chooser's `textChanged`,
   `resolvedVolatileValue()`, `setFocusToInputField()`, `validateInput()`,
   `defaultValidationFunction()` and `addButton()`. The setters that used to
   poke the cached widget now emit `controlConfigurationChanged()` and the
   renderer re-reads the aspect, so live updates survive without either side
   holding the other. `TextDisplay`'s cached `InfoLabel` and its
   `InfoLabelType` parameter, `StringSelectionAspect::fixupComboBox()`,
   `StringAspect::setCompleter(QCompleter *)`, `BaseAspect::aspectForWidget()`
   and `WorkingDirectoryAspect::pathChooser()` went the same way.

   With the checkable composite moved to the renderer, no aspect has an inline
   `addToLayoutImpl` body left, and no aspect holds a pointer to a control.

   Measured over the include graph (same metric as the table above):
   **`aspects.h` reaches no QtWidgets header.** It is still counted on the
   widget side only through the pairing rule, because `aspects.cpp` is. That
   distinction matters: the header is what `AspectContainerModel` and every
   other consumer sees.

   The widget code the aspects used to run themselves has moved out too.
   `createSubWidget`, `registerSubWidget`, `createLabel`,
   `addLabeledItem(s)`, `addMacroExpansion`, `improveWheelScrolling`,
   `createConfigWidget`, `adoptButton`, `addToLayoutHelper` and
   `groupChecker` are free functions in `Utils::AspectWidgets`
   (`aspectwidgets.{h,cpp}`) taking the aspect as their first argument. They
   had to stop being members: on Windows an exported class cannot have its
   member definitions split across two libraries, so as long as they were
   members no amount of moving other code could get `aspects.cpp` off the
   widget side. 29 aspects that build their own control call them by name now.
   `addToLayoutHelper` and the renderer's `renderBool` turned out to be the
   same function written twice, so they became one `addButtonToLayout` that
   the renderer, `adoptButton` and the checkable composites all go through.

   Two more headers were pulling QtWidgets in for non-widget reasons and were
   split: `CheckableDecider` (two `std::function`s) out of
   `checkablemessagebox.h` into `checkabledecider.{h,cpp}`, and
   `CheckableAspectImplementation::addToLayoutFirst/Last` out of
   `checkableaspect.h` into the renderer.

   **`aspects.cpp` now has no QtWidgets include of its own, and exactly one
   widget-side dependency: `layoutbuilder.h`.** That one cannot be worked
   around, and it is worth being precise about why, because the obvious idea
   does not work.

   The obvious idea is to split `layoutbuilder.h`: `Layouting::Object`,
   `LayoutItem` and `Layout` name widget types only through pointers, and the
   only declarations needing a complete widget type are `QFrame::Shape`,
   `QDialogButtonBox::ButtonRole/StandardButton` and
   `CompletingTextEdit::CompletionBehavior` (nested enums, so their enclosing
   class must be complete). A `layout.h` with the core, included by
   `layoutbuilder.h`, would compile with no consumer changes.

   It would not link, and this is measured, not reasoned:
   `nm -u aspects.cpp.o | c++filt` lists exactly three Layouting symbols -
   `Layout::addItem`, `LayoutItem::~LayoutItem` and
   `addToLayout(Layout *, const Layout &)` - all defined in
   `layoutbuilder.cpp`. `AspectContainer::addToLayoutImpl` does not merely need
   the type; it calls `parent.addItem()`, which reaches
   `Layout::addLayoutItem`, which does `qobject_cast<QBoxLayout *>` on the
   product and populates real layouts. `Layout`'s implementation is widget
   code, so it belongs in `UtilsWidgets`, and a `Utils` that calls it would
   need to link against `UtilsWidgets`. Splitting `Layout`'s member
   definitions between the two libraries is not an option either: on Windows
   an exported class cannot have that. Even storing the layouter is enough to
   bind the two - `std::function<Layouting::Layout()>` instantiates
   `LayoutItem`'s destructor, which is declared in the header and defined in
   `layoutbuilder.cpp`.

   So the aspects cannot be separated from `Layouting`, and the `Utils` split
   waits on `Layouting`'s removal - which the plan calls for anyway. The scale
   of that coupling: 95 `setLayouter` call sites and 70 `addToLayoutImpl`
   overrides outside `Utils`. Both `BaseAspect::addToLayout`/`addToLayoutImpl`
   and `AspectContainer::setLayouter` have to go, the same way the helper
   family went - the entry point becomes a free function on the widget side.

   Do not "fix" the nested enums by weakening the signatures to `int`;
   `setFieldGrowthPolicy(int)` already does that and it is not a pattern to
   spread, and as shown above it would not help.

   What is left in `aspects.h` that mentions a widget type is only what those
   free functions need in their own signatures, plus
   `ConfigWidgetCreator = std::function<QWidget *()>`, which never needs
   `QWidget` complete.

1. **The earlier measurement note, kept because the method matters.**
   Every binary that renders aspects now installs the renderer (`main.cpp`, the
   qtprofiler tool, the aspects manual test, the renderer autotest), so the
   inline bodies are only reached when the renderer declines a control. What
   deleting them buys, counted by compiling `aspects.cpp` with every QtWidgets
   include stripped and reading the compiler's own list of required types:

   | | widget references in `aspects.cpp` |
   |---|---|
   | inside the 17 `addToLayoutImpl` bodies | 51 |
   | everywhere else | 19 |

   So the bodies are 73 % of the widget surface. The remaining 19 sit in
   `registerSubWidget`, `createLabel`/`addLabeledItem(s)`, `groupChecker`,
   `StringAspect::setCompleter`/`completer`, and a few gui-sync remnants -
   `QItemSelectionModel` and `QCompleter` (3 each), `QComboBox`, `QListWidget`,
   `QSpinBox`, `QGroupBox`, `QDoubleSpinBox` (2 each), and one each of
   `QFontComboBox`, `QButtonGroup`, `QVBoxLayout`.

   Note what *not* to do: an earlier estimate counted whole `#include` lines
   rather than references and concluded deletion would free only 7 of 17
   includes, which made the step look worthless. Counting references, and
   letting the compiler enumerate them rather than a regex, gives the honest
   73 %. The include-level view was also actively misleading: `<QLayout>` was
   masking `<QVBoxLayout>`.

   Deleting the bodies makes a missing renderer render nothing. There is no
   self-registration precedent in this codebase (no `Q_CONSTRUCTOR_FUNCTION`
   anywhere), and the house style is an explicit install next to
   `installWidgetPrompts()`, so the deletion should make the absence loud with
   `QTC_CHECK` rather than silently degrade. After the split, the renderer lives
   in `UtilsWidgets` and linking it is what supplies rendering.

1. ~~Split `terminalcommand.cpp`.~~ **Done** - unpinned the
   `filepath`/`qtcprocess`/`environment` cluster, 96 files at once. The same
   pattern has since also split `TerminalSolution` into a QtGui-only
   `TerminalModel` and the widget `TerminalLib`, verified at the binary level.
2. **Invert `addToLayoutImpl` into `AspectPresentation`.** In progress. Done:
   the descriptor and `BaseAspect::presentation()` with all 16 built-in and 7
   plugin aspects reporting a control; GUI writes undoable without a widget;
   and the last four aspects that kept GUI state in a widget pointer
   (FilePathAspect, IntegerAspect, DoubleAspect, MultiSelectionAspect) now
   use the UndoableValue pattern like the other seven. Remaining, in order:
   the descriptor fields from the feasibility study (in progress), then a
   widget renderer keyed on the control that the 16 `addToLayoutImpl`
   bodies collapse into, then the move of that renderer plus the two dozen
   QtWidgets includes out of `aspects.cpp`. Note the Windows constraint on
   the end state: an exported class's member definitions cannot be split
   across two libraries, so the virtual itself must go, not merely move.
3. The remaining 15 clean-header/tainted-impl pairs are genuine widget classes
   whose headers happen not to name a widget type (`tooltip.h`, `dropsupport.h`,
   `fadingindicator.h`, `jsonrpcinspector.h`, `guiutils.h`, ...). They belong on
   the widget side and need no work; they classify there when the split lands.
4. **Then the split itself**: two `add_qtc_library` calls over the same
   directory, so include paths do not change. Note the export macro: a second
   library needs its own (`add_qtc_library(Foo)` auto-defines `FOO_LIBRARY`), and
   defining `UTILS_LIBRARY` for both would mark imported symbols as exported,
   which breaks on Windows.

## On seams

Three callbacks now let core code ask a user interface for something it cannot
do itself: `ExtensionSystem::PluginPrompts`, `Utils::Prompts`, and the file icon
in `Utils::DeviceFileHooks`.

They were kept separate on purpose. The first two are *prompts*, and the plugin
ones are specific dialogs (a markdown document with an acceptance checkbox), not
generic questions, so they belong to the library that raises them. The icon is
not a prompt at all but a service, and `DeviceFileHooks` already existed for
exactly that shape, so it went there rather than into a new seam.

If a fourth arrives that fits none of these, that is the point to reconsider a
single "host services" object, rather than at the third.

## Nothing drawn inside a Qt Quick page consults a layouter

Chasing the remaining closures one surface at a time was going to take as many
batches as there are surfaces, so this is the general statement, checked:

- a **page** container with a `qmlSource` is rendered by the page's own QML;
- a **nested** container reached by name from that QML is laid out by the QML;
- a nested container drawn *generically* goes through `AspectItems`, which takes
  a model;
- an **AspectList item pane** is `AspectListDelegate` doing
  `setSource("AspectItems.qml", {"model": currentItemModel})`.

None of those paths asks for a layouter, and the word appears in QtcQuick
exactly once, in a comment. So **every container drawn anywhere inside a Quick
page has a dead layouter**, and since the census renders all 107 pages with
Quick, that is every settings page and everything under one.

Four went on that basis. Three - Custom Language Models, To-Do's keywords and
GitLab's servers - were `Form { a, br, b, br, c }`, one aspect per row, which is
what `AspectItems` draws anyway. The fourth, the MCP server's listen address,
was a `Row` putting two aspects side by side, and `McpServerSettingsPage.qml`
already does that itself with a `RowLayout` around the two delegates.

**One was deliberately left.** ACP's closure builds `InfoLabel`s and wires
behaviour into them - the case the factory's comment warns about, where a
layouter does more than arrange the aspects. Deleting it would drop those labels
unless the page reproduces them, which is a port and not a deletion.

**Two more went the same way**, and these are the useful shape: neither is a
page container. ClangTools' `RunSettings` and ProjectExplorer's
`PerProjectProjectExplorerSettings` are *nested* containers, and the check is
the same one - who draws them. `ClangToolsRunOptionsForm.qml` names all five run
options in a group titled "Run Options", which is what the closure's
`Group{title(...), Column{...}}` built; `BuildAndRunProjectPanel.qml` names all
seven per-project settings. In both cases the QML says what the closure said,
and only the QML is read.

**The control is of the reasoning, not the deletion.** Deleting a layouter
cannot change what a Quick page draws, so no test can fail from it - which makes
"the tests still pass" worth very little here. What the deletion *relies on* is
that the QML draws those aspects by name, so that is what gets broken:
misspelling one name in `ClangToolsRunOptionsForm.qml` fails the census with
*"Clang Tools: ... has no aspect"* and trips the QML-complaint collector as well.
That is the assertion holding this ground.

**And clangd's two, which came with a helper worth deleting.** Both
`ClangdSettings` and `ClangdProjectSettings` set the same closure,
`clangdSettingsLayout()`, and that one is not pure arrangement: it builds an
`InfoLabel`, wires `clangdPath.addOnChanged()` into it and posts a queued update.
That is the shape that stopped the ACP closure being deleted.

Here it was safe, because **the behaviour had already been moved where it
belongs**. `ClangdSettingsPage` carries a `TextDisplay versionWarning` aspect
named `VersionWarning`, and updates it from `updateVersionWarning()` wired in the
*constructor* - which is exactly the split this plan asks for, behaviour in the
constructor and only a list in the layout. The closure's `InfoLabel` was the
widget-side twin of an aspect that already exists, and
`ClangdSettingsForm.qml` names every clangd setting for both the page and the
project panel. So the helper went with its two call sites.

**A flake worth naming, because it looked like a regression.** CppEditor came
back 11 failures against a baseline of 4 - Locator, renaming and model-manager
tests, none of them near a settings layout. Sampling rather than assuming: a
second run with the same change gives 1543 passed, 4 failed, the baseline
exactly. This suite is on record as giving wildly different counts from the same
binary, and one sample of it is worth nothing in either direction.

## The ACP closure, ported rather than deleted

This is the one that was skipped twice for being more than a layout, and it is
the plan's own recipe worked through: *separate what it lists from what it does*.

What it **did**, all inside a layouter: built two `InfoLabel`s, computed their
text and visibility from the chosen template and whether the executable is on
PATH, hid `name`, `launchCommand`, `launchArguments` and `environment` when a
template was chosen, posted a queued first run, and made three `connect()`s.
What it **listed** was seven rows.

The two labels are now `TextDisplay` aspects - the same move clangd's
`VersionWarning` had already made - and everything else is in the constructor.
The item pane draws them without being told to, because an AspectList item is
drawn by `AspectItems` over a model.

**The port moved behaviour, so the first thing to ask is what pinned it.**
Nothing did: breaking `updateVisible()` left all 43 ACP tests passing. Behaviour
that no test holds is behaviour a port can silently drop, so it has a test now -
choosing a template hides the four fields, clearing it brings them back - and
breaking `updateVisible()` fails it by name. That is the difference between
moving code and porting it.

Two mechanical notes, both of which cost a build: a class with `Q_OBJECT` in a
`.cpp` needs `#include "<file>.moc"` at the end, and a `#include <QTest>` added
next to the test block lands *inside the namespace*, where `QTypeInfo`
specialisations cannot go. Includes belong at the top even when what needs them
does not.

**What this batch does not verify.** These closures were dead on the Quick side,
which is the side that is used; nothing here proves the *widget* rendering of
those containers is unaffected, because nothing draws them that way any more and
no test does either. That is the same reasoning that made them safe to delete,
so it cannot also be evidence - it is stated rather than hidden. The tests that
do pass over this ground are the census's "every visible aspect appears on the
page" and the AspectList item-pane tests in QuickUi.

## Four of the five behaviour layouters were not waiting on ClangFormat

The condition recorded above - *the code style and tab/behaviour/storage/typing
layouters go when ClangFormat's factory names a QML form* - is right for one of
them and wrong for the other four. Grouping them by the page they appear on hid
that they are pinned by different things.

Both surfaces that draw these five containers are already Quick:
`BehaviorSettingsPage.qml` for the global page and `EditorProjectPanel.qml` for
a project's Editor panel, each drawing five forms over `AspectModels.named()`.
Following the registrations rather than the page, `BehaviorSettings`,
`TypingSettings`, `StorageSettings` and `ExtraEncodingSettings` are registered
in exactly those two places and nowhere else. `TabSettings` is not: three code
style containers embed it (C++, Nim, QmlJS - all with a `qmlSource`), and so
does `TestCodeStyleEditor` in `codestyleaspect_test.cpp`, with
`Layouting::Column{&m_tabSettings}.attachTo(this)`.

That last one is the whole difference. It is the fixture for *a language that
has no Qt Quick form* - ClangFormat's shape - so `TabSettings` is the only one
of the five whose layouter the ClangFormat gate actually holds up.

**Measured before deleting, because the static argument has been wrong here
four times.** The `layouter()` probe over TextEditor, ProjectExplorer,
CppEditor, QmlJSTools and QuickUi hands back a stored closure 12 times, and
every one is a test: five `CodeStyleAspectTest` cases (twice each - the
`CodeStyleAspect` and the `TabSettings` inside it), `CppCodeStyleAspectsTest::
testTheWidgetFormShowsOneCategoryToo()`, and the QuickUi fixture's own
`widgetPage`. The four never appear. Static and runtime agree, which is the
only reason this was a deletion rather than a port.

**Why a deletion needs the probe and not just a test run.** Removing a layouter
does not leave a hole - `layouter()` falls back to a column of the aspects - so
a container that quietly lost its designed layout still draws, and a test that
only asks whether controls exist stays green. The evidence has to be that the
stored closure was never handed out, not that nothing failed afterwards.

## A page the code style tests named but never had

Reading the TextEditor run for warnings rather than failures turned up
`CodeStyleTestPage.qml: No such file or directory`, followed by the soft assert
in `QuickWidget::setSource()`. The file had never existed. Two test factories
name it as their form, and
`CodeStyleAspectTest::testTheApplyButtonReachesTheStyleAndNotJustTheAspect()`
builds a real `IOptionsPage` around one and presses its Apply.

**The test was not vacuous, which is worth separating from the bug.** Restoring
the old `IOptionsPageWidget::apply()` - the one that read the first child's
`isAutoApply()` before asking whether the page was dirty - fails that test and
only that test. It works off the container, and the container is built whether
or not the form loads.

What was missing is the other half of what it claims. The page it builds is a
*Quick* page, and it was proving nothing about that: every code style form in
the product could have stopped loading and this would still have passed. So the
form exists now, drawing whatever the language handed over through
`AspectItems` - the two factories hand over different things, and the one that
exercises style pools hands over nothing - and the test asks the built widget
for its `QQuickWidget`'s `status`. Breaking the QML fails it with `Actual: 3`.

Two smaller things fell out of it. The form deliberately leaves out
`CodeStyleSelector`: its buttons are Qt Quick Controls, a plugin test loads
Controls before `QQuickStyle::setStyle()` runs, and drawing them traded one
warning for eight customization ones. And the file is listed under
`if(WITH_TESTS)`, because a fixture has no business shipping - which qbs needs
no edit for, its `qml` group being a `*.qml` wildcard.

**The general point.** A test run's warnings are not decoration. This one had
been printing a missing file and a soft assert on every TextEditor run, and
nothing failed, because the test asserted the half that worked.

## The profiler samplers, and the surface nobody had looked at

The four sampler layouters were written off last batch as "load-bearing for a
widget tool". That is a reason to port them, not to skip them - and the tool is
`Qt Profiler.app`, which this build produces, so unlike ClangFormat it can
actually be run.

`qtprofilerwindow.cpp` called `AspectWidgets::layouter(s)().attachTo(config)`
directly. It now asks `QtcQuick::createAspectForm(s)` first and falls back to
that, which is the same "a page moves to Quick on its own" contract the code
style pages use: `createAspectForm()` returns nullptr for a container with no
`qmlSource`, so the four backends convert one at a time.

**Three of the four have moved.** The QML profiler's and the combined sampler's
closures only arranged aspects. The other two each build a process picker - a
button and a label wired to `ProcessPickerDialog::pickProcess()` - and the perf
one additionally embeds `perfSettings.createPerfConfigWidget()`, a whole widget
no list of aspects describes. That widget is the honest blocker, so perf keeps
its layout.

**The picker turned out to be a control that already existed.** An
`ActionAspect` given a `setSummaryProvider()` presents as `TextWithAction` - a
summary and a button beside it - which is exactly a value edited in a dialog.
So the call-stack sampler's button and label became one aspect, `pickProcess`,
with `setEnabler(&attach)` replacing the hand-written enable/connect pair. Worth
reading `AspectPresentation` before inventing a control: this one was already
described in the header as "a value that is edited in a dialog rather than in
place".

**And the behaviour that moved got a test**, per the ACP rule: the picker being
offered only while attaching, and saying so before anything is picked, both ran
only when someone opened the page before. Dropping the enabler fails the first;
dropping the summary provider fails the second.

**The feature toggles forced a decision about nesting.** Each of the two builds
one `BoolAspect` per `QmlDebug::ProfileFeature`, so a form cannot name them -
how many there are is QmlDebug's business. They now live in a nested
`AspectContainer features`, drawn by a `Repeater` over
`AspectModels.container(aspects.Features)`, which is what makes the form
independent of the count.

That nesting is a settings-compatibility question, and the answer was not
obvious: `BaseAspect::writeToSettingsImmediatly()` takes the group from the
*immediate* container only, which for a nested one would drop the outer group
and orphan every toggle a user has. It is not the path these take -
`AspectContainer::readSettings()`/`writeSettings()` recurse with the outer
group still open - but "I read the code" is what has been wrong here before, so
`tst_Aspects::aNestedContainerKeepsItsAspectsSettingsKeys()` now asserts it, and
giving the nested container a group of its own fails it with
`Outer/Inner/Nested`. That is a Utils guarantee, tested where it lives.

**How a form with no test was verified.** qmllint passes on both, and qmllint
cannot see a wrong `aspects.Foo` - that is a property-map lookup, not a type.
So the tool was run offscreen against each backend it offers. All three load
with no diagnostic; renaming `Executable` to `ExecutableZZZ` prints `Unable to
assign [undefined] to Utils::BaseAspect*`, and renaming `Features` trips the
soft assert in `AspectModels::container()`. Two controls, because the two names
are resolved by different mechanisms.

**Stated rather than hidden:** the tool gained a `QtcQuick` dependency, and it
has an Emscripten target. Whether `QQuickWidget` builds there was not checked -
there is no wasm configuration here. The qbs side is a one-line `Depends` that
mirrors the CMake `DEPENDS` and matches what `quickui.qbs` does, but no qbs
exists on this machine, so it was not resolved.

## What a Quick form asks for when it is not the whole page

Embedding a form beside other widgets was new with the profiler tool, and its
size was never checked - the settings dialog gives a page all the room there
is, so nothing had ever depended on the answer.

Measured: `createAspectForm()` hands back a widget whose `sizeHint()` is
`0 x <content height>` - 68 for two rows, taken from the root item's
`implicitHeight`, with a Preferred policy. Width 0 means the layout decides the
width, which is what a layout is for. In a 500x200 column between a label and a
button the form came out 460x68. So it sizes correctly, and the tool's panel was
not quietly collapsed.

`testAFormInAWidgetLayoutAsksForItsContentHeight()` keeps it that way, and asks
the question twice - two rows and four - rather than comparing against 68, so
the assertion is about the content and a fixed default cannot satisfy it.
Making the helper ignore its argument fails it with "a form of four rows asks
for 68, one of two for 68".

## Axivion's path mapping, and a plugin that had no tests

`PathMappingDetails` is drawn in two places, and only one of them was using its
layouter. On the settings page it is an `AspectList` item, and
`AspectListDelegate` loads `AspectItems.qml` over the item's model - it never
consults `qmlSource()`, only `createAspectForm()` does. So the closure existed
for exactly one caller: the modal that asks for a missing mapping.

That made the port contained. The container names a form now, the dialog asks
`Core::createAspectForm()` instead of reaching for the layouter, and the page is
untouched - the census still renders 107 of 107.

**The aspects had no names.** Being list items, they carry no settings keys, and
`qmlName()` falls back to the last segment of the key - so all three answered
"". A form could not have addressed them. `setQmlName()` on each is additive:
the item pane draws by model role, not by name.

**Axivion had no tests at all**, and this form is reachable from no other one:
the census only walks pages, and nothing else renders it. So the plugin has a
test hook now, and one test.

Getting it to assert anything was the interesting part. The obvious check -
count the delegates that got an aspect - cannot be written here, because the
QML items are children of the `QQuickWidget`'s offscreen window rather than of
the widget, so `findChildren()` does not reach them, and reaching them properly
would mean making Axivion link Qt Quick for a test. It does not need to: a name
the form gets wrong is not a load error, it is a *diagnostic*, so the test
collects messages around `createAspectForm()` and asserts that none says
`Unable to assign`. Renaming one `qmlName()` fails it with the file and line.

That is the same shape as the code style page test earlier on this branch: ask
the metaobject for `QQuickWidget::status` rather than linking Qt Quick, and
assert the diagnostic when the outcome is invisible.

## Perf: the blocker was a widget drawing a page that already existed

`PerfConfigWidget` was called the honest blocker twice. Reading it, it draws the
events table, the reset button and the call-graph/frequency aspects - which is
what `PerfSettingsPage.qml` already draws, because the CPU Usage page was ported
in the table workstream. Its only widget-exclusive control is "Use Trace
Points", and `createPerfConfigWidget()` hides that when there is no target,
which is exactly the standalone tool's case. Add and Remove come free:
`AspectPresentation::allowAdding`/`allowRemoving` default to true, so
`TableDelegate` offers them without being asked.

**What actually blocked it was ownership, not drawing.** `PerfSamplerSettings`
holds `Profiler::PerfSettings` as a plain member and forwards `readSettings()`
and `writeSettings()` to it by hand. Registering it - the obvious way to make a
form able to name it - would do two things quietly:

- `PerfSettings` sets no settings group, so its keys are shared with the IDE's
  CPU Usage page. Nested under `PerfSampler` they would all move, and the tool
  would silently lose the perf configuration it has.
- `PerfSettings` is `setAutoApply(false)` while a sampler is `true`, and
  `insertAspect()` pushes the container's flag onto whatever it takes in.

So the seam is `SamplerSettings::reusedSettings()`: settings a backend reuses
rather than owns. The window draws them beside the backend's own form instead of
nesting them inside it, which leaves both the keys and the apply alone. The
process picker moved the same way the call-stack one did, and the last profiler
layouter is gone.

**Verifying it needed a different instrument.** Perf is not offered outside
Linux, so the "run the tool offscreen against each backend" technique - which
covered the other three - cannot reach this form at all on a Mac. The forms are
therefore covered by a data-driven test that constructs each backend's settings,
builds the form and asserts that nothing was reported as `Unable to assign`.
Breaking one name fails that backend's row and only that row.

That test has a precondition worth stating rather than hiding: the Qt Quick form
factory is installed by the *QuickUi plugin*, so under a bare `-test Profiler`
`Core::createAspectForm()` falls back to the widget path and there is no QML to
have got wrong. It `QSKIP`s there with that reason, instead of passing while
asserting nothing. Under the usual `-load all` it asserts in full.

## QmlDesigner: compiled here, drawn somewhere else

"QmlDesigner is not buildable here" has been repeated in this log for several
batches, and it is not true of the file that matters. The plugin is gated on
`CONDITION TARGET QmlDesignerCore AND TARGET Qt::Svg` and is not built - its
dylib in the build tree is from July - but `settings/designersettings.cpp`
belongs to `add_qtc_library(QmlDesignerSettings STATIC ...)`, which *is* built.
The stale dylib was the evidence for the wrong claim: nobody had built the
plugin on this branch, which is not the same as nobody being able to.

So the page is ported. Eight groups, all list, plus the one thing the closure
*did*: a `PushButton` with an `onClicked` that cleared the controls style. That
is an `ActionAspect` now, and the label the layout supplied as a literal
(`"Controls style:"`) moved onto the aspect, where a form can find it.

**What can be verified here, and what cannot.** The plugin does not link, so the
page cannot be rendered, the census cannot reach it, and the tool-run technique
has nothing to run. What is left is static, so it was made to bite:

- It compiles: `ninja QmlDesignerSettings` rebuilds the file, and appending a
  call to an undeclared function fails it - checked, because a target that is
  "up to date" proves nothing on its own.
- `qmllint` passes, run directly against the file with `-I <build>/qml_modules`
  rather than through a `<Target>_qmllint` that does not exist here. Controlled
  twice: an unknown component and a property no delegate has, both reported.
- qmllint cannot see `aspects.Foo` - it is a property-map lookup - so the names
  were cross-checked against the container's settings keys and `setQmlName()`
  calls. All 27 resolve. Misspelling one reports it.
- And the port is complete rather than merely valid: the members the closure
  listed, compared against the ones the page draws, differ by exactly
  `resetStyle` - added, none dropped.

The QML module is declared on the plugin target inside `if(TARGET QmlDesigner)`,
not on the static library, even though that would have made it buildable here.
`QmlDesignerSettings` is a static library whose resources already need
whole-archive linking to register - there is a commented-out block about exactly
that beside it - and a QML resource that silently fails to register is the kind
of thing this machine could not detect. `qmldesigner.qbs` is a wildcard stub, so
it needs no edit.

## The census's own report was the thing not being asserted

Every census run has printed five lines of the form *"<page> does not draw:
<setting>"*, with a comment saying each had been checked against the closure it
replaced. Printed, not asserted - so a *sixth* line, from a future port that
dropped a control, would have appeared in a `QINFO` and changed nothing. That is
the same failure shape as a warning nobody reads.

Before asserting anything the five were re-checked, because an allow-list is
only worth what the checks behind it are:

- **GDB**'s `BreakOnThrow`/`BreakOnCatch` and **Valgrind**'s `CycleDetection`/
  `ShortenTemplates` were already justified here - the Breakpoints view and the
  Callgrind toolbar.
- **CVS**'s `LogCount` comes from `VcsBaseSettings`. The closure it replaced
  listed the binary path, the root, the timeout, the diff options and
  describe-by-id, and not this.
- **FakeVim**'s `TextWidth` is a vim option set with `:set tw=N`. The closure
  put `shiftWidth, tabStop, scrollOff, timeoutlen, cursorFlashTime` in a row and
  left this out.
- **Catch**'s `WarnEmpty` is labelled and has a tool tip, and the closure listed
  every other option and not this one. So it is shown by no UI, before the port
  as well as after - a pre-existing oddity rather than a migration one, which is
  worth knowing but is not this branch's to fix.

None of the five is a regression, which is the answer that lets the list be
closed. `knownUndrawnSettings()` now holds them with that reasoning attached,
and anything else fails the census by name: *"these pages lost a setting on the
way to Qt Quick"*. Deleting one delegate from `CvsSettingsPage.qml` fails it;
so does deleting an entry from the list, which is what proves the list is
carrying the five rather than the check being vacuous.

**Why this rather than ClangFormat.** The remaining closures are the widget
fallback and its tests - `CodeStyleAspect`'s, the `TabSettings` one that
`codestyleaspect_test.cpp` uses as the fixture for a language with no Qt Quick
form, the QuickUi fixture that tests the fallback itself, and Lua's `layouter`
binding, which two bundled extensions use. Removing the first two means porting
`ClangFormatSettingsEditor` - two more widgets inside a plugin that does not
build, link or run here, with no compile behind it. Tightening the census
protects every page that has already moved, and is checkable today.

## Auditing what the port lost, rather than what it moved

With the closures done, the question changes from "is it drawn" to "does it say
the same things". The census answers the first. Nothing answered the second, so
it was measured: every `setToolTip(Tr::tr("..."))` and `Layouting::toolTip()`
literal at the branch point, checked against the tree today. 383 of them, 362
still present, 21 gone.

Most of the 21 are rewordings that the audit cannot tell from deletions - "Local
directory path corresponding to the *analyis* path" became "analysis", "Choose
which kit settings to display by default" became "Choose which settings to
display for all kits by default". Each has to be looked at. Two survived that:

- **Cleanups Upon Saving** had `groupBoxStorageSettings->setToolTip(...)` in
  `behaviorsettingswidget.cpp`, and the text is now nowhere in the tree.
- **Autotest**'s path filter had "Apply path filters before scanning for tests."
  on the check box; `limitToFilter` today has a label and no tool tip.

The GitLab ones - "Add new GitLab server configuration." and its siblings - are
not counted as lost. Those buttons became an `AspectList`'s own Add and Remove,
which are labelled rather than icons, so the tool tip was saying what the label
now says.

**The first is not a missing string, it is a missing capability.** A `QGroupBox`
carries a tool tip; `AspectGroupBox` had nowhere to put one, and `GroupDelegate`
ignores `BaseAspect::toolTip()` on a nested container. So `AspectGroupBox` takes
a `toolTip` now, shown from the title - the one part of a group that is not some
delegate with a tool tip of its own, and the same place
`setShowToolTipOnLabel()` puts an aspect's.

`GroupDelegate` was left alone deliberately: no `AspectContainer` in the tree
sets a tool tip, so that half of the gap is theoretical, and a custom label
there would change how every generic group looks for no one's benefit.

Testing it needed the attached property rather than the property that feeds it,
or the test would only assert the alias it just wrote: the title carries an
`objectName`, and the test reads `ToolTip.text` off it. That needs the QML
context - `QQmlProperty(title, "ToolTip.text", qmlContext(title))` - because
without one the attached type does not resolve and the read comes back empty,
which looks exactly like a missing tool tip.

## Asking every delegate the same question

The tool tip audit was a floor, not a ceiling: it compares string literals, so
it finds text that vanished and not a control that quietly stopped showing what
it has. The complementary question is answerable at runtime - *does a delegate
show the tool tip its aspect carries?* - and nearly all of them do, which is
exactly what makes the one that does not a trap. A page sets a tool tip,
nothing complains, and nobody ever sees it.

Counted precisely, after the fix below: 27 delegates take an `Aspect` and 22
show its tool tip. The five that do not are `GroupDelegate`,
`InlineGroupDelegate`, `FlattenedGroupDelegate`, `AspectInlineListDelegate` and
`UnsupportedDelegate` - containers and a placeholder, none of which draws a
value, and no `AspectContainer` in the tree sets a tool tip anyway.

So the test builds one aspect of each kind, gives each a tool tip, renders them
through the **generic** form so the kind picks the delegate rather than the test
naming one, and asks each delegate whether the text is anywhere inside it.
`MultiSelectionDelegate` was the one that failed, and it has the tool tip now.

Three things this took, all of which looked like failures of the code:

- `findChildren<QQuickItem *>()` finds nothing useful. A Repeater's items are
  visual children of the delegate and QObject children of somewhere else, so
  the walk has to be over `childItems()`. The suite already had
  `findQmlComponents()` for this.
- Reading `ToolTip.text` off the delegate's *root* reported six failures, and
  all six were the test's fault: `BoolDelegate` is the check box itself and
  carries the tool tip on its root, while the delegates that wrap a control in
  a layout put it on the control. The question is whether the text is anywhere
  in the delegate, not where.
- `QQmlProperty(item, "ToolTip.text")` reads empty without a context. It needs
  `qmlContext(item)`, or the attached type does not resolve and the result
  looks exactly like a missing tool tip.

**`GroupDelegate` and the rest are deliberately not covered.** No
`AspectContainer` in the tree sets a tool tip and only one
`MultiSelectionAspect` exists at all - the fix here is for the next page, not a
bug anyone can see today. The test covers the value delegates, which is where
the evidence was.

## What the audit did not find

Worth recording, because a clean audit is a result and repeating it is waste.
Two other seams were measured and are sound:

- **The census's blind spot.** It skips aspects with no `labelText`, on the
  assumption that they were never meant for a form - and that assumption is
  what a closure supplying a literal label breaks. There are 75 such aspects.
  Every one inspected is session state (`LastTraceFile`, `LastShownPages`), a
  view or toolbar toggle (Git's log and blame options), a vim option, or a
  model-side aspect. No page lost a control this way.
- **Aspects configured nowhere.** 21 have no settings key, `qmlName` or label.
  All are either configured in a sibling file - `fontsettings.h`'s are the
  model behind the page, not the page's own - or dead flags that were dead
  before the branch too (`forceOpenLinksInNextSplit` was a plain member of
  `DisplaySettingsData` at the branch point).

## The two aspect properties that are not cosmetic

Asking every delegate the same question found a tool tip gap. The same question
about `enabled` and `readOnly` is worth more, because a delegate that ignores
those is not merely quiet - it lets a user change something the page says they
may not.

The survey came back clean: every delegate binds `aspect.enabled` except
`TextDisplayDelegate` and `UnsupportedDelegate`, which have nothing to disable,
and `readOnly` is handled everywhere a value is typed. All of `enabled`,
`visible`, `readOnly`, `toolTip`, `labelText` carry a NOTIFY, so the bindings
follow the aspect rather than sampling it once. Nothing to fix.

**What was missing was the end-to-end check, and both mechanisms are used
heavily.** `setEnabler()` is how a page says one setting only applies when
another is on; `setReadOnly()` has 36 production callers - a device's detection
log, a docker image's id, the effective qmake call. The widget renderer wires
both when it builds the control. Qt Quick binds instead, and a binding that is
never exercised is a claim, not a fact.

So two tests. The first turns an enabler on and off with the form open and
requires the dependent delegate's enabled parts to change both ways - pinning
`StringDelegate`'s three `enabled` bindings to `true` fails it with "turning the
enabler on left the form exactly as it was". The second counts the parts of a
delegate a user could type into: enabled, and carrying a `readOnly` property of
their own, so labels and layouts do not count. A read-only aspect must offer
none, and the editable case must offer some - without that second half the
assertion would pass whatever read-only did.

**A note on what these tests can express.** Neither asks "is *the* control
disabled", because there is no single control: `BoolDelegate` is its check box,
while the delegates that wrap one in a layout put the state on the inner item.
Counting parts is what survives that difference, and it is why the first test
compares two renders rather than reading one value.

## Does any of this build without WITH_TESTS?

Several plugins gained inline tests on this branch - AcpClient, Axivion, the
profiler samplers - and a settings form that is only listed when tests are on.
Nothing had checked that a build without `WITH_TESTS` still works, which is the
one way this work could break a release and not be noticed here.

Checked, for every production `.cpp` these commits touched: reuse the compile
command, drop `-DWITH_TESTS`, `-fsyntax-only`. Fifteen of sixteen clean.

**Controlled both ways, because a clean run means nothing on its own.**
Appending `#ifdef WITH_TESTS / #error / #endif` *and* a call to an undeclared
function: with the flag dropped only the undeclared function is reported, and
with it kept the `#error` fires too. That says the file really is compiled and
the define really is gone.

**The sixteenth was a false positive worth naming.** `acpsettings.cpp` failed
with a wall of errors - every one of them inside
`AcpClient_autogen/include/acpsettings.moc`, which was generated *with*
`WITH_TESTS` and still declares a test class that is no longer compiled. A real
build regenerates it. Any file with an inline test and an `#include
"<file>.moc"` will do this.

That led to a second question with a real answer: `acpsettings.cpp` includes its
`.moc` unguarded although its only `Q_OBJECT` is inside the test guard, while
the sampler and Axivion ones this session guarded theirs. Neither is wrong -
moc writes an *empty* file rather than failing when there is nothing to
generate, and 53 files in the tree are in exactly that position, `outputformatter
.cpp` and `dockerdevice.cpp` among them. Guarding it is a style, not a fix, so
nothing was changed to match.

What did change: `qmlprofilerplugin.cpp` was including `tests/samplerforms_test
.h` and `callstacksampler.h` outside the `#ifdef WITH_TESTS` block that holds
every one of its other test includes, and both are there only for the creator
registered inside it.

## What deleting a widget leaves behind

Removing `DockerDeviceWidget` left its forward declaration and a friend
declaration naming a class that exists nowhere. That is the residue this
migration produces every time a widget goes, and it is worth looking for
rather than waiting to trip over it.

**The test that works** is not "does this name have a definition" - a regex for
that misses export macros and multi-line class heads, and answered 735, nearly
all of them wrong. It is *"is every occurrence of this name a declaration"*,
which is exactly how the Docker one was confirmed: `grep` returned two lines and
both declared it. Counting occurrences and requiring all of them to be a bare
`class X;` or a `friend class ...X;` gives 43 names across the tree.

Three of those name a settings or config widget, had a definition at the branch
point, and have none now: `DebuggerSettingsPageWidget`,
`KitManagerConfigWidget`, `CompileOutputTextEdit`. They are gone.

**The other forty are not this branch's**, and are left alone: they are in
QmlDesigner, qmlpuppet and modelinglib, they predate the migration, and touching
dozens of unrelated files to tidy them would bury the change that matters.

**A C++ detail that turned into a build failure.** Deleting the forward
declaration but leaving `friend class Internal::KitManagerConfigWidget;` does
not compile: a *qualified* friend declaration needs the class to have been
declared already, while an unqualified `friend class X;` declares it itself. So
the two lines have to go together - which is what the Docker change did, and
what the scan output made easy to miss, because it lists where a name is
forward-declared and not every line it counted.

That failure is also the control. A dead declaration cannot be told from a live
one by removing it and seeing the build pass unless something proves the build
would notice; here `KitManagerConfigWidget` proved it by failing in three
translation units, and the other two removals then mean something.

## Where the branch's own goal actually stands

The branch is called `utils-drop-printsupport`, and that part is done:
`Qt::PrintSupport` is gone from `Utils`. `Qt::Widgets` is still a
`PUBLIC_DEPENDS`, and it is going to stay for a long time - 77 of the 337 files
in `src/libs/utils` include a QtWidgets header, and most of them are widgets in
their own right: `FancyLineEdit`, `DetailsWidget`, `CrumblePath`, `Wizard`,
`InfoBar`. None of that is settings pages.

The part that *is* this work's is the aspect core, and it is clean:
`aspects.h`, `aspects.cpp` and `aspectpresentation.h` reach no further than
QtGui. `QUndoCommand`, `QAction` and `QStandardItemModel` all look like widgets
and are not - they moved to QtGui in Qt 6, which is worth knowing before
counting them as a dependency.

`aspectlist.cpp` was the exception, and only because of leftovers. It carried a
`ColoredRow : public QWidget` that painted alternating row backgrounds, never
instantiated anywhere since the list moved to QML - the alternating colour is
`pres.rowBackground` in `TableDelegate` now. With it and three includes gone the
file is down to `QUndoStack`, and the aspect core no longer names a widget type
at all.

**Checked for more of the same, and there is none.** Sixty-six classes in
`Utils` derive from a widget type; `ColoredRow` was the only one nothing used.
That the scan sees all sixty-six is what makes the zero worth anything - a
scan run after the fix will always report nothing.

## A page that is never shown has nothing to measure

Three things were reported broken in the code style preview: no highlighting, a
selection that could not be deleted, and a Format button that did nothing. The
third was real and is fixed above. The first two do not reproduce - and the
interesting part is how long it took to be able to say that.

Every instrument pointed the wrong way at first. The preview reported
`highlighting=true` with the right mime type, `readOnly=false` and an enabled
aspect; the component tests for a `CodeBuffer` passed; deleting a selection
passed as soon as a test for it existed. Then reading the formats off the
viewport on a real page answered *zero ranges, zero colours*, which looked like
the reported bug and is not: `visibleLineCount` was 0 as well. **A page the
census creates but never shows never lays out**, so its viewport has no lines -
no colours to read, and no characters to click on either.

Shown, and driven with a press, a drag, a release and a Delete, the C++ page
answers 25 lines, 103 format ranges, 8 distinct colours, and removes exactly
the 11 characters the drag selected. That is now
`testAShownPreviewIsColouredAndEditsWithMouseAndKeyboard()`, and blanking the
mime type fails it with "Code Style draws its preview in one colour".

Two details worth keeping:

- The test drives the *mouse*, not `forceActiveFocus()` and a property write.
  That is what a test reaches for and it is not what anybody does - and the
  click taking the focus is precisely the step that would explain the report if
  it were broken. Asserting `hasActiveFocus()` after the click is the point.
- `qWaitForWindowActive()` returns false on this machine and the key events
  arrive anyway. Waiting on it only turns the test into a skip, which is worse
  than not asking: the precondition that matters is the item's focus, and that
  is asserted where it happens.

**What this does not settle.** The two symptoms are real to whoever saw them and
invisible to every instrument here. Nothing in this file explains them; what
would narrow it is whether typing a plain character into the preview works, and
whether the Snippets editor behaves the same way.

## The Snippets page opens empty, and always did

Surveying every editor a settings page draws, on screen, gives four that are
coloured and editable - the three Code Style previews (8, 9 and 6 distinct
foreground colours) and the Python language server page (4) - and one that is
not: **Snippets shows a single empty line**. Its editor follows the table's
current row, and a `TableView` starts with nothing selected.

That looks like the port losing something, and it is not. The widget page did
the same, for a reason visible in the order of two statements:

    loadSnippetGroup(m_groupCombo->currentIndex());

    connect(&m_model, &QAbstractItemModel::rowsInserted,
            this, &SnippetsSettingsWidget::selectSnippet);

The connection that selects an inserted row is made *after* the initial load, so
the rows that load puts in select nothing. Opening the page showed an empty
editor before this branch as well.

Two things worth keeping from it. The first is that the conclusion was worth
having before touching anything: "should a list page preselect its first row" is
a design question, and answering it by matching what the widget did is only
possible if what the widget did is actually looked up. The second is that the
lookup nearly gave the wrong answer - `git show "$BASE:path"` in zsh parses the
`:` as a parameter modifier, hands git only the rev, and prints the commit;
`git cat-file -s` then reports a few hundred bytes, which reads as "the file was
a stub back then". It was 552 lines.

**Not measurable here:** whether *switching* snippet group leaves the editor
blank, where the widget would have selected a row because by then the connection
exists. A scratch settings path has one snippet in it, so there is nothing to
switch between.

## Correction: project panels have their own census

An earlier version of this section claimed that nothing rendered a project
panel, that `CodeStyleProjectForm.qml` had never been drawn by anything, and
that fifteen other `*ProjectPanel.qml` files were in the same state. All three
are wrong, and the mistake was looking for panel coverage in the QuickUi suite
and concluding from its absence there that it was absent everywhere.

`projectpanelfactory.cpp` has `ProjectPanelFactoryTest`, with a
`PanelCensusProject` and `testPanelsThatSayWhatTheyShowRenderWithQuick()`. It
walks every factory, renders each panel that offers aspects, and checks more
than the page census does: that the container names a QML file, that the
`QQuickWidget` reports `Ready` and its source, that every delegate found its
aspect, that the panel is not disabled as a whole - and it collects QML
warnings through a message handler while doing it.

`CodeStyleProjectForm.qml` is covered too. It is a `Repeater` delegate over the
per-language forms, so it looked like something a project with no languages
would never instantiate; the census project has **three**, so the form is drawn
three times on every run.

A test added here on that false premise has been removed again. The general
lesson is the cheaper one: **before writing coverage for something, grep for a
test of it by name.** `grep -rn "ProjectPanelFactory" --include='*_test.cpp'`
would have found nothing, but the tests for it live in the production file
behind `#ifdef WITH_TESTS`, which is where 105 other classes in this tree keep
theirs.

**And the suite is not idempotent, which was there before this.**
`CodeStyleAspectTest` passes against a fresh `-settingspath` and fails on a
second run against the same one:

    fresh:  17 passed, 0 failed
    again:  15 passed, 2 failed   (testAQuickPageOffersTheStylesToDelegateTo,
                                   testPickingAStyleIsAnEditThatCancelReverts)

Both are about which style is selected, and a code style *pool* persists. So the
tests write state into the settings and then assert against defaults that state
has replaced. Verified to predate the test added here by removing it and
reproducing the same two failures.

That matters beyond tidiness: a scratch settings path is a test's habit and not
a developer's. Anyone running these against their real settings gets two
failures that have nothing to do with what they changed - and every "it passes
here" in this file was measured with `rm -rf` in front of it, which is what made
it invisible.

**Fixed by having the tests remove what they wrote.** `cleanupTestCase()` drops
the settings groups the six test factories can persist under - each language id
and the global `text` prefix, crossed with each `setSettingsSuffix()`. Three
consecutive runs against one settings path now pass; disabling the cleanup body
fails the second run again, which is what says the cleanup is doing the work
rather than something else having changed.

The general shape is worth keeping: **a test that applies settings has to
un-apply them**, and the way to find out whether it does is to run it twice
against the same `-settingspath` rather than once against a fresh one. Both
suites here now pass twice in a row, which is a claim none of the earlier runs
in this file were making.

**How far the problem goes: one suite.** QuickUi, TextEditor, Axivion,
AcpClient and Profiler were each run twice against one settings path.
QuickUi (86), TextEditor (203) and Axivion (3) repeat exactly. The other two
vary for reasons that are not persistence, and telling them apart is the useful
part: a settings leak fails the *same* tests on every rerun and keeps failing
until the path is wiped, while AcpClient's `testE2ePermissionGranted` failed
once in five runs on its test server terminating abnormally, and Profiler's
suite aborts on the `testMissingModelManager` QFATAL at a different point each
time, so a different set of tests gets to run at all. Re-sampling three times
separates the two.

## Where the page migration ends, counted

The census reports "aspect-driven pages: 108, rendered with Qt Quick: 108",
which says nothing about pages that offer no aspects at all - it skips those
before counting. Counting them too: **109 options pages, of which exactly one
is not aspect-driven.**

That is a count of what the census can *see on this machine*, and it is worth
saying so: a platform-gated page is never registered and so never counted.
`WindowsSettingsPage` ("Windows App SDK") returns early on
`!HostOsInfo::isWindowsHost()` and uses `setWidgetCreator`, so a Windows user
has a third non-aspect page that no run here can find. Any census of a
cross-platform tree run on one platform undercounts.

- ~~**Filters** (`D.Filters`) wraps `QHelpFilterSettingsWidget`~~ - done, see
  "Reimplementing a Qt widget" below. It is now an aspect page like any other,
  which is what moved the census from 107 to 108.
- **Browse** (`ExtensionManager.Browse`) is roughly 2,700 lines of
  `extensionsbrowser.cpp`, `extensionmanagerwidget.cpp` and
  `extensionsmodel.cpp`. It is an application view that happens to live in the
  settings dialog, not a list of settings.

So the settings pages are done, and the remaining `setLayouter` calls are the
four already accounted for: the widget fallback in `CodeStyleAspect`, the
`TabSettings` layouter that `codestyleaspect_test.cpp` uses as the fixture for
a language with no Qt Quick form, the QuickUi fixture that tests the fallback
itself, and Lua's `layouter` binding, which two bundled extensions use.

**What a next phase would be, measured rather than guessed.** There are 324
`.attachTo(` calls left in `src`, and almost none of them are settings:

      174  tool windows, editors, everything else
       91  dialogs and wizards
       22  not ours (the qbs submodule, layoutbuilder itself)
       20  settings-named files - the fallback path, the code style test
            fixtures, ClangFormat, perf's config widget, and a handful of
            pages that build a widget around something that is not an aspect
       17  panels and welcome pages, mostly the standalone profiler

That is a different and much larger job than replacing layout closures, and it
is not started.

## Re-checking this file's own numbers

Every count in the sections above was measured when it was written, and some of
them were then invalidated by later commits in the same session - which is a
quiet way for a record to go wrong, and worth catching once rather than leaving
for a reader to trip over.

Re-measured against the tree as it stands:

| claim | now |
| --- | --- |
| 66 widget-derived classes in `Utils` | 66 |
| 53 files whose only `Q_OBJECT` is inside `WITH_TESTS` yet include their `.moc` | 53 |
| 105 production `.cpp` files with an inline test class | 105 |
| 36 production callers of `setReadOnly()` | 36 |
| 324 `.attachTo(` in `src` | 324 |
| 109 options pages, two not aspect-driven | 109, two |
| 78 of 337 `Utils` files including a QtWidgets header | **77** |
| "24 of the 26 value delegates" show a tool tip | **27 take an aspect, 22 show it** |

The two that moved are both this branch's doing: `aspectlist.cpp` lost its
widget includes with `ColoredRow`, and `MultiSelectionDelegate` gained the tool
tip it was missing. Both are corrected in place above.

The one that was not merely stale is the delegate count, which was never
precise: "value delegate" was a category invented for the sentence rather than
something the tree defines. Counted by what the code actually says - a delegate
that declares `property Aspect aspect` - it is 27 and 22, and the five that do
not show a tool tip are the three group delegates, the inline list and the
placeholder. **A number worth putting in a document is one with a definition
behind it that someone else could re-run.**

## Controlling the assertion everything else rests on

Every "107 of 107" in this file comes from one test, and one of its assertions
covers all of them at once: `qmlWarnings.isEmpty()`, which fails the census if
any page reports a QML warning while being built. Only the assertion added most
recently had ever been controlled, so the linchpin was taken on trust.

It bites. A `visible: noSuchThingAtAll` added to `CvsSettingsPage.qml` fails the
census with the file and the line - and a second test catches it independently,
which is more coverage than expected.

**And a caveat in its own comment turned out to be wrong.** The comment said a
second test building the same pages "sees nothing, the warnings having already
been reported", which would mean the collection depends on this test being
declared before every other one that builds a page - a property nobody would
notice breaking. Adding a slot *before* the census that builds every page, the
census still caught the error. `QQmlEngine` reports a binding error when the
binding is **evaluated**, and a component fetched from the cache evaluates its
bindings again on every instantiation.

So `clearComponentCache()` was written to make the collection
order-independent, and then removed again: with it and without it the census
catches the same thing, which makes it mechanism nobody earned. The comment is
corrected instead, since what was actually wrong was the belief, not the code.

## The editor's next increment: a gutter

The viewport draws text, a caret, a selection and highlighting, and that is
enough for a preview. It is not enough for the main editor, and the first thing
missing is the one that makes a viewport look like an editor rather than a text
box: the line numbers.

`EditorGutter.qml` is one item per line **on screen**, not per line in the
document - the same rule the viewport follows, and what keeps a million-line
file the same price as a short one. Two properties had to come out of the
viewport for it to be possible at all:

- `lineCount`, so the gutter can be as wide as the highest number the file
  reaches. Sizing to the numbers currently visible would make the gutter change
  width while scrolling, which is the kind of thing nobody writes down as a
  requirement and everybody notices.
- `font`, the *zoomed* one the text is laid out with. Numbers measured in a
  different font do not sit on the rows the text is on, and the zoom trap here
  is the one already recorded above: `font()` is unzoomed and `lineSpacing()` is
  not.

The row a number sits on is `line * lineHeight - scrollY`, deliberately the same
arithmetic the viewport uses rather than an offset from the top of the frame.
That is what makes the numbers stay on their lines at a scroll offset that is
not a whole number of them. Making it frame-relative instead - `index` rather
than `firstVisibleLine + index` - fails the test with "Actual: 1, Expected:
1001" after a scroll, which is exactly the bug that shape has.

The gutter is part of `CodeViewport` rather than something an editor assembles
beside it, behind `showLineNumbers`, off by default. A settings preview is a few
lines of demonstration and numbering them says nothing; an editor without them
is not one. Hidden, it takes no width either - the text starts where it would
have without a gutter rather than indented by an invisible one, which is the
half of "hidden" that is easy to leave out and the test checks separately.

**Still missing before this is the main editor**, in rough order of how much
each is load-bearing: an `IEditorFactory` so a file can be opened in it at all,
the current-line highlight, folding, text marks and annotations, extra-selection
overlays, the context menu, drag and drop, and wrapping - which needs a height
cache first, because uniform line height is what makes everything above
arithmetic instead of a search.

## A file can be opened in it

The viewport had everything an editor needs to *show* a file and no way to be
given one. `QuickTextEditor` is a plain `Core::IEditor` - not a
`TextEditorFactory`, which is built around `TextEditorWidget` and would have
had to be taken apart first - holding a `TextDocument` and a `QuickWidget` over
`MainEditor.qml`.

The seam worth naming is **who opens the file**. The editor manager creates the
editor and *then* opens its document, so the form has to show a document it did
not open. `CodeDocument` opens a file and `CodeBuffer` holds text of its own;
neither fits, so `AdoptedSource` is a third `CodeSource` that only points at a
document somebody else owns. That is also what the test checks: the
`TextViewport`'s source and the editor's `document()` have to be the same
object, not merely have the same contents.

**It is offered, not imposed.** `addMimeType(text/plain)` puts it in Open With
for any text file, and `setupQuickTextEditor()` is called *after*
`setupPlainTextEditor()` because the default for a mime type is the first
factory that claims it. That ordering is a one-line accident waiting to happen,
so it is a test rather than a comment: registering this one first fails with
"the unfinished editor is what a text file now opens in".

`TextEditor` gains a `QtcQuick` dependency, which it had avoided so far. It is
earned now - the plugin ships a Qt Quick editor - and there is no cycle, since
`QtcQuick` depends only on `Utils`.

**What it is at this point:** text, caret, selection, mouse and keyboard
editing, highlighting, indenting, scrolling, line numbers, the current-line
highlight, text marks and what they say, all O(visible). No extra-selection
overlays, no context menu, no drag and drop, and no wrapping - which still
wants a height cache first. Folding came next and needed no cache at all; see
"Folding, which Qt had already solved".

The current-line bar reuses `cursorRectangle`, which the caret already reads, so
there is no second answer to "which line is the caret on". Two things it had to
get right, and one of them the obvious test missed:

- The colour is `C_CURRENT_LINE`'s **brush**, transparent when the scheme sets
  none. `QBrush().color()` is opaque black, so a scheme that asks for no
  highlight would otherwise get a black bar across the line the caret is on -
  the same trap already recorded for the editor background, hit a second time
  because `texteditor.cpp` takes `.color()` off this one directly.
- `cursorRectangle` is in the *viewport's* coordinates and the viewport is inset
  by a margin, so the bar has to be moved by the same inset. Asserting which
  line the highlight is on does **not** catch forgetting it: a four-pixel error
  in a fifteen-pixel line still rounds to the right line. Comparing the
  highlight's offset to `cursorRectangle().y()` exactly does - 24 against 28.

That is a general point about testing a position: rounding an answer to the
thing it is *for* hides errors smaller than the thing. Assert the number.

## Marks, and the update nobody sends

An error, a warning or a breakpoint is a `TextMark` on the document, and the
gutter is where it shows. Per visible line the viewport asks `marksAt()`, keeps
the highest-priority visible one - the same slot the widget gutter gives them -
and hands QML a URL through `QtcQuick::iconUrl()`, because a `QIcon` cannot
cross into QML on its own.

**Marks arrive without the text changing**, which is the whole difficulty: an
error appears while the file sits there, so `contentsChanged` says nothing. The
widget gutter is redrawn from `TextDocumentLayout::updateExtraArea`, so the
viewport listens to the same signal - and that is not enough:

`TextDocument::addMark()` only calls `requestExtraAreaUpdate()` when the
document already had marks. The **first** mark takes the other branch,
`scheduleUpdate()`, because the layout has to make room for a gutter it did not
have before - and that emits `QAbstractTextDocumentLayout::update`, not
`updateExtraArea`. Listening to only the obvious signal draws every error except
the first one in a file.

That was found by running the suite **twice**, which is now the habit for a
different reason: the test passed the first time and failed the second. Nothing
had leaked - the first run simply happened to relayout for its own reasons
between the mark being added and the assertion. A test that depends on
something else happening to do the work is not flaky by accident, it is a bug
report that has not been read yet.

**qmllint earned its place in the loop again.** The mark delegate first called
its per-line data `data`, which is `Item`'s *default property*: the
`HoverHandler` beneath it was being assigned into that property rather than to
the item. It compiles and it draws; qmllint reported both the shadowing and the
duplicate binding.

## An invokable has nothing to notify on

A mark's message is drawn after the text of the line it is about, starting where
the text actually ends - the viewport already reports each line's natural width,
so it is not a column somebody has to keep in step.

Writing it turned up a bug in what was committed the day before. Both the marks
and the annotations were read by delegates calling
`viewport.visibleLine(index)`, and **an invokable has no signal**: a binding on
one is evaluated when the delegate is built and never again. The gutter icons
had exactly the same defect and the tests had not caught it, because they
asserted `visibleLine()` from C++ - which was always right - rather than asking
whether anything was drawn.

What is on screen is a property now, `visibleLines`, with a `linesChanged`
emitted at the end of `updatePolish()`, and the delegates take `modelData` from
it. Not emitting it fails both the annotation test and the gutter one; before
the change, the annotation was simply never drawn while every C++ assertion
about it passed.

**The lesson is about where an assertion sits, not about QML.** Checking the
value a component is given tests the component's input. Only looking for the
thing on screen tests the component. The first is much easier to write and
passes in cases the second catches - so when both are cheap, the second is the
one worth having.

## Folding, which Qt had already solved

Folding was the piece the viewport was written *around*: the class comment said
so, because `line = scrollY / lineHeight` and `findBlockByNumber(line)` both
assume every block is a line, and a folded one is not. The fear was that fixing
it meant a height cache - the same thing wrapping wants - and that adding it
carelessly would turn scrolling into a walk over the file.

None of that was needed. `QTextDocument` already keeps a per-block *line count*,
and folding sets it to zero: `PlainTextDocumentLayout` does
`setBlockLineCount(block, block.isVisible() ? 1 : 0)` in four places, and
`doFoldOrUnfold()` does it directly. So the document maintains, in its own
fragment tree, exactly the two answers the viewport needed:

- `document()->lineCount()` - how tall the document is *now*, folds excluded.
- `document()->findBlockByLineNumber(n)` - which block is drawn on row `n`,
  in a tree lookup rather than a walk.

The whole change is using those instead of `blockCount()` and
`findBlockByNumber()`, plus advancing to the next *visible* block at the end of
the loop. It stays O(visible) with folds exactly as it was without them, and
the height cache is still only wrapping's problem.

**Three counts that used to be one.** They are now separate, and each has a
distinct job, which is worth stating because the old code could not tell them
apart:

| | what it is | who needs it |
|---|---|---|
| `lineCount` | `blockCount()` - every line in the file | the gutter, to size itself for its highest number |
| `contentHeight` | `lineCount()` lines - what is not folded away | the scrollbar |
| `visibleLineCount` | rows laid out | tests, and the delegates |

**And a fourth: what a row is called.** The gutter used to number a row by
arithmetic, `firstVisibleLine + index + 1`, which is right only while the two
counts agree. It cannot be computed on the QML side at all once they do not -
only the viewport knows which block a row came from - so each line now carries
its `lineNumber` and the gutter prints that. With four lines folded under line
1, the numbers read `1, 6, 7` and the rows are still adjacent.

**Nothing new listens for it.** Folding arrives on the connection already there
for the first text mark: every caller of `doFoldOrUnfold()` follows it with
`requestUpdate()`, which is `QAbstractTextDocumentLayout::update`. Worth
knowing, because that connection reads as mark-specific and is not.

**Four controls, because there are four mechanisms.** Height from the wrong
count, the wrong lookup at the top, no skip when advancing, and the gutter's
arithmetic each break the test at a different assertion. That mattered here:
one control's *restore* silently failed - collapsing the do-while left
`block = block.next();` matching in two places - and the next control ran on top
of the still-broken file, so its failure proved nothing. A control is only
evidence if the baseline passes again afterwards, which is now the last step.

**The marker, and the click.** The gutter grew a third column, hard against
the text where the widget puts it, holding `Utils::Icons::EXPAND` or
`COLLAPSE` - a boxed plus or minus - on the lines that start a fold. The widget
draws this through `QStyle::PE_IndicatorBranch` with per-style corrections for
Windows, GTK, Oxygen and Mac, which is the `QStyle`-leaks-into-painting note
elsewhere in this document; a Creator icon has none of that and themes itself.

A tap calls `toggleFold(lineNumber)`, which repeats what
`TextEditorWidgetPrivate::toggleBlockVisible()` does, including the two parts
that are easy to leave out:

- **Wait for the highlighter.** Folding indents are its output, so folding
  while it is still running folds the range it had worked out so far. It looks
  the line up again afterwards rather than keeping a `QTextBlock`, so whatever
  the highlighter did to the document in between does not matter.
- **Move the caret out.** A caret left inside what was just folded would type
  into text nobody can see.

**One fold is not enough to test a fold.** The first version of the click test
folded line 1 - and at the top of an unfolded file a line's number and its row
are the same number, so the control that made the marker fold *the row it sits
on* passed. Two folds, with the second clicked after the first is closed, makes
line 10 sit on row 5 and the control bite.

**A test that could not see its own icons.** The markers loaded nothing: the
`image://qtcreator/` provider is on Creator's shared engine, and a `QQuickView`
made in a test has its own. `Image.visible` was bound to `source !== ""`, a
string test, so a marker pointing at nothing was still visible and still
clickable, and every assertion passed. The provider is now installed on every
view the suite makes, and the test asserts `Image.Ready`. The symptom had been
in the log for several batches as `Invalid image provider`.

**And a collector that caught too much.** `QmlComplaints` installed a message
handler and kept every warning at or above `QtWarningMsg`, so a
`QUnifiedTimer::stopAnimationDriver` from an unrelated window teardown failed a
test about bindings - once, which is how these get ignored. The obvious fix,
filtering on the `qml` logging category, is worse than the bug: QML's runtime
warnings are logged with *no* category, so the filter silences exactly what the
test is listening for. The control - a `ReferenceError` in the watched
component - passed happily with the filter in place, which is the only reason
it was not committed. It listens to `QQmlEngine::warnings` now, which is the
thing it always claimed to be listening to.

**Saying what was swallowed.** A fold with no marker beside the text is a gap
the reader has to spot in the line numbers, so the widget draws `{...};` in a
rounded box after the folded line - and the brackets are not decoration: they
come from the *hidden* text, the opening one from the first hidden line and the
closing one from the last, semicolon and all.

That string logic sat inside `TextEditorWidgetPrivate::paintReplacement()`,
mixed with the painting. It is now
`TextBlockUserData::foldReplacementText(ellipsis, firstHidden, lastHidden)` and
both backends call it. It takes the two blocks rather than working them out,
because *who counts as hidden* differs: the widget uses its own
`nextVisibleBlock()`, which also skips blocks hidden in that view alone - the
unchanged lines an inline diff collapses - while the viewport only knows about
the document's own visibility. Extracting that walk too would have quietly
changed the diff editor.

**One Repeater, not two.** The replacement and a mark's annotation both start
where the line's text ends, so as separate Repeaters they drew on top of each
other on a folded line with an error on it. They are one delegate now, a `Row`,
which makes the overlap impossible rather than unlikely. That moved the
annotation into a parent, and an existing test asserting its local `x` went red
- correctly, because a local `x` had silently stopped meaning "after the text".
Both tests ask `mapToItem(viewport, ...)` now, which is the question either way.

**The caret walked into the fold.** `QTextCursor::Down` moves by *document*
line, so pressing Down on a folded line put the caret inside the hidden text,
where nothing draws it. The fix is one line - `cursor.setVisualNavigation(true)`
in `textCursor()`, which is what `moveCursorVisible()` uses on the widget side.
Worth recording because the instinct is to write the skip loop by hand: Qt
already has the flag, and it applies to every move rather than to the two the
loop would have covered.

This one needed no separate control: the test was written first, watched fail
on the unmodified viewport, and went green on the one-line change.

**Two ways to open a fold, and a z-order.** The gutter marker is far from what
the reader is looking at, so the `{...};` box opens the fold too, the way the
widget does. Making that work meant moving `CodeViewport`'s text-selection
`MouseArea` from *after* the viewport to *before* it. Declared after, it is a
sibling drawn on top of the whole viewport subtree, so it took every press
before anything inside the viewport could - including a `TapHandler`. Declared
before, it is the fallback it was always meant to be: `TextViewport` accepts no
mouse buttons itself, so a press landing on none of its children still reaches
it. The ordering is load-bearing now, so it has a control of its own - moving
the `MouseArea` back on top fails the test.

**On running controls.** Two restores failed this session. One collapsed a
`do`-`while` and left `block = block.next();` matching in two places; the other
tried to put a deleted block back by locating a comment whose text wrapped
differently than assumed. Both times the *next* control ran against a still
broken file and its failure proved nothing. Controls copy the file aside and
copy it back now, and the baseline is re-run at the end - a control is only
evidence if the thing passes again afterwards.

## Keyboard navigation, which was already written

The viewport's key handling was a `switch` on `Qt::Key_Left`, `Key_Home` and
friends with a Shift check. That is wrong twice over: there was no word
movement and no way to reach the ends of the file, and the keys it did handle
mean different things on different platforms. On macOS `Home` is
*MoveToStartOfDocument*, not start of line - so the Quick editor's Home did
something the rest of the machine does not.

`Utils::MultiTextCursor::handleMoveKeyEvent(event, camelCase, layout)` already
does all of it and, crucially, **takes no widget**: it is in `Utils`, it works
off a `PlainTextDocumentLayout`, and the widget editor is only one of its
callers. Handing the event to it deleted the switch and gained word movement,
document and block ends, every `Select*` variant, camel-case stepping (a
behaviour setting, so it follows the user's preference) and the platform's own
chords. Two backends, one answer to what a key means, by construction rather
than by discipline.

Pages stay local: they depend on how tall the view is, so they are not in the
table.

**Home is not column zero.** Creator's Home goes to the first thing on the
line, and only to the margin once the caret is already there. That has to be
intercepted *before* the shared table, which would take the same key - the same
order `TextEditorWidget::keyPressEvent()` uses.

Mirroring `handleHomeKey()` exactly turned on one line. The widget's loop
checks `if (pos == initpos) break;` **after** incrementing, which means a caret
sitting *inside* the indentation goes to the margin rather than forward to the
code. Written the natural way - count the whitespace, then compare - the caret
goes forward instead. Both behaviours are defensible; only one is Creator's.
The test pins all four starting points (in the code, at the first character, at
the margin, inside the indentation), because only the last one separates them.

**A test that was asserting the platform.** `QTest::keyClick(view, Key_End)`
had encoded "End means end of line", which is Windows and Linux behaviour. It
went red on this change, correctly. Tests type what a move *means* now -
`QTest::keySequence(view, QKeySequence(QKeySequence::MoveToEndOfLine))` - which
is the same question on every platform.

**And a guard that stopped being needed, then was needed again.**
`setVisualNavigation(true)` in `textCursor()` was the fix for the caret
stepping into a fold. After the shared table took over, its control stopped
biting: `handleMoveKeyEvent` sets the flag itself. That leaves two options -
delete the line, or find what it still covers. It covers pages, which are the
one move that stayed local. Making the control bite again needed a fold
**deeper than a page**: a page that clears the fold lands on a visible line
whether or not it counted the hidden ones, so the four-line fold the other
tests use could not tell the two apart. See [[negative-control-two-guards]] -
this is the same shape from the other side, a guard that looks redundant until
you find the one path the other guard does not cover.

## The IEditor hooks, which are most of what an editor is *for*

`QuickTextEditor` implemented two virtuals: `document()` and `toolBar()`. That
is enough to show a file and nothing else. Everything that *sends* a reader
into a file - a search result, a compiler error, go-to-definition, the locator,
`F4` - is `EditorManager` calling `IEditor::gotoLine()`, and an editor that
does not answer opens at the top. It looks like those features are broken
rather than like the editor is unfinished.

Now implemented: `currentLine()`, `currentColumn()`, `gotoLine()`,
`saveState()`, `restoreState()` and `duplicate()`. Three details that are not
obvious from the signatures:

- **Column zero means the line, not the margin.** `gotoLine(50)` with no column
  lands on the first thing on line 50, skipping its indentation - which is what
  a compiler message points at.
- **Going to a line inside a fold opens the fold.** Someone who asked to go
  there asked to see it. The walk that opens every enclosing fold came out of
  `TextEditorWidget::ensureBlockIsUnfolded()` into
  `TextBlockUserData::unfoldTo()`, so both editors do it the same way; the
  highlighter wait stays with each caller, since only the widget has one.
- **`duplicate()` shares the document.** A split view is two editors on one
  document; opening the file twice would give two documents that do not see
  each other's edits.

`saveState()` keeps the caret *and* the scroll offset. Coming back to a line
that happens to be one row off the bottom is not coming back.

**Four vacuous assertions, found by controlling all five at once.** Only one
control bit on the first run. Two of the misses were the harness, not the
tests: the control loop piped `ninja` to `/dev/null`, so a control that broke
the build left the *previous* binary in place and the suite passed green
against code that no longer existed. This is
[[cmake-redgreen-ninja-regen-trap]] wearing a different hat - **never hide the
build inside a control loop**; print errors and bail.

The remaining miss was real and more interesting: the test asserted that
`gotoLine(50, 0)` lands on column 5, using a file of unindented lines. On an
unindented line the code and the margin are the *same position*, so the
assertion could not fail. Indenting the fixture is the whole fix. The general
form: **an assertion about skipping something needs a fixture that has
something to skip.**

## Find, and extra selections without QtWidgets

`Core::BaseTextFindBase` asks its subclass for five things - a cursor, a way to
set one, a `QTextDocument`, whether it is read-only, and a widget to anchor the
find bar to. **None of them requires the editor to be a widget**, so Ctrl+F in
the Quick editor is a ~60-line subclass over the viewport. It is found the way
every find is: `Utils::Aggregation::aggregate({widget, find})`, on the
*widget*, because that is what `CurrentDocumentFind` queries.

**Extra selections, without `QTextEdit`.** Showing every match needs ranges of
the document drawn differently - what the widget calls extra selections. The
type for those, `QTextEdit::ExtraSelection`, is QtWidgets, which is the thing
being removed. `TextViewport::Highlight` is the same idea as
`{start, end, QTextCharFormat}`, keyed by `Utils::Id` the same way so a source
can replace its whole set without touching anyone else's, and merged into
`QTextLayout::formats()` under the selection - a match the reader has selected
should still look selected.

Each set is kept sorted by start and found with `lower_bound`, so highlighting
every match in a large file stays O(what is on screen). Highlighting ten
thousand matches over fifty visible lines is otherwise half a million
comparisons per relayout.

**`C_SEARCH_RESULT`'s format is deliberately almost empty.**
`FontSettingsData::toTextCharFormat()` treats it as an *overlay* category and
drops its foreground, because the widget editor paints search results as an
overlay rather than as a format - which is also why `TextEditorWidgetFind` sets
`setResultHighlightingEnabled(false)`. Reading that format straight and using
it as a format range produced ranges with an **invalid brush**, and Qt drops a
no-op format range, so the highlight silently did nothing. The fix takes the
same colour the overlay takes (`background().color().darker(120)`) and falls
back to the theme when the scheme names none: a scheme that forgot a search
colour must still show the reader where the matches are. That is the "an unset
brush is not black, and not nothing either" trap for the third time in this
document.

**The find test found a real bug.** `supportsReplace()` came back false, which
is `!isReadOnly()` - and `MainEditor.qml` had never set `readOnly`, so the
whole Quick editor was a *viewer*. Every editing test up to that point had
driven a viewport it configured itself. The editor now sets `readOnly: false`
and the viewport refuses edits to a file the filesystem will not take back
(`IDocument::isFileReadOnly()`); the widget's offer to make it writable is not
there yet, but the refusing half must not be missing.

**Two harness lies in one afternoon.** After the `/dev/null` build, this:
`-test A -test B` on one command line runs **nothing at all** - no output, exit
0 - so all five controls came back clean. Any control harness needs a positive
check that the tests ran (grep for `Start testing`), not just an absence of
failures.

**And one control that correctly did not bite.** Overriding `clearHighlights()`
to clear the viewport was redundant: `BaseTextFindBase::clearHighlights()` is
`highlightAll(QString(), {})`, which already routes through the same slot and
clears. The override was removed rather than kept "for safety" -
[[negative-control-two-guards]] cuts both ways, and an unearned mechanism is
one more thing that can drift.

## Preferences never reached the open editor

A `TextDocument` does not read the global font settings; they are **pushed**
into it, and until now only `TextEditorWidget` did the pushing
(`connect(&globalFontSettings(), &FontSettings::changed, document, ...)`). So a
Quick editor kept the font, the size and the colour scheme it opened with,
forever. Changing the theme in Preferences changed every other editor and not
this one.

The viewport does it now, for whatever document it is shown - which covers the
editor and every settings preview with one connection - plus a push when the
document changes, so a file opened *after* a zoom opens at that size.

That needed two separate connections and they fail differently, which is why
they have separate controls:

- `globalFontSettings().changed` -> push into the document. Without it nothing
  arrives at all.
- `TextDocument::fontSettingsChanged` -> `polish()`. Without it the settings
  arrive and nothing lays out again.

## The pointer, and what it can no longer do by accident

Double click takes the word, triple click takes the line, Ctrl and the wheel
zoom. Three notes:

**Qt reports one double click, never a triple.** The third click of a triple
click arrives as a *plain press*, so recognising it has to happen in
`onPressed` - before the press collapses the selection the second click made -
gated on a `Timer` started by `onDoubleClicked`. Writing it in
`onDoubleClicked` looks right and never fires.

**`Qt.styleHints` is untyped to qmllint.** It answers `Member
"mouseDoubleClickInterval" not found on type "QObject"`. `Application.
styleHints` is the same object through a typed singleton and lints clean.

**Zoom is global, not this view's**, matching the widget: every editor grows
together. It honours `scrollWheelZooming`, which a user can turn off, and the
setting is checked in C++ where the QML cannot forget it.

**Three of six controls missed, all for the same reason.** Not the harness this
time - the *fixtures* could not express the behaviour:

- the whitespace refinement in `selectWordAt()` needs **two adjacent spaces**;
  with a single space the ordinary `WordUnderCursor` answer is already right;
- trimming `BlockUnderCursor`'s reach back over the preceding newline needs a
  triple click on the **second** line; on the first there is no newline above
  to reach back over;
- pushing the font settings on *document change* needs a file opened **after**
  a zoom; the `changed` connection covers every other case.

Same shape as the unindented `gotoLine` fixture: an assertion about handling a
special case needs a fixture containing that case. Worth checking for
deliberately, because each of these tests reads as though it covers the
behaviour it names.

## A menu is a list of QActions, so list them

Right-clicking did nothing in the Quick editor. The widget builds its menu with
`QMenu menu; appendMenuActionsFromContext(&menu, M_STANDARDCONTEXTMENU);
menu.exec()` - which is three lines because the *contents* are not its
business: Qt Creator's menus are `QAction`s assembled by the `ActionManager`
out of every plugin that wants a say, and no port should reinvent that.

So the port is not "write the menu", it is **"expose the actions"**.
`QtcQuick::ActionModel` is a `QAbstractListModel` over a `QList<QAction *>`:
text, shortcut, enabled, visible, checkable, checked, separator, icon. It is in
`QtcQuick` rather than in `TextEditor` because every widget menu still to be
ported needs exactly this, and there is nothing editor-specific in it.

Three things it has to get right:

- **Follow `QAction::changed`.** Enabled, checked and even *text* change while
  a menu is open - a command's text says what it will do next ("Add UTF-8 BOM
  on Save" / "Delete UTF-8 BOM on Save"). A model that reads them once shows
  the wrong thing.
- **Ask again when the menu opens.** An `ActionContainer` gains entries as
  plugins register them, so the model takes a *provider* rather than a list and
  `refresh()` re-reads it. A list taken once at construction is right only
  until it is not.
- **Prefix every role name.** `MenuItem` already has `text`, `enabled`,
  `checkable` and `checked`, and a delegate cannot declare a `required
  property` that shadows one - qmllint says `shadows final member`. The roles
  are `actionText`, `actionEnabled` and so on, and the delegate assigns them
  across.

**A separator is an entry, not a type.** A `Repeater` delegate is one component,
and `Menu` treats its `MenuItem`s specially enough that swapping in a
`MenuSeparator` per row is not worth it - so a separator is a `MenuItem` with
no text, disabled, with a rule drawn through it. That needs a **`Binding on
implicitHeight`** with `restoreMode`, not a ternary: the other arm of the
ternary would be "whatever the style says", and there is no way to write that.
`implicitHeight: separator ? x : undefined` compiles, runs, and prints
`Unable to assign [undefined] to double` twice per menu - a warning, so a green
test says nothing about it. Found by reading the run's warnings, again.

**Right-click keeps a selection.** Clicking inside one acts on it, so the caret
only moves when the click lands outside - which is what every editor does and
what makes "Cut" mean anything.

**And one control that correctly did not bite.** `ActionModel::trigger()`
checked `isEnabled()` before triggering; removing the check changed nothing,
because `QAction::trigger()` on a disabled action is already a no-op. The check
went, the *assertion* stayed - a disabled entry doing nothing is worth pinning
whoever guarantees it.

**The qbs step needs its own control.** `qbs resolve` on this tree reports
errors from the vendored `src/shared/qbs` (`qbscore` is disabled here), so
"there were errors" and "my product is broken" look alike. Adding a
`"nosuchfile.cpp"` to the product and confirming
`qtcquick.qbs:11:12 File ... does not exist` is what proves the resolve is
reading the thing that changed. See [[qbs-sync-means-resolve]].

## The test that should have been written first

Nine commits into porting the editor, nothing had ever asserted that **typing
in it changes the file on disk**. Every editing test so far drove a viewport it
had configured itself; none of them went through `EditorManager::openEditor`,
typed a key into the form, and then saved. It does hold - `document->save()`
writes what was typed - but that was luck rather than knowledge, and it is the
one thing on which everything else here is worthless.

Worth the habit: for any port, the *first* test is the end-to-end one that
names what the thing is for. The interesting tests come after.

**`QVERIFY2` evaluates its message whether or not the condition held.** So
`QVERIFY2(result.has_value(), qPrintable(result.error()))` aborts the process
on **success**, because `Result::error()` asserts on a value. Use
`if (!result) QFAIL(qPrintable(result.error()));`.

## A drag that leaves the editor

Dragging a selection below the bottom edge stopped at whatever happened to be
on screen when the drag started - which on a real file is a long way from what
was wanted. A `Timer` now keeps scrolling and re-extending while the pointer is
outside, a line per tick and up to five the further out it is.
`positionAt()` already clamps to what is laid out, so after each scroll it
answers with the line that has just arrived - the auto-scroll needed no new
mapping, only something to keep asking.

**"It stopped" has no event to wait for.** The obvious assertion after
releasing the button is that the scroll position stays put, and
`QTRY_COMPARE(viewport->scrollY(), restingAt)` where `restingAt` was just read
from `scrollY()` passes instantly and forever - a control that left the timer
running did not touch it. What is true immediately is the *timer's* state, so
the test gives the `Timer` an `objectName` and asks `running`, having first
asserted it was running before the release. Assert the diagnostic, not the
outcome.

**A third harness lie: the stale binary.** One control reported clean and bit
when re-run by hand with the identical command. `ninja` had not relinked, so
the suite ran the previous `libTextEditor.dylib`. Grepping the build for
`error:` does not catch this - there was no error, there was no *build*. The
loop now records the plugin's mtime before and after and prints
`DID NOT RELINK` instead of running. Together with the earlier two - the
`/dev/null` build and `-test` twice - the rule is: **a control harness must
prove it built, prove it ran, and prove the baseline still passes.**

## Every file was grey

Ten commits in, the Quick editor had never highlighted anything. `.cpp`,
`.json`, `.py` - all grey text. The tests did not catch it because every
highlighting test so far installed a highlighter *itself* and then checked it
reached the scene graph, which it does.

The cause is a factory feature. `PlainTextEditorFactory` says
`setUseGenericHighlighter(true)`, and `TextEditorFactory::createEditor()` turns
that into `setupGenericHighlighter()` - a connection to `filePathChanged` that
sets the mime type and installs a `Highlighter` with the definition
KSyntaxHighlighting found. A `Core::IEditorFactory` that builds a `TextDocument`
directly gets **none of it**, and nothing complains: a document with no
highlighter is a perfectly valid document.

The Quick editor now does the same three things when the path arrives:

    document->setMimeType(mimeTypeForFile(path, MatchDefaultAndRemote).name());
    definitions = HighlighterHelper::definitionsForDocument(document);
    document->resetSyntaxHighlighter([definition] { ... new Highlighter ... });

And folding came free with it. The generic `Highlighter` sets folding indents
from the folding regions in the definition, so a real file is foldable without
anyone saying where - which the earlier folding tests could not have shown,
because they set the indents by hand.

**Two things the controls said about this.**

`setMimeType()` looked redundant: removing it changed nothing, because
`definitionsForDocument()` tries the *file name* before the mime type, and
`TextDocument::open()` sets the mime type itself. It is load-bearing exactly
once - on **rename or Save As**, where the path changes without a reopen. So
the test renames the document and checks the mime type followed, which is both
what makes the line earned and the behaviour a user would notice when saving a
`.txt` as a `.py`.

The other was a guard that fired wrongly. The test tried to assert "the mime
type is about to change" *before* renaming, but the reconfigure runs
synchronously inside `setFilePath()`, so by the time anything could be read it
had already changed. The check that the fixture is not vacuous belongs on the
two file *names*, not on the document's state: `QVERIFY2(expectedForPy !=
expectedForJson, "both names have the same mime type, so renaming proves
nothing")`.

**And the warning that named it.** The first version called
`configureHighlighter()` in the constructor, when there is no path yet, and
asking the mime database about an empty path prints
`QFSFileEngine::open: No file name specified` once per editor. The call is
still needed - a duplicate's document already has a path - so it returns early
on an empty one.

## Preferences, the rest of them

The font settings were one of *seven* global containers the widget editor
pushes into its document. The other six were still missing, and the pattern is
identical every time: a `TextDocument` reads none of them itself, so an editor
that does not push them behaves differently from every other editor in Creator
and says nothing about it.

The ones the **document** consumes, now pushed and followed:

| container | what it decides |
|---|---|
| `globalStorageSettings()` | clean whitespace on save, add a final newline |
| `globalTypingSettings()` | what Tab does, smart backspace, comment position |
| `globalExtraEncodingSettings()` | the UTF-8 BOM |
| `globalCodeStyle()` | tab and indent size, through the indenter |

The rest that `TextEditorWidgetPrivate` connects - display, margin, completion,
behaviour - are widget state, not the document's, so they are not this
editor's to push. Behaviour settings are read where they are used
(`scrollWheelZooming` in `TextViewport::zoomBy()`).

Storage settings are the one with consequences beyond appearance: saving from
the Quick editor was leaving trailing whitespace that every other editor
strips, which is diff noise in someone else's review.

**`setCodeStyle()` needs no connection.** `TextDocument::setCodeStyle()`
connects to the preferences it is handed - `currentTabSettingsChanged` ->
`setTabSettings` - and applies them immediately. The connection added
"to be safe" alongside it was removed when its control did not bite.

**A false clean that was not the harness.** With the build and run checks in
place, `no-code-style` still reported clean once. The cause was in the *test*:
it asked for `globalCodeStyle().tabSettings().m_indentSize + 3`, and an earlier
test in the same process had left the global indent at a value that made the
sum equal `TabSettingsData`'s own default - so the document already held the
number being waited for, and the assertion passed with nothing propagated.

Deriving the wanted value from **the document's current value** rather than
from the global one makes that impossible: the target is then always different
from what the document holds. The general form, and the fourth distinct way a
control has lied this session: **a fixture derived from shared mutable state
can collide with the default it is trying to prove is not being used.**

## Matching brackets, the first extra selection that needed no widget

Of the things the widget draws as extra selections, the bracket pair is the one
that needs nothing from the widget at all: `TextBlockUserData::
matchCursorBackward()` and `matchCursorForward()` are statics over the
document, the brackets themselves are recorded by the generic highlighter
(`setParentheses()` in `Highlighter::highlightBlock()`), and
`TextViewport::Highlight` already existed for the find results. So it is
~40 lines and no new plumbing.

Two brackets, one character each - not the range between them, which would put
a whole function body in the parenthesis colour. `C_PARENTHESES_MISMATCH` is an
overlay category and needs the same fallback the search results needed.

**Where to recompute it.** Not on `cursorPositionChanged`, not on
`contentsChanged`, not on the highlighter's `finished` - **in `updatePolish()`**.
Everything that can change which bracket the caret is beside already asks for a
layout, and `setHighlights()` now does nothing when handed the same ranges, so
computing it there settles instead of looping. Three call sites became one, and
the one is where the answer is used.

**A connection that was dead for the main editor.** The viewport connected to
`SyntaxHighlighter::finished` inside `documentChangedInternal()` - which runs
when the *document* changes. But an editor installs the highlighter *later*,
once it knows the file's language, and nothing announces that swap, so
`m_connectedHighlighter` stayed null for every real file. The check moved into
`updatePolish()`, where it is a pointer compare per pass.

Honestly: **its control does not bite.** Every case the suite can construct is
already covered by `contentsChanged`, because applying formats calls
`markContentsDirty()`. The connection is kept - it is the direct signal, and it
predates this work - but nothing here proves it earns its place.

**Two tests that were vacuous in different ways.**

The first asserted that no pair appears when the setting is off, by moving the
caret and checking immediately - before the deferred layout pass had run. It
passed against a build that ignored the setting entirely. There is no event for
"nothing happened", so the test was turned around: show a pair *first*, then
turn the setting off and wait for the pair to **go away**, which is an event.
That in turn needed the viewport to follow `displaySettings()` at all - it did
not - so the fix to the test and the fix to the code are the same change.

The second was an ambiguous control anchor: `updateParenthesesMatch();`
appeared twice, because `documentChangedInternal()` still called it before
asking for the polish that would call it again. The redundant call went. Worth
noting that a control that *cannot be applied* looks nothing like a control
that does not bite, which is why the harness now says `CONTROL DID NOT APPLY`
rather than failing silently.

## The feature that turned out not to exist

This batch set out to add auto-inserted brackets: type `(`, get `()`. It is one
of the most visible things an editor does, and the Quick editor did not do it.

It turned out the plain text **widget** editor does not do it either. The base
`AutoCompleter` is almost entirely stubs - `contextAllowsAutoBrackets()` returns
`false`, `insertMatchingBrace()` returns an empty string - and all the real
behaviour lives in per-language subclasses that arrive through
`TextEditorFactory::setAutoCompleterCreator()`: `CppAutoCompleter`,
`JsonAutoCompleter`, `CMakeAutoCompleter`, QmlJS's. There is no registry from
mime type to auto-completer, so an editor that is not built by a
`TextEditorFactory` cannot obtain one.

So the premise was wrong: this is not a gap in the port. A Quick editor with no
auto-insertion matches the plain text editor exactly.

**What was written and then deleted.** The first version wired all four hooks -
`autoComplete()` on typing, `paragraphSeparatorAboutToBeInserted()` on Enter,
electric-character re-indenting, and `autoBackspace()`. Three of them can never
fire with a base `AutoCompleter`, because each goes through
`contextAllowsAutoBrackets()`. They were removed rather than kept "for when a
language completer arrives": code that cannot execute is not a hook, it is a
claim that a feature exists.

**One of the four is real.** `AutoCompleter::autoBackspace()` is implemented in
the base class and does *not* consult the context, so Backspace between the two
halves of a pair removes both - which the widget editor does for plain text
files too. That one stayed, with its settings connection, and has a test and
three controls.

The general shape, worth repeating before adding anything else by analogy with
the widget: **check that the widget actually does the thing, in the
configuration being matched.** "The widget has an AutoCompleter" and "the plain
text editor auto-inserts brackets" are different claims, and only the first one
was true.

**The control loop lied a fifth time**, differently again: two controls
reported clean and then bit when re-run individually with the identical
command. The cause was not established. Individual verification is what the
result rests on; the loop is a convenience, and its output is only worth
trusting when it says a control *did* bite.

## The control loop, fixed

Five false cleans in, the cause was finally found and it was the build check
itself:

    err=$( (cd $BUILD && ninja $TARGET 2>&1) | grep -E "error" | head -2 )
    if [ -n "$err" ]; then echo "BUILD FAILED"; return; fi

Wrong twice over. The message pattern is a guess at what a failure looks like,
and putting `ninja` in a pipeline throws its exit status away - `$?` belongs to
`head`. The fix is to stop reading the log for a verdict:

    (cd $BUILD && ninja $TARGET >$log 2>&1); rc=$?
    [ $rc -ne 0 ] && { echo "BUILD FAILED: $(grep -m1 error $log)"; return; }

and to print **provenance** every iteration - `shasum` of the built library and
of the source file. Distinct source hashes across controls, and a baseline hash
that returns to where it started, is what proves each control was applied,
built, and undone. With that in place the same three controls that had
"not bitten" all bit, five runs out of five.

The general rule, which cost most of a session to learn: **a check that reads a
tool's output is a guess; a check that reads its exit code is a fact.**

## Wrapping needs no height cache either

Recorded because it changes the plan. The viewport's class comment says
wrapping "needs a height cache before it can say which line is at a given
scroll offset", and folding turned out not to need one because `QTextDocument`
tracks per-block line counts. The same is true here:
`PlainTextDocumentLayout::layoutBlock()` ends with
`setBlockLineCount(block, tl->lineCount())` - the *wrapped* line count. So
`document()->lineCount()` and `findBlockByLineNumber()` already answer for
wrapping exactly as they do for folding, and a row within a block is
`row - block.firstLineNumber()`.

**Two corrections, and what they were both worth.** First I wrote that wrapping
needs no height cache because the document tracks line counts. Then that this
was only true if the port accepted *sharing* those counts with every other
view, since `TextEditorLayout` overrides `blockLayout()` but not
`blockLineCount()`.

Both readings were wrong, and both for the same reason: I read an excerpt of
`texteditorlayout.h` and stopped. The second half of the class overrides
**all** of it -

    int blockLineCount(const QTextBlock &) const override;
    void setBlockLineCount(QTextBlock &, int) const override;
    int lineCount() const override;
    int firstLineNumberOf(const QTextBlock &) const override;
    QTextBlock findBlockByLineNumber(int) const override;
    int offsetForBlock(const QTextBlock &) const;
    int offsetForLine(int) const;  int lineForOffset(int) const;
    int documentPixelHeight() const;
    QRectF blockBoundingRect(const QTextBlock &) const override;
    void setBlockVisibleInEditor(const QTextBlock &, bool);

- backed by an `m_offsetCache` in `TextEditorLayoutPrivate`. It is a complete
**per-view** line and pixel index, maintained incrementally, and it even has
per-view block hiding that `QTextBlock::setVisible()` cannot give (the inline
diff editor collapses unchanged lines with it).

So: **the height cache wrapping needs already exists, is per-view, is in
`Utils`, and its layout half needs no widget.** There is no shared-state
problem and no design decision left open. The shape of the port is

    m_layout = new TextEditorLayout(documentLayoutOf(doc));   // per viewport
    m_layout->setTextWidth(width());                          // when wrapping
    contentHeight  = m_layout->documentPixelHeight();
    blockForRow(r) = m_layout->findBlockByLineNumber(r);
    yOfBlock(b)    = m_layout->offsetForBlock(b);

with the viewport still building its own `QTextLayout` copies for the render
thread, as it does today - it needs the *index* from the layout, not its
layouts.

**And a third wrong entry, before it was finally measured.**
`TextEditorLayout::firstLineNumberOf()` carries

    // FIXME: The first line cache is not reset/recalculated on width change

which reads like exactly the thing that would sink a wrapped Qt Quick view,
since a Quick item changes width constantly. That became the next conclusion:
blocked on a pre-existing `Utils` bug.

It is not. `testTheEditorLayoutFollowsAWidthChange()` puts twenty long lines in
a document, asks where the last block starts at width 100000 and again at 120,
and the answer moves. The FIXME overstates the case.

**What the same test pins is the part that does constrain a port**, and it took
asking the question two ways to see it. Ask the index *without* laying the
blocks out again at the new width and it answers from line counts that are
still 1. So the contract is:

> A view may ask where a block starts only after the blocks above it have been
> laid out at the current width.

That is the O(file) cost wrapping carries - and the widget carries it too, for
the same reason: its scrollbar range needs `documentPixelHeight()`, which needs
the whole cache. The viewport's O(visible) promise is a promise about the
*unwrapped* path, which is why wrapping is opt-in rather than a mode the editor
is in by default.

One more practical obstacle the test surfaced: **`setTextWidth()` is protected**
on `PlainTextDocumentLayout`. `PlainTextEdit` reaches it as a friend; anything
else has to subclass. Three lines, but it is the sort of thing that is better
found by a test than by an implementation half-written.

**The lesson, which cost three wrong entries in this document:** I reasoned
from a header excerpt, then from a fuller excerpt, then from a code comment,
and was wrong every time. The test that settled it took twenty minutes and
answers precisely what the implementation needs to know. When an API's
behaviour is load-bearing, **measure it** - and keep the measurement as a test,
because it is also the thing that will notice if `TextEditorLayout` changes
underneath the port.

Not started here: it restructures `updatePolish()` so a `Line` is a visual row
rather than a block, and every position mapping with it. It deserves a batch of
its own rather than the end of one.

## A design-system component nobody had ever instantiated

The editor supplied no `toolBar()`, so Core drew its own row - file list, close,
split - and nothing of the editor's. Every other editor shows where the caret
is. It does now: a small Qt Quick toolbar bound to the viewport, saying
`Line: 1, Col: 3` in the same words the widget uses, with `(Sel: N)` when
something is selected.

The column there is **tab-expanded** (`TabSettings::columnAt()`), which is not
what `IEditor::currentColumn()` returns - that is the character offset, and
`BaseTextEditor` returns the same. Two different questions with the same name;
the viewport now answers both, as `cursorColumn` and `cursorDisplayColumn`.

Writing it turned up that **`QtcQuick`'s `QtcLabel` had never been used**. Its
first line of real use failed with `Type QtcLabel unavailable`, because it
assigns `implicitHeight` on a `Text` - which computes its own implicit size and
will not be told one. The design system's intent (a fixed line height plus
padding) is expressible in properties `Text` does own:

    lineHeight: root.labelLineHeight
    lineHeightMode: Text.FixedHeight
    topPadding: root.vPadding
    bottomPadding: root.vPadding

A mirrored component with no call site is not "done", it is untested by
construction - `grep -c 'QtcLabel {'` over the tree answered **1**, and that one
was the new file.

**The sweep, and the guard it turned into.** Four more components in
`QtCreator.Ui` have no call site anywhere: `QtcBadge`, `QtcButton`,
`QtcIconDisplay`, `QtcPageIndicator`. All four do load, but nothing was
checking. `testEveryComponentInTheModuleCanBeLoaded()` now compiles every
`.qml` in the module and reports the ones that error - it names
`QtcLabel.qml:37: Invalid property assignment` when the bug is put back, which
is what makes it worth having rather than merely reassuring.

Two things it deliberately does *not* do: it does not instantiate (a component
with `required` properties compiles fine and cannot be created without them),
and it asserts the file list is non-empty first, because
`QDir(":/qt/qml/QtCreator/Ui")` finding nothing would otherwise be a pass.

**And a reminder about the sweep itself:** the first version was
`grep -r --include=*.qml ...`, and zsh ate the unquoted glob, so *every*
component came back with zero uses - a list of 47 "never used" components,
including ones visibly used on every page. Quoting it (`--include='*.qml'`)
gave the real answer of four. A sweep that reports everything is as wrong as
one that reports nothing; sanity-check it against something known to be used.

## The rest of the toolbar

Beside the caret position, the widget's toolbar says what the *file* is: its
line ending (`LF` / `CRLF`) and its encoding. Both are there now.

Each hides itself the way the widget hides it, and the rule lives in C++ rather
than in the QML: `fileLineEnding()` and `fileEncoding()` return an **empty
string** when their display setting is off, and the labels are `visible: text
!== ""`. One rule, one place. The line ending additionally hides for a
read-only file, because there is nothing to be done about the line endings of a
file that cannot be written - which is `TextEditorWidgetPrivate`'s rule too.

**The defaults are not symmetric**, which the test assumed and got wrong:
`m_displayFileLineEnding` defaults to **true** and `m_displayFileEncoding` to
**false**. The first version asserted both were showing and failed on the
encoding - correctly. The test now sets both explicitly, then turns each off
*separately* and checks the other is unaffected: two labels reading one
setting between them would otherwise pass a test that toggles both together.

Clicking the line ending offers Unix or Windows and switches the file between
them. It marks the document **modified** rather than writing - the change
reaches disk on save, which is the widget's behaviour and the reason the test
checks the file on disk is still `\n` immediately afterwards and `\r\n` once
saved.

Still display only: the encoding. Changing that is a codec chooser and a
reload, which is a different piece of work from a two-item menu.

**A control that needed a better fixture, not a change.** Removing the
`fileFormatChanged()` from the setter did not bite, because
`document()->setModified(true)` happens to emit `IDocument::changed`, which the
label already listens to. But `setModified(true)` on a document that is
*already* modified emits nothing - so the signal is load-bearing exactly when
the line ending is changed twice, or after an edit. The test now does that, and
the control bites. The rule from earlier in this document holds again: when a
control misses, ask whether the fixture can reach the case before concluding
the code is redundant.

**A note on running controls at this size.** Six build-and-run cycles no longer
fit in one command, and a control loop killed by a timeout leaves the *last*
control applied - the restore never runs. The recovery is `git diff --stat` on
the touched files before anything else. Splitting the loop and narrowing the
test to one class (`-test TextEditor,QuickTextEditorTest`) keeps each command
inside the limit.

## Wrapping

Done, and smaller than four analyses of it suggested. A `wrapping` property,
off by default, wired to the `textWrapping` display setting for the editor.

**A `Line` stays one visual row.** That was the decision that kept the change
contained. The alternative - one `Line` per block, holding a multi-row layout -
would have changed what `visibleLine(i)`, `visibleLineCount` and
`firstVisibleLine` mean, and with them the gutter, the annotations, the caret
and a dozen tests. Instead a wrapped block is *sliced*: each row gets its own
single-line layout over `blockText.mid(from, length)`, with the block's formats
clipped to the slice and shifted to start at zero - the same arithmetic
`appendHighlights()` already does, one level down. Everything downstream was
untouched.

**The unwrapped path is untouched too**, deliberately: `breaks` is
`{{0, text.size()}}` without shaping, and the row index still comes from the
document. The 238 tests that existed before this went green unchanged, which is
the evidence that matters for a change to the layout pass.

**What a row carries that a line does not.** A wrapped line covers several rows
but is *numbered, marked and annotated once*, on the row that starts it - so
`Line` gained `firstRowOfLine`, and the gutter shows a number only when it is
true. Getting this wrong is invisible until you look: every row numbered gives
a gutter that counts rows, which is a different document.

Two smaller things, both found by writing it rather than reading:

- The newline belongs to the **last** row of a block: that is where the caret
  sits at the end of a line. A continuation row's length stops where the next
  row begins, or the two rows both claim the same position.
- A row's own layout must be laid out at *infinite* width, not the wrap width.
  It is already one row's worth of text, and re-wrapping at the same width can
  split a row whose measured width came out a hair over.

**The cost is stated rather than hidden.** Turning wrapping on gives up the
O(visible) promise: `updatePolish()` lays out every block to build the index.
That is what the class comment now says, and it is what the widget editor does
too - its scrollbar range needs `documentPixelHeight()`, which needs the whole
cache.

## An editor needs a context of its own

Wrapping is also a menu item - **Wrap Lines** - and menu items are `Command`s
registered per editor, in that editor's context. The widget editor registers
`TEXT_WRAPPING` against `m_editorContext(Id::generate())`, and the `Id::
generate()` is the whole point: **one generated context per editor instance**.

The Quick editor had a single shared context, `QUICK_TEXT_EDITOR_ID`, which is
right for "any editor of this kind" but wrong for a per-editor action. Opening
a second Quick editor produced

    addOverrideAction: Action is already registered for context
    TextEditor.QuickTextEditor.

sixteen times, the second registration was dropped, and that editor's Wrap
Lines entry silently did nothing. The editor now carries both: the shared id
*and* one of its own.

**The test did not catch it**, which is the more useful half. A dropped action
registration is a `qWarning` and nothing else - the suite was green with
sixteen collisions in the log. The test installs a message handler for the one
phrase and asserts it never appears, which is the same shape as every other
"the failure mode is a diagnostic" case in this document. Only then did the
control (registering in the shared context) bite.

**And `QVERIFY2` caught me a second time.** Its message argument is built
whether or not the condition held, so
`QVERIFY2(hits.isEmpty(), qPrintable(... + hits.first()))` aborts the process
on success. `join("; ")` is safe on an empty list. This is the second time this
session - the first was `Result::error()` on a value - which is enough to call
it a rule: **nothing in a `QVERIFY2` message may assume the assertion failed.**

## Diagnostics belong to the document

The last thing called architectural in this document turned out not to be. Every
producer of an extra selection - the language clients' warnings, the code
model's unused symbols, the debugger's exception line - goes through **one**
method, `TextEditorWidget::setExtraSelections(Id, list)`. So no producer had to
change: that method now also puts the document-wide kinds on the `TextDocument`,
and any view of that document can draw them. `TextViewport` does.

`TextDocument::ExtraSelection` is `{QTextCursor, QTextCharFormat}` -
`QTextEdit::ExtraSelection` is the same pair with a QtWidgets header in front
of it. A cursor rather than two offsets, because an edit above has to carry the
warning with it; the test inserts a line at the top and checks the highlight
followed rather than staying on its row.

**An allowlist, and the reason is cost.** The first version shared everything
except four view-only kinds. That included `CodeSemanticsSelection`, which is
one range per identifier on screen - and every `QTextCursor` in a document is
updated on every change, so a second copy of a large set doubles that work on
every keystroke. The evidence was circumstantial but pointed: `LayoutPreviewTest`,
whose assertion is a *timed* wait on semantic rehighlighting, failed in the full
CppEditor run and passed alone. Narrowing to an explicit allowlist of the small
diagnostic kinds made it stop failing.

Semantic colouring was never the reason to do this anyway - a Quick view gets
that from the highlighter's formats, which is where it already came from.

**What stayed with the view:** the cursor, what this view just auto-inserted,
the bracket it is matching, snippet placeholders. Those are not facts about the
file, and the test checks a bracket match set on a widget editor does *not*
appear on the document.

## Two of my own notes, ignored in one command

    for f in ...; do cp "src/plugins/texteditor/$f" "$backup/$f"; done
    git checkout -- src/plugins/texteditor/

`cp` is aliased to `cp -i` here, which **silently refuses** when the
destination exists - six of seven copies did not happen - and then the checkout
discarded the working tree. Both traps are already written down in this
project's notes, and combining them nearly lost an afternoon's work.

It survived by luck: the seventh file was new in the backup directory, so its
copy succeeded, and the other six still held an earlier backup that happened to
include everything but the last test. The recovery was `cat "$backup/$f" >|
"$dest"`, which is the form the notes prescribe.

The lesson worth keeping is narrower than "be careful": **a backup step whose
failure is silent is not a backup.** Check it (`ls`, or compare a marker) before
running anything destructive.

## Where else the selected word appears

The widget marks the other places the selected text occurs, and it computes
that itself from the document - no producer, no code model - so it ported
directly. Same rule: a selection **inside one line**, trimmed, only when
`highlightSelection` is on, searched case-insensitively with `QChar::Nbsp`
read as a space.

It is done inside the layout pass, over the blocks already being laid out, so
it costs what the rest of the pass costs. The ranges go into the block's format
list *before* the selection's own, so where the two overlap - the selection is
one of its own occurrences - the selection wins.

The fill is `C_SELECTION`'s background at 0.25 alpha on a light scheme and 0.5
on a dark one, which is what the widget's overlay uses. The luminance of
`C_TEXT`'s background is what decides which.

**The test had to stop counting formats.** Its first version asserted the
number of format ranges on a row, and got two where it expected one: the second
was the *whitespace* mark, because `CodeSource` installs a `SyntaxHighlighter`
and `SyntaxHighlighter::highlightBlock()` calls `formatSpaces()`. Counting
everything on a row couples a test to every other thing that formats text. It
now filters by the property that identifies an occurrence and nothing else -
**a translucent background** - which is worth stating in the test because it is
an invariant of the viewport, not an accident.

**And a control that needed the fixture to be sharper.** Removing the
single-line rule changed nothing, because a selection spanning two blocks
contains U+2029 and no block does, so the search finds nothing either way. The
case that separates them is a selection that *starts on the newline*:
`QString::trimmed()` strips the paragraph separator - `QChar::isSpace()` is
true for it - so the text searched for would be exactly the word on the line
below, and only the two-blocks test rejects it. Third time this session that a
control missed because the fixture could not reach the case rather than because
the code was redundant.

## Going back to a report I could not reproduce

Early in this work the user said the Code Style preview's "highlighting does not
work". Two of the three symptoms in that message were fixed then; that one was
never reproduced, and it stayed unexplained while the editor grew around it.

Coming back with more of the machinery understood, there is exactly one way it
happens: `CodeSource::applyHighlighting()` asks
`HighlighterHelper::definitionsForMimeType()` and, finding nothing, installs a
bare `SyntaxHighlighter` - which marks whitespace and nothing else. A preview
with the wrong mime type, or on a machine with no syntax definitions installed,
is grey by that path and no other.

So the question worth answering is not "is it broken" but **"does every Code
Style page name a mime type that resolves"**. It does, here, for all three -
`Cpp (text/x-c++src)`, `QmlJS (text/x-qml)`, `Nim (text/x-nim)` - and the test
enumerates `codeStyleFactories()` rather than naming them, so a fourth language
is covered the day it is added.

That does not explain what the user saw, and this document should not pretend
it does. What it does is make that particular cause impossible to reintroduce
silently, and give the next report a fact to start from: if the test passes and
a preview is still grey, the definition lookup is not the reason.

**The control is the interesting half.** Forcing the no-definition path fails
the test with the three pages named in the message, which is what makes it worth
having - a green run says the previews resolve, and a red one says which one
does not.

## Changing the encoding, and where to put a dialog

The last of the toolbar. Clicking the encoding opens Core's codec chooser, and
the answer is one of three quite different things - which is the whole reason
the dialog asks rather than just setting something:

- **Reload** reads the same bytes as something else. What is on screen changes;
  what is on disk does not.
- **Save** is the other direction: the text stays and the bytes change, and
  that only reaches the file when it is saved.
- **Cancel** does nothing at all, not even mark the document.

**The dialog is at the edge and the decision is not.** `selectEncoding()` is
two lines - ask Core, hand the answer to `applyEncodingChoice()` - and the
second is what has the behaviour and the test. A test that called
`selectEncoding()` would stop on a modal dialog; one that calls
`applyEncodingChoice()` with each of the three answers checks the thing that
matters. Splitting a function so the untestable part is trivially small is
cheaper than any amount of dialog automation.

The fixture is Latin-1 bytes - `0xE4` is a-umlaut there and not valid UTF-8 -
so which encoding the file is read as is visible in the text, and the three
outcomes are told apart by *both* what the document holds and what is on disk.

**What could not be tested, and is asserted instead.** That the label opens the
chooser cannot be exercised without opening it, so the test asserts the label
carries a `TapHandler` - by class name, from `findChildren` - and says why. It
is a weaker claim than "clicking it opens the chooser", and it is stated as
the weaker claim rather than dressed up.

That test also found its own bug first: it looked for the label in the editor's
form, and the toolbar is a *separate* form built on demand by `toolBar()`.

What is left of the editor: auto-insertion and completion, which need the
provider registry described above. Dragging text has since been done.

**On completion, having looked properly.** (Superseded - see "The registry that
was already there" below. The lookup exists; what follows was written before
that was found, and its conclusion is wrong for the languages that keep their
services on the factory.) It is not one missing call. The
provider lives on the document (`TextDocument::completionAssistProvider()`),
which is promising, but it is *put* there by
`TextEditorFactory`'s document creator from a provider the factory was given -
`setCompletionAssistProvider()`, one per editor factory. There is no lookup from
mime type to provider anywhere, so an `IEditorFactory`-based editor cannot
obtain one, exactly as with `AutoCompleter`. Giving the Quick editor completion
means building that registry and changing every editor factory to populate it:
Creator-wide infrastructure, not a batch, and worth agreeing before starting.

## The same measurement, applied to kits

Re-running the `layouter()` instrumentation with the device closures gone leaves
15 uses across five suites, and every one is still a test: `CodeStyleAspectTest`
(10, blocked on ClangFormat), `KitAspectTest` (3), and one each from the QuickUi
and CppEditor tests that build a widget form on purpose.

**Kits go the same way as devices, for the same reason.** `KitsPage.qml` draws
the current kit with

    AspectItems { model: AspectModels.container(root.details.KitAspects.container) }

so a kit aspect's control container is drawn by the Quick renderer, which
consults no layouter. `ToolchainKitAspect` was handing over a container with a
`Grid` closure in it to make its per-language lists stack; **a container stacks
anyway** - one aspect per row unless its presentation asks for `inlineRow` - in
the Quick renderer and in the widget renderer's fallback column alike. The
closure only ever told the widget renderer something it would have done without
being asked.

**The test was asserting the mechanism, not the property.** It emerged the
widget layout and looked for a `QGridLayout` whose `rowCount()` matched the
language count - which is one renderer's way of stacking, and the one the
closure built. It now asserts what makes them stack in either renderer: the
aspect hands over a container, that container lists one selection per language,
and it does not ask to be drawn in a line. Setting `inlineRow` on it fails the
test by name, which the old assertion would not have noticed.

That is 4 closures gone in two batches - three device, one kit - and the count
of `setLayouter` calls in the tree is down to 23. What is left is the code style
group behind ClangFormat, the run configuration surface, and two hand-attached
call sites.

## What still reaches a widget layouter, measured

Requiring Qt Quick looked like it would strand the widget layouters - the six
restored earlier in this plan were kept only because a Quick-less build reached
them generically. It does not, and the reason is worth writing down, because
this is the third time in this plan that deleting them looked safe and was not.

**Who asks for a layouter at all**, which is the question rather than how many
`setLayouter` calls there are (29, in 24 files):

| caller | when |
|---|---|
| `IOptionsPage::createAspectForm()` | the Quick factory declined the container |
| `aspectwidgetrenderer.cpp` (x2) | a *nested* container inside a widget form |
| `runconfiguration.cpp` | a run configuration aspect's project settings |
| `axivionsettings.cpp`, `qtprofilerwindow.cpp` | attached by hand |
| `codestyleaspect_test.cpp` (x5) | tests of the widget path |

**Measured rather than reasoned about.** Instrumenting the fallback in
`createAspectForm()` and running QuickUi, TextEditor, ProjectExplorer, CppEditor
and Debugger shows it taken 14 times, and every one is either
`DesktopDeviceTest` - **device settings containers have no `qmlSource`** - or
`QuickUiTest::testBuildingAPageIsNotShowingIt`, which builds a widget form
deliberately to check both paths behave alike.

**And the code style layouters are still live**, which is the one that would
have caught me out. `CodeStyleAspect` renders with Quick for a language whose
factory names a form, and C++, QML/JS and Nim all do - but **ClangFormat's
factory calls no `setQmlSource` at all**, so that page still builds the widget
editor, and with it `TabSettings`, `BehaviorSettings` and the rest of the nested
containers whose layouters looked dead.

So the condition for deleting each group is now a named thing rather than a
feeling:

- the `createAspectForm()` fallback goes when **device settings name a QML
  form**;
- the code style and tab/behaviour/storage/typing layouters go when
  **ClangFormat's factory does**;
- the run configuration and hand-attached ones go when those surfaces move,
  which they have not started to.

**Correction, from trying the first of those.** Devices are not reached through
`createAspectForm()` in production at all - every call for a device container is
inside `DesktopDeviceTest`. Their settings are drawn by the *widget renderer*,
laying out a nested container, and that asks `layouter()`. So the two layouters
in `idevice.cpp` are load-bearing exactly where it is least visible: delete them
and `layouter()` falls back to a column, turning label-beside-field rows into
label-above-field ones with nothing failing.

The attempt also ran into a contract. `createAspectForm()` declines a container
with no QML **deliberately**, and `testPageWithoutItsOwnQmlIsDeclined` says so
in as many words. Refining it to "declines unless it has no layouter either" is
sound in the small - a container with neither has no layout to lose, and
`AspectWidgets::hasLayouter()` already exists for exactly this distinction, used
by the widget renderer two lines apart - but it changes a tested contract for
every layouter-less container in the product, and it does not reach devices,
which was the point. Reverted.

**Second correction: they were dead after all, and the measurement said so.**
Instrumenting `layouter()` to report every time it hands back a *stored*
closure, across six suites, gives one answer - **every single use comes from a
test**. 41 from `DesktopDeviceTest`, 10 from `CodeStyleAspectTest`, 3 from
`KitAspectTest`, one from the QuickUi test that builds a widget form on purpose.
Not one from a production path.

The reason is plain once looked at rather than reasoned about: `DevicesPage.qml`
draws the device's containers with

    AspectItems { model: AspectModels.container(root.aspects.TypeSpecific.container) }

and `TypeSpecific` is set to `deviceInfoAspects()` or `settingsAspects()`
depending on the device. Nothing in QtcQuick consults a layouter at all - the
only mention of the word in the whole library is a comment. A container drawn
inside a Quick page therefore never uses one, and the Devices page is Quick.

So all three closures in `idevice.cpp` are gone. The third even said so itself:
*"One row each, whatever they turn out to be. A Qt Quick page draws the
container directly and needs none of this."*

**The two tests were pinning a path production does not take.** One asserted a
`QFormLayout` with one row per tool - a property the deleted closure provided by
putting a `br` after each aspect. How many rows those become is the renderer's
business now, so the test asserts what the group actually owes: the list, and
its order. The other asked `createAspectForm()` for a form and checked its
layout; it now asserts `!hasLayouter()`, which is the more useful thing - a
layouter added back here would never run, so nothing else would notice.

## A shutdown crash the fixed Apply uncovered

Running the whole suite plugin by plugin - which twenty-odd commits touching
`ioptionspage.cpp`, `TableDelegate.qml` and `aspectpresentation.h` had earned -
found ProjectExplorer aborting under AddressSanitizer at **exit code 134**, in
code reached from the code style selector.

It is a shutdown race, and it took two goes to see properly.

    ~CppCodeStylePreferencesFactory
      ~ICodeStylePreferences
        CodeStylePool::detachCodeStyle(this)
          emit codeStyleRemoved(this)        <- still in the pool's own list
            CodeStyleSelectorAspects::refill()
              m_codeStyle->delegatingPool()  <- half destroyed

`detachCodeStyle()` emits **before** dropping the style from its list, so a
handler sees a list that still contains an object whose destructor is running.
The first fix - ignore the signal when the style being removed is the one being
edited, and skip it while walking the pool - turned the heap-buffer-overflow
into a heap-use-after-free at the same line, which is the more useful error: the
style being edited was *already gone*, and a different style's removal was
bringing us back in.

So the real shape is that **the style outlives nothing in particular**. It can
be destroyed while the container the aspects live on is still alive, which is
why scoping the connections to the container did not help. `m_codeStyle` is a
`QPointer` now and the two entry points return early when it is null.

**What made this reachable.** `CodeStyleAspect::apply()` never ran before the
Apply fix earlier in this plan - `IOptionsPageWidget::apply()` soft-asserted and
returned - so the selector was never refilled at the point where this bites.
Fixing Apply did not introduce the bug; it stopped hiding it. That is the
ordinary shape of a latent crash behind dead code, and the reason for running
the whole suite rather than the two plugins being worked on.

## Qt Quick is required, so nothing branches on it

Qt Quick used to be an `OPTIONAL_COMPONENT`, and everything added for the editor
sat behind `CONDITION TARGET Qt::Quick`. That was worth auditing, and the audit
found a real break: `texteditorplugin.cpp` is built either way and was including
`codehighlighting_test.h` and `textviewport_test.h` under `#ifdef WITH_TESTS`
alone, while those headers only exist when `WITH_TESTS AND TARGET Qt::Quick`. A
`WITH_TESTS` build without Qt Quick did not compile. One of the two includes was
pre-existing; the other was added the same way because the line above it looked
right.

**The answer is not to guard it better.** Qt Quick is now a required component:
it moved out of `find_package(Qt6 OPTIONAL_COMPONENTS ...)` and into the
required `COMPONENTS` list beside Widgets and Qml. The whole point of the
migration is that the UI is Qt Quick, so a Creator without it is not a
configuration anyone is going to ship - and every `#ifdef` and `CONDITION` for
it is a branch that exists only to be got wrong. The guard added for this was
removed again the same day.

That left **73 `CONDITION TARGET Qt::Quick` blocks across 67 CMakeLists**, now
always true, and they are gone: 69 mentions across 66 files, of which 59 were
the single form `if(TARGET <plugin> AND TARGET Qt::Quick)`.

**What was deliberately left alone.** `Qt::Quick3D*` and `Qt::QuickPrivate` are
still genuinely optional - Quick3D is not in the required list and QuickPrivate
comes from the Qt 6.9 block - so every condition naming those stays. So does
every `DEPENDS`/link line: those name real targets and are not conditions. Three
mixed conditions kept their other terms rather than being dropped whole:
EffectComposer keeps `TARGET QtCreator::QmlDesigner`, QmlDesigner keeps
`TARGET QmlDesignerCore AND TARGET Qt::Svg`.

**The qbs side had four of the same, and removing them exposed a trap.**
`Depends { name: "Qt.quick"; required: false }` paired with
`condition: Qt.quick.present` is one idea in two lines: deleting both leaves a
product with *no* dependency on Qt Quick at all. `TerminalQuick` builds a
`QQuickItem` and would have had nothing to compile it against. Both it and
`QuickUi` got a required `Depends { name: "Qt"; submodules: [...] }` where the
optional pair used to be - the other two already had one further down.

It does **not** unblock deleting the widget layouters, which is what it looked
like it would. That claim was checked and is wrong; the checking is below.

## Editing 55 pages, because asking them was not enough

Three passive censuses could not answer whether a page's Apply and Cancel work,
and all three failed the same way: the property only means something *after* an
edit, and an unedited page is indistinguishable from one that can never be
dirty. The fix is to edit - but not on a hundred pages, because editing runs
whatever each page does about it.

So the subset is chosen to make editing bounded rather than to be
representative:

- the container is **exactly** `Utils::AspectContainer`, so nothing overrides
  `apply()`, `cancel()` or `isDirty()` and there is no page-local copy in play;
- the aspect edited is a plain enabled, visible `BoolAspect` - one bit, no side
  effect;
- the write goes to the *volatile* value, which is what a check box writes, and
  `cancel()` puts it back with nothing reaching disk.

That is **56 of the 107 pages**, and all 56 keep the promise: editing makes the
container dirty, and Cancel restores the value.

**The count went down when the test got right, which is the point.** It reached
61 by widening what it would edit, and then 56 by refusing to edit anything with
no settings key. An aspect that is never saved is not a setting and never
promised to make a page dirty: the device picker on the Compilers page chooses
whose toolchains to *show*. Five pages had been asserted through an aspect like
that, so five assertions were about something the page does not claim. 56 is the
number where every one of them is a claim the page actually makes.

It got there by asking why the others were skipped rather than guessing. Only
**3** pages have a container of their own; the rest were skipped because their
settings live in groups, a container down, and because a check box is not the
only harmless thing to edit. Recursing into nested containers and accepting
integers, doubles, selections and tri-states - still refusing paths, which get
resolved against the filesystem when they change, and plain strings, which may
drive a completer - is what the subset is now. The count of each skip is
reported, so the boundary is a number rather than an impression:

    pages edited and cancelled: 56 | skipped, own container: 3
      | skipped, applies as edited: 4 | skipped, nothing safe to edit: 44

**What the last 44 are, listed once so nobody has to guess again.** Overwhelmingly
paths - Docker, Podman, Zephyr, Vcpkg, WebAssembly, Coco, and every formatter
page - and pages whose content is a list, a tree or a set of buttons: Kits,
Compilers, Debuggers, Devices, Keyboard, Locator, External Tools, MIME Types,
Snippets. Those are not a hole in this test so much as a different kind of page.
What they promise is add, remove and select, and that is what the table, tree
and grouped-list tests above operate. A page whose only editable thing is a file
path has no cheap safe edit at all, and forcing one would mean touching the
filesystem to find out.

The edit is also only made if it *takes*: a value already at the top of its
range does not move, and asserting that a page noticed a change that never
happened would assert nothing.

Both halves are held by a control that names the page. Making one page forget
`setAutoApply(false)` reports *Terminal/EnableTerminal did not notice being
edited*; stopping `AspectContainer::cancel()` from restoring reports 51 pages by
name. A diagnostic that says which page and which setting is the difference
between a failure someone can act on and one they have to go looking for.

**The two tests divide the question between them.** A page that applies as it is
edited never becomes dirty, so this one skips those and the list above owns
them; break `setAutoApply(false)` on a page and the *list* test names it, break
`AspectContainer::cancel()` and this one names 51 pages. Neither can catch both,
and together they catch either.

**What this does not cover, and why that is the honest boundary.** The other 51
pages have a container of their own, apply as they are edited, or hold nothing
safe to edit, and for them the question is still open - `CodeStyleAspect` is the one that was answered, one
page at a time, by a test that presses its Apply. There is no shortcut for the
rest: a page that keeps its own copy means something particular by "dirty", and
only its own test can say whether it is right.

## Which pages apply as you type, and why that is a list and not a check

Having fixed Apply on one page, the obvious question is the other 106. It is
not answerable the way the first one was. A page whose aspects auto-apply has
written the edit through already, so its container is never dirty and Apply has
nothing to commit - but **an unedited page is not dirty either**, so the
condition cannot tell "never saves" from "nothing has been typed yet". Two
attempts at a census of it named the same four pages both times, and both times
they were the pages that mean it.

Editing something on each of a hundred pages to find out is the only thing that
would answer it, and that runs whatever each page does about the edit. So this
does not claim to detect the bug. It holds the *list* still:

- the three Code Style pages keep a page-local copy their aspects edit live, and
  `CodeStyleAspect` overrides `apply()`/`cancel()`/`isDirty()` to push it
  across. Covered by a test that presses the page's own Apply.
- Android says so with `IOptionsPage::setAutoApply()`: its changes are immediate
  and it offers no Apply of its own.

A fifth page arriving in that list has neither, and its Apply button is a no-op.
The test names it and asks for a reason. It also fails the other way, when a
listed page starts deferring - otherwise the list outlives the reason for it and
becomes four names nobody dares remove.

**This is the same discipline as the buttons note above**, arrived at from the
other direction: where a census cannot separate the deliberate from the broken,
it should enumerate the deliberate ones rather than quietly permit both.

## Apply on the Code Style pages did nothing

Following the same thread - what is wired but never operated - to the buttons at
the bottom of the dialog. Pressing Apply on a Code Style page fired

    SOFT ASSERT: "!aspect->isAutoApply()" in ioptionspage.cpp:206

and returned. `CodeStyleAspect::apply()` never ran, so the page pool was never
copied into the real one and `toSettings()` was never called: **a code style
change was not saved.**

The check was using one child's `isAutoApply()` as a proxy for "this page
defers", and on this page that is exactly backwards. `CodeStyleAspect` keeps a
page-local copy of the style; its aspects edit that copy *live*, which is what
makes Cancel mean something, and `apply()` is what pushes the copy across. The
live-editing child is the design, not the mistake.

Reordered so the question asked is the one that matters: apply when the
container says it is dirty, and keep the diagnostic for the case it was written
for - nothing to apply *and* auto-applying aspects, which is what "nothing ever
saves" actually looks like. A page that forgets to defer still reports it,
because auto-applied aspects are never dirty.

**Why the aspect's own tests missed it.**
`testEditMakesDirtyAndApplyCommits` calls `aspect.apply()` directly. That works
and always did. The dialog does not call it - it presses
`IOptionsPageWidget::apply()`, which decides for itself whether there is
anything to commit. The aspect was covered and the button was not, which is the
same gap the table and tree bugs came out of, one layer up.

**And the test scaffolding hid it too.** The Quick test factory handed the page
an `IntegerAspect` with `Q_UNUSED(codeStyle)` - a settings aspect that edits
nothing. A page whose settings change nothing has nothing to apply, so no test
built on it could have failed this way whatever the check did. It now edits the
page's copy the way a real factory's aspects do, which is also what makes the
negative control bite.

## What "operated" now covers

Three shapes of the same question - which item is the page about - and all
three are now driven by a test rather than only drawn:

| shape | pages | what is checked |
|---|---|---|
| table | 10 tables | selecting a row reports that row |
| tree | 5 trees | the reported index is in the aspect's own model, and the handler runs without QML complaining |
| grouped list | 5 lists, 26 rows | `rowForIndex(indexForRow(r)) == r`, and `currentRow` takes |

A grouped list - Kits, Toolchains, Debuggers, Qt Versions, the CMake, Meson and
GN tool pages - shows items under headings, so a row of the view is not a row of
the aspect. A click does

    aspect.currentRow = aspect.rowForIndex(view.index(row, column))

and moving the current row the other way asks `indexForRow()`. Those two have to
be inverses or the page acts on an item the user did not pick. They are, on all
26 rows; breaking the mapping reports *Kits: row 3 came back as 0* and seven
more, which is the diagnostic worth having - it says which page and which row,
not that something is wrong somewhere.

Everything here is reached with `QMetaObject::invokeMethod` by name, which is
what QML does. That is deliberate: it is also the only thing that says these
are still `Q_INVOKABLE`, which no build error would.

**What is still only drawn: buttons.** Every page has them and nothing clicks
one. That is not an oversight to fix by clicking them all - a button's action is
a real action, and a census that pressed every one would open dialogs, start
processes and rewrite settings. Two known hazards are already recorded: an
aspect looked up by type is first-wins, and a test that asked for Remove got Add
and sat in a modal wizard for 300 seconds. Whatever covers buttons has to name
which ones it presses.

## Operating a tree, and the crash under the mapping

The table bug said where to look next: a tree has the same shape - the view says
which row the cursor is on, a handler hands it to the aspect - and nothing
operated one either. Auditing first: every `aspects.X.y()` call in page QML
comes to **11 distinct calls**, and all 11 resolve to a real `Q_INVOKABLE` with
a matching signature. So the tree pages were already right, and the test locks
that in rather than fixing anything.

What it checks is two failures that look nothing alike. The delegate has to
answer an index in the aspect's *own* model, because an index from the filter
proxy finds nothing there. And the handler has to run without QML complaining:
`aspects.X.setCurrentIndex(...)` is resolved by name at call time, so a setter
that is not `Q_INVOKABLE`, or one taking an `int` where the delegate hands over
a `QModelIndex`, is a `TypeError` at runtime and silence everywhere else. That
second half is what qmllint cannot see and what only running the handler finds.

**A test that reads global state it did not set is not testing itself.** Both
of these build pages, and whether a page builds with Qt Quick at that point in
the run depends on which tests ran first: the census installs the form factory
and clears it again, `showPage()` installs it and does not. The table test
passed on state a *later-declared* test had left behind, and the tree test - one
line further down - saw 107 pages with no `QQuickWidget` in any of them. Both
now install the factory themselves.

**The control found a heap overflow.** Reverting `TreeDelegate`'s `mapToSource`
does not produce a wrong answer, it aborts the suite under AddressSanitizer:

    ERROR: AddressSanitizer: heap-buffer-overflow
    #2 Core::ExternalTool::preset()
    #3 Core::Internal::ExternalToolsAspects::updateButtons()

`ExternalToolModel::toolForIndex()` is
`static_cast<ExternalTool *>(index.internalPointer())` with nothing asked, and a
`QModelIndex` from another model carries a pointer to something that is not a
tool. So the mapping is not a correctness detail, it is the only thing standing
between a stale index and undefined behaviour - and `setCurrentIndex` is
`Q_INVOKABLE`, which means the value comes from QML and cannot be assumed
mapped. That end is now guarded too: an index of another model's is declined.
Of the seven `setCurrentIndex(QModelIndex)` implementations, External Tools was
the only one casting blindly.

Worth noting how that control reports: the run aborts, so there is no failure
line and no totals at all. **Checking the exit code is what catches it**, which
is why the per-batch procedure asks for the exit code as well as the totals.

## Font && Colors, and the table bug behind it

Font && Colors turned out not to need the editor at all. The workstream listed
it beside Code Style and Snippets, but its "preview" is not a code snippet - it
is the **format list itself, read in the colours it is describing**. Per-cell
foreground, background and font were already ported and tested. One piece was
not:

`Qt::BackgroundRole` answers *nothing* for a format that sets no background of
its own, which is most of them. The widget list carried the editor's background
in its palette, so those rows were still read on it. In Quick they fell through
to the form's background, and a dark scheme's colours on a light form are
unreadable. That is now `AspectPresentation::rowBackground` - the background the
rows are read *against*, as distinct from any row's own - painted behind the
rows and not behind the header, which is where the widget put it too. Unset
leaves the form alone, which is what every other table wants; the shipped
"Default" scheme sets no text background and so gets nothing, exactly as the
widget did.

**The real find was underneath.** Writing the test for it meant selecting a row,
and selecting a row did not work anywhere:

    readonly property int currentRow: {
        const index = view.currentIndex        // TableView has no currentIndex
        return index && index.valid ? ... : -1
    }

`TableView` answers `currentRow` and `currentColumn`. There is no
`currentIndex`, so this read `undefined` and returned **-1 whatever was
selected**. Every page that shows a detail of the current row - the format
properties in Font && Colors, the snippet text in Snippets - was told nothing,
ever.

Two things let it live that long. qmllint reported it (`Member "currentIndex"
not found on type "TableView"`) and it was waved off twice as pre-existing noise
in a file with other warnings. And no test selected a row and then asked what
the page had been told: the census renders every page and asserts the controls
are there, which a page showing the wrong row passes.

**How wide it went, measured rather than guessed.** Six pages hand
`currentRow` to an aspect - Project Environment, Python Interpreters, ClangTools'
project panel, Snippets, Font && Colors and MIME Types - and all six names check
out against a real `setQmlName`, so the wiring was right and only the property
was wrong. Reverting the fix and running the new test names **ten** tables, not
six: User Command Mapping, the MCP Server's tools, CPU Usage, Code Style,
General, Documentation and QML/JS Editing have tables whose current row was
also always -1, whether or not they read it yet.

That test is deliberately kept out of the page census. The census builds pages
and looks at them; selecting a row runs whatever the page does about it, and a
side effect there should not read as a rendering failure. It is also the answer
to why this survived: **the census renders every page and asserts the controls
are present, which a page showing the wrong row passes.** Rendering is not
operating, and until something operated a table nothing was going to notice.

**Two assertions in this batch turned out to be vacuous and were cut rather than
kept.** An unshown `TableView` lays out once and its `currentRow` then goes
stale, so half a test read a value from before the step it meant to check.
Showing the form fixes the staleness but not the vacuity: after filtering,
`currentRow` already holds the number the assertion looks for, so it is
satisfied by the previous state. The mapping through the filter proxy is still
right - it is what makes "the row in the aspect's own model" true - but nothing
here holds it, and the test says so instead of implying otherwise.

## Why an aspect subclass cannot be typed in QML

The census counts an aspect as drawn when some item on the page declares a
property called `aspect` holding it. `CodeStylePreview.qml` draws the preview
with a `TextArea` rather than one of the stock delegates, so for three Code
Style pages the census reported `does not draw: /Preview` - the one line in it
that was wrong rather than merely allowed. Declaring

    readonly property Aspect aspect: root.aspects.Preview

fixes it, and is worth more than the tidier report: a misspelt `aspects.` name
is a `QQmlPropertyMap` miss, which is `undefined` rather than an error, and now
one of them fails the census with *"CodeStylePreview_QMLTYPE has no aspect"*.

**The typed version does not work, and the reason generalises.** Declaring
`property CodeStylePreviewAspect aspect` instead, with the type registered the
usual `QML_FOREIGN` way, builds and runs and renders - and qmllint answers
`Type TextEditor::CodeStylePreviewAspect is used but it is not resolved`. The
registration is fine; the prototype chain is not:

    CodeStylePreviewAspect -> Utils::StringAspect -> TypedAspect<QString> -> ?

`TypedAspect<T>` is a class template. A template cannot carry `Q_OBJECT`, so it
has no metaobject of its own and cannot be registered, and qmllint stops at it.
Registering `Utils::StringAspect` as well does not help - it just moves the
break one link along.

So **only aspects that derive directly from `BaseAspect` can be typed in QML**,
which is exactly the set already registered in `aspectcontainermodel.h`
(`ActionAspect`, `AspectList`, `GroupedListAspect`). Everything below a
`TypedAspect<T>` has to stay an untyped `aspects.Foo` lookup, and the
protection against a typo in one is the census assertion above rather than
qmllint. Both speculative registrations were reverted: a type that does not
resolve also stops qmllint checking the bindings that named it, so it is worse
than the untyped form it replaced.

## The QSGTextNode spike: go, with one architecture change

The editor port's go/no-go was measured with a standalone Qt Quick spike rather
than reasoned about. Both risks it was aimed at are answered.

**Throughput is a non-issue.** M5 Pro, macOS 26.5, Metal RHI, 120 Hz, window
1400x834 at DPR 2.0, 61-62 visible lines, continuous 3000 px/s scroll over 12 s,
frame measured swap-to-swap:

| lines | mode | frame mean / p99 (ms) | polish (ms) | paintNode (ms) |
|---|---|---|---|---|
| 10k  | re-emit | 8.333 / 8.963 | 0.091 | 0.590 |
| 100k | re-emit | 8.333 / 9.264 | 0.091 | 0.610 |
| 1M   | re-emit | 8.335 / 9.216 | 0.092 | 0.615 |
| 1M   | node cache | 8.334 / 9.834 | 0.092 | 0.048 |

Every mode locks to vsync at every file size, including one million lines: cost
is O(visible), not O(file). Re-emitting every visible text node each frame costs
0.6 ms of render thread, so **the per-line node cache the plan assumed would be
mandatory is not needed** for 120 Hz. Caveat stated by the spike: vsync could not
be disabled on Metal, so headroom *beyond* 120 Hz is inferred from CPU time
rather than measured.

That retires the risk the plan ranked second overall - "if QML scrolling of a
200k-line file is 10 % worse the programme is dead".

**The multi-selection workaround has to be inverted.** The plan specified: emit
selection backgrounds as scene-graph rectangles, merge only foregrounds into the
layout formats. Measured against a `QTextLayout::draw(selections)` ground truth,
that is the wrong way round:

- Backgrounds as rectangles **fail**. Geometry from the public API punches
  unhighlighted holes at tabs (23 px and 8 px in the fixture) and at a BiDi
  boundary; a single-rect variant missed 55 px and over-painted 14 px on a
  discontiguous RTL selection. There is no public API for selection-region
  geometry - `addSelectedRegionsToPath` is private.
- Merging foregrounds **and** backgrounds into `QTextLayout::formats()` **works**,
  continuously across whitespace, trailing spaces and tabs, with correct BiDi and
  full line height to within one device pixel. The whitespace hole that
  `texteditorlayout.h` warns about, and that motivated the split, **did not
  reproduce**.

So merge both and patch the one real gap: the `lineHeight/4` newline tail, one
rect off `naturalTextRect().right()`. Cost is a range flattener plus re-layout of
a line when its overlays change, measured at ~25 us per line.

**The render-thread rule is confirmed, not assumed.** Layout in `updatePolish()`
ran clean; deliberately laying out in `updatePaintNode()` produced exactly the
cross-thread QObject and QTimer failures the plan predicted. Creator's document
layout is QObject-based, so that ordering is mandatory rather than stylistic.

Upstream request, now a convenience rather than a blocker:
`void addTextLayout(QPointF, QTextLayout *, const QList<QTextLayout::FormatRange> &selections, int lineStart = 0, int lineCount = -1);`
or the smaller `QList<QRectF> QTextLine::selectionRects(int start, int length) const`.

## TextViewport: the first editor increment

`src/plugins/texteditor/textviewport.{h,cpp}` turns the QSGTextNode spike into a
real `QQuickItem`. It takes a `CodeDocument` and draws it: nothing else. No
cursor, no input, no gutter, no folding, no marks, no wrapping. It is the
smallest thing that puts Creator's own `TextDocument` - its highlighting, its
font settings, its tab settings - on the scene graph, so that everything after
it is an addition rather than a rewrite.

The three spike conclusions are implemented literally, and each is pinned by a
test that fails when it is undone:

- Layout happens in `updatePolish()`, never in `updatePaintNode()`.
- Text nodes are re-emitted every frame; there is no per-line node cache.
- Selection foreground *and* background are merged into
  `QTextLayout::formats()`, with one `QSGRectangleNode` for the newline tail.

**It does not wrap, and that is load-bearing.** Uniform line height is what
makes "which line is at this scroll offset" arithmetic instead of a search, and
what keeps the visible window O(visible) rather than O(file). Wrapping needs a
height cache and is a later increment; adding it before the cache exists would
quietly make scrolling O(file).

**Three bugs the tests found, all in reading `FontSettings`:**

- `lineSpacing()` already returns pixels *and* has the zoom applied.
  Multiplying it by `QFontMetricsF::height()` and dividing by 100 - the obvious
  reading of a "line spacing percentage" - gives a line height off by the font
  height.
- `font()`, on the other hand, is the *unzoomed* font. Using both as they come
  makes the glyphs and the line height disagree at any zoom other than 100 %:
  the text is the wrong size for its own rows. The viewport applies the zoom to
  the point size itself.
- **The editor background is a brush, and it has to stay one.**
  `formatFor(C_TEXT).background()` is the *scheme's* colour and is invalid when
  the scheme sets none, which is the default. `toTextCharFormat(C_TEXT).
  background()` - the route all 11 sites in `texteditor.cpp` take - is a
  `QBrush`, and when the scheme sets none it is `Qt::NoBrush`. The widget editor
  fills with that brush, so "no background" paints nothing and the widget's
  palette shows through. Take `.color()` off it, as a QML colour property must,
  and `QBrush().color()` hands you **opaque black**. Both obvious readings are
  wrong in different directions: one gives an invalid colour, the other gives a
  black editor. The viewport asks the theme when the brush is `NoBrush`.

The first two are invisible at the default zoom in a screenshot. The third is
the interesting one, because the first assertion written for it - valid, and
alpha 255 - passed on the broken code: black *is* valid and opaque. Only
comparing against what the widget would actually paint caught it. That is the
rule the whole port runs on restated in one property: the Quick side has to
match the widget side's behaviour, so assert against the widget's behaviour,
not against a plausible-looking invariant.

**The visible window is exact, not generous.** `int((scrollY + height() - 0.001)
/ lineHeight)` for the last line. Rounding a fraction of a line up is a
one-line-per-frame leak that no rendering test catches, because the extra line
is off-screen and correct. `testItDrawsOnlyWhatIsOnScreen()` asserts both
directions - `(count - 1) * lineHeight < height` as well as
`count * lineHeight >= height` - on a 5000-line document, so laying out the
whole file and laying out one line too many both fail.

**`CodeDocument` no longer needs a `QQuickTextDocument` to open a file.**
`reattach()` required one because every consumer so far was a `TextEdit`.
TextViewport draws the document itself and has nothing to substitute in, so the
requirement is now a path only. The full TextEditor suite is unchanged by that
relaxation (178 passed, 0 failed with `-load all`).

**The second increment is the coordinate mapping**, `cursorRectangle(position)`
and `positionAt(x, y)`. Nothing above the scene graph can work these out, and
they are what a caret and a mouse need - so they land before either, and QML
gets to draw the caret as a blinking `Rectangle` rather than the viewport
growing a cursor of its own.

They are one question asked in two directions, so the test asserts the
round trip: for every position on the first two lines, including the one at the
end of each line where the caret sits on the newline,
`positionAt(cursorRectangle(p))` is `p` again. That is stronger than checking
either alone, and it is cheap - an off-by-one in the mapping shows up as a click
landing one character away, which is not something anyone notices until they are
editing.

Two edges are stated rather than left to chance. `cursorRectangle()` answers in
item coordinates, so a position that scrolled off screen gets an *empty* rect -
otherwise a caret is drawn at the top of the viewport for a line that is not
there. `positionAt()` is clamped to what is laid out, not to the document, so a
drag that leaves the viewport selects to its edge instead of jumping to the end
of the file.

The viewport does **not** paint its own background. A QML `Rectangle` behind it
does, using the `backgroundColor` property - the colour lives in `FontSettings`
and QML has no other way to reach it, but deciding to fill a rectangle is not
the scene graph's job here. That property is also where the brush-versus-colour
trap above lives, so it is the one place the fallback has to be right.

**`CodeViewport.qml` is the third increment**: everything that is not text.
Background, caret, mouse, wheel and scroll bar, around a `TextViewport`. It is
`CodeView.qml`'s job done for files that might be large - `CodeView` puts the
whole document in a `TextEdit`, this one lays out what is on screen.

The caret is the reason `cursorRectangle` is a *property* and not only the
`rectangleAt()` call. QML has to bind to it, and it moves when the document is
scrolled or relaid out rather than only when the position changes; a binding on
an invokable would never re-evaluate. `rectangleAt()` stayed for arbitrary
positions, and now reads as the inverse of `positionAt()`, which is what it is.

**A negative control that failed to bite, and what it actually meant.** The
scroll bar started as a `position:` binding plus a write-back guarded by
`pressed`. The comment said the guard was there because dragging the handle
assigns `position` imperatively and destroys the binding. Rewriting it the
"safe" way with `Binding on position { when: !pressed }` and then controlling
the old form showed the test still passing - because the premise was wrong.
**Qt Quick bindings are destroyed by a JavaScript assignment, not by a C++
one**, and `ScrollBar` drags the handle by calling its own C++ setter. The
plain binding survives. The `Binding` was reverted: it bought nothing that
could be demonstrated, and an unearned mechanism is worse than none.

What the test does assert, controlled in both directions and after a drag: the
handle follows the viewport, the drag scrolls the viewport, the wheel scrolls
it, and the handle still follows afterwards. Plus a `qInstallMessageHandler`
collector over the whole component lifetime, because a binding loop is a
warning and nothing else - the component still builds and still draws, so a
test only sees one by listening.

**The fourth increment is editing.** Keys move the caret (arrows, Home, End,
Page Up/Down, with Shift extending), and where `readOnly` is off they insert,
delete and split lines. Everything goes through a `QTextCursor` on the
document, so the undo stack, the document layout and the marks on it all see
the change; writing the text out and back would lose every one of those while
the page still looked right.

`readOnly` defaults to **true**. A viewport is a view until told otherwise, so
showing a file cannot accidentally change it - and moving the caret is allowed
either way, because reading a file means moving through it.

Three things the tests pinned that are easy to get wrong:

- **The viewport has to follow the caret.** A caret the view does not follow is
  a caret typing where the user cannot see, which is worse than not moving at
  all. `ensureCursorVisible()` scrolls the minimum that brings its line back.
- **An edit the viewport did not make still has to show.** The indenter, another
  view, a refactoring: nothing tells it but `QTextDocument::contentsChanged`.
  Nothing is cached across that signal - the whole visible window is laid out
  again - so this needs none of the ordering care a widget editor's
  `contentsChange` handler does.
- **Focus stops one level short.** `TextViewport` is an `ItemIsFocusScope`, so
  `root.forceActiveFocus()` in the component gives the root focus and the
  viewport none, and every key is silently dropped. The component focuses the
  viewport itself, and the test asserts a typed character landed, which is what
  makes that visible.

That last one is also why the fixture calls `forceActiveFocus()` and every key
test asserts `hasActiveFocus()` first: without it a focus failure reports as a
wrong value somewhere else entirely.

**A control worth recording, because the second assertion looks redundant.**
Removing the `contentsChanged` connection did *not* break the first external
edit - `CodeDocument` reports `modifiedChanged` for it, and that already
polishes. Only a second edit, which changes nothing else, has the document's
own signal to arrive by. The test makes both, and only the second one bites.
Two guards again, as with widget visibility.

**The fifth increment is the three keys that make it an editor rather than a
text box**, all of which ask the document rather than deciding for themselves:

- **Tab** indents by what the code style says - `TextDocument::indent()`, which
  inserts one indent's worth at a caret and shifts whole blocks under a
  selection. A text box types a tab character; that is precisely what a code
  style is there to override. Shift+Tab unindents.
- **Return** auto-indents through `TextDocument::autoIndent()`, so the new line
  starts where the language's indenter says and the caret lands after that
  indentation rather than in column zero.
- **Undo and redo** are `QTextDocument`'s, so they take back what any other view
  of the same document did too. That is the return on editing through a cursor
  rather than through the text. Both are blocked by `readOnly`: a view has
  nothing to take back.

The tests write the tab settings down themselves
(`SpacesOnly, tabSize 8, indentSize 4`) rather than reading the ones in force,
so the expectations are literals - `"    alpha\n    \nbeta\n"`, caret at 14 -
and not the same arithmetic run twice. Every `TextDocument` carries a
`PlainTextIndenter`, which carries the previous line's indentation over, so
none of this needs a language plugin to be shown.

**The sixth increment answers what blocked the preview: a source, not a file.**
`CodeViewport` needed a `CodeDocument`, whose only input is `filePath`, and a
code style preview's text is an aspect's value that was never a file.

The answer is a sibling, not a mode flag. `CodeDocument` grew a text mode in
none of the obvious ways because its whole vocabulary is files - `save()`,
`reload()`, `opened`, `useLanguageServer` - and four of those are meaningless
for a string. Instead:

- `CodeSource` is what a `TextViewport` draws: one virtual `textDocument()` and
  a `textDocumentChanged` signal.
- `CodeDocument` is a `CodeSource` that opens a file.
- `CodeBuffer` is a `CodeSource` that holds text, with `mimeType` said out loud
  because there is no path to guess it from, and a two-way `text` so an aspect
  can own the value.

`TextViewport::document` is a `CodeSource *`. It binds to the source rather than
to the document on purpose: `CodeDocument` throws its document away and makes a
new one whenever the path changes, so a pointer to the document is good only
until then. Asking the source is always good.

**Two real bugs fell out of writing the first test that asserted a colour.**

- **`TextViewport` was never told when highlighting finished.** It lands as
  formats on the blocks' own layouts, which is not a content change, and the
  viewport copies those formats into layouts of its own. So it drew everything
  in one colour until something else happened to relayout it. The fix is a
  connection to `SyntaxHighlighter::finished`.
- **`CodeDocument` never highlighted anything.** `TextDocument::open()` works
  out the mime type and leaves the highlighter alone; putting one on is the
  editor *widget*'s job, and a document drawn by a viewport has no widget.
  Every page so far paired `CodeDocument` with a `CodeHighlighting` attached to
  the `TextEdit`, which hid it. Both sources now share
  `CodeSource::applyHighlighting()`.

Neither had a test because nothing had ever asserted that what the viewport
draws is *coloured* - only that it has the right text and the right selection.
A whole feature can be absent without a single assertion noticing.

**`highlight` is explicit rather than clever.** `CodeDocument` colours what it
opens by default, and `CodeEditor.qml` sets `highlight: false` because it
attaches `CodeHighlighting` to the same document. Two highlighters both write
the blocks' formats, and the one to keep is the one that is *told* what the
text is: a `.clang-format` file is YAML and is named after neither. The
alternative - having `CodeDocument` notice whether a `QQuickTextDocument` is
attached and stay quiet if so - would have worked and been invisible; a
property that says which page colours its own document is worth the line.

**A control that only one test catches.** Removing the `finished` connection
leaves the *file* highlighting test green: a file is coloured before the
viewport's first frame however the wiring goes. Only the `CodeBuffer` test
fails, because a buffer's colours arrive after it. Both tests are worth having
and only one of them holds the mechanism in place, so the buffer test says so
in a comment - otherwise weakening it would quietly free the connection to be
deleted.

**The seventh increment finishes the plumbing**: `CodeIndenting` and
`CodeViewport` now speak `CodeSource` too, which is what a preview needs before
it can give up its `TextArea`.

`CodeIndenting` only ever wanted the `QTextDocument` - it makes its own
`Indenter` from the language factory and never touches `TextDocument` - so it
gained a `source` beside its `document`, and one `target()` that answers
whichever way it was said. Both ways are held by a test of their own:
`CodeHighlightingTest::testIndentsAQuickDocument` for the `TextEdit`, and a new
buffer test here for the source. Breaking either half of `target()` fails
exactly one of them.

`CodeViewport` now takes a `source` instead of a `filePath`, and no longer
builds a `CodeDocument` of its own. A file is `source: CodeDocument { filePath:
... }`, one line longer and no longer a special case. It was a path only
because a file was the only thing there was.

**Clipboard and select-all come before the preview swap, not after.** The
preview is a `TextArea` today, and a `TextArea` can be copied out of. Swapping
it for a `TextViewport` that could not would be a functional regression on a
page that works - which is the one thing the port is not allowed to do, whatever
else it disables. So the order is: give the viewport what the control it
replaces already has, then replace it.

Select-all and copy sit *before* the `readOnly` guard: reading a file means
being able to take a copy of it. Cut and paste sit after it.

**The clipboard is not the document.** `QTextCursor::selectedText()` separates
paragraphs with U+2029, which is right inside a document and wrong on a
clipboard - pasted into any other application it is a stray character where a
line break should be. The conversion happens where text leaves the document, so
paste needs to know nothing about it. The test asserts the real newline *and*
the absence of U+2029, because a test that only checked the text looked right
would pass on either.

The test borrows the machine's clipboard and puts back what was on it. Running
a test should not cost the user their paste buffer.

**Input methods, and the rule that falls out of them: one QTextLayout, many
views.** Composing text - a dead key, a CJK input method - is shown before it is
committed, and until then it is not in the document. `QTextLayout` has
`setPreeditArea()` for exactly that, so the viewport invents nothing; the
preedit and its formats go on the *per-line copy* the viewport built, never on
the document's own layout.

That is not a detail. **A block's `QTextLayout` belongs to the document, and a
document can be shown by more than one view.** Anything a single view knows -
its selection, its caret, what it is composing - must live on that view's own
layout, or two views of one file fight over it. The viewport reads
`block.layout()->formats()` and writes nothing back, which is what makes it safe
to have several.

**The widget editor does not follow that rule, and it is worth knowing where.**
`WidgetTextControl::inputMethodEvent()` calls `setPreeditArea()` *and*
`setFormats(overrides)` on the shared block layout. So while a widget editor is
composing, that block's shared format list is the composition's overrides and
nothing else - the highlighter's colours are gone from it until the commit. A
`TextViewport` showing the same document at the same time would copy those and
draw that one line with the other view's composition colours.

This is **not fixed**, and the reason is worth stating: the viewport cannot tell
a highlighter range from another view's preedit range by looking at them. The
first attempt was to clip copied ranges to the block's own text length, which
was reverted - it does not address the case that actually miscolours (a preedit
begun mid-line produces a range that lands *inside* this view's text, which
clipping leaves exactly where it was), and the test written for it turned out to
assert nothing. A fix belongs on the widget side, or in an API that hands out
the highlighter's formats for a block rather than the layout's. No page shows
one document in both kinds of view today.

Not ported, listed so the gap is not mistaken for a decision: a context menu,
drag-and-drop of text, the gutter, folding, text marks and annotations,
wrapping, and the extra-selection overlays beyond the primary selection. Nothing
left on that list is something a `TextArea` does and this does not, so the code
style preview can be swapped over without losing anything.

**The code style preview is now the real editor.** `CodeStylePreview.qml` is a
`CodeBuffer` under a `CodeViewport`, with `CodeIndenting` over the same source.
The `TextArea`, the `ScrollView` around it and the `CodeHighlighting` beside it
are gone: a buffer highlights itself, and a viewport scrolls itself.

**What that turned up, which no page had needed before.**
`TextDocument::setTabSettings()` runs `autoDetect()` over the text and keeps
whatever indentation it finds. For a file that is exactly right - the file's own
style is the one to preserve. For a preview it is circular: the text *is* the
demonstration of a style, so detecting the style from it only ever answers
"however it looks now", and the Tab key indented by whatever the snippet already
used instead of by the style being edited.

So the choice moved to where the two kinds differ. `CodeSource::setTabSettings()`
hands them to the document and lets it auto-detect; `CodeBuffer` overrides it and
takes them verbatim. `CodeIndenting` pushes on *every* style signal rather than
only on `currentTabSettingsChanged`, because an editor changing the indent width
may report it as a value change, and one signal arriving instead of another must
not leave the document indenting by yesterday's settings.

**Two census tests changed shape rather than being deleted**, because what they
assert still matters. `testACodeEditorScrollsInsteadOfGrowingThePage` used to
look for a `ScrollView` above the editor; there is none now, so it asks the
viewport whether its `contentHeight` exceeds its `height` - the same property,
more directly. `testTabTypesAnIndentInACodeEditor` used to read `text` off the
editor; the text is the buffer's now, and focus goes to the viewport rather than
to the component around it, which is a focus scope.

**The snippet editor went the same way**, and turned up the one thing a preview
did not need. It has no language and so no code style, and its old comment said
so: "it is what says how wide a Tab is here, which is the global tab settings".
`CodeIndenting` read those live at every keypress; a `CodeBuffer` holds a
document, and a document holds its settings. So a buffer starts on the global
tab settings and keeps following them, until something tells it what an indent
is here - which is what a code style does. After that it is demonstrating that
style and the global settings are not its business.

Testing that took two goes. `TabSettings::setData()` wraps itself in a
`QSignalBlocker` on purpose, so setting the whole struct tells nobody and the
"keeps following" half of the test could not fire. Driving the individual aspect,
which is what the Behavior page does, works. A default-constructed
`TabSettingsData` also happens to match the shipped defaults, so the test has to
move the global settings somewhere distinctive first or it asserts nothing -
which is exactly what the first version of it did.

The snippet editor is tested as the *delegate* rather than through its page:
selecting a snippet in the page's table is a different thing to test and has its
own tests, and driving a `TableView`'s selection model to reach a text field is
a lot of machinery between the assertion and what it is about.

**And one that was missing.** Nothing asserted that editing the preview reaches
the aspect that owns it - the old `TextArea` wrote back on `editingFinished` and
no test noticed either way. It matters: Reset and Format work on the aspect's
value, so a preview whose edits never got there would format the text as it was
before they were made. `testEditingThePreviewReachesTheAspectThatOwnsIt` now
checks both halves - that the aspect is *not* written while the cursor is still
in the editor, and that it is once focus leaves.

## Dragging text, and a paste path that was written twice

The last editor gap that needed no design decision. What made it worth doing
properly is what the first version got wrong.

**A drop is a paste, and the widget already knew how.** The obvious
implementation - insert at the drop point, then `autoIndent()` - is not what
`TextEditorWidget::insertFromMimeData()` does. That function reindents a *block
range*, and it distinguishes four cases: whether the insertion starts at the
beginning of a line, whether the text ends in a newline, and the two together.
It also strips the whitespace the caret was sitting after when text arrives at
a line start without a trailing newline. Reimplementing that from the outside
would have produced an editor where dropping text indents differently from
pasting the same text, and the difference would have been visible on the first
multi-line drop into indented code.

So the per-cursor body moved out to `TextDocument::insertWithIndentation()`, and
both callers use it. That is the same shape as the earlier extractions
(`handleMoveKeyEvent`, the `TextBlockUserData` statics): the *computation* leaves
the widget, the policy stays. The widget keeps its own `MultiTextCursor` loop,
its snippet-overlay accept, its code-assist teardown and - deliberately - its
`!m_autoIndent` early return, which returns before the block-mime substitution.
Folding that early return into the shared function looked like a simplification
and quietly changed what a multi-cursor block paste does with auto-indent off.
It was put back.

**What is in QML and what is not.** The `DropArea` and the `Drag` proxy in
`CodeViewport.qml` do event plumbing only: they hand over a point, a string and
one flag - whether the drag started in this same viewport. Everything that
touches the document is `TextViewport::dropText()`, which is a plain invokable
and is tested directly. A drop that arrives from elsewhere is a copy; one that
started here is a move, and the removal happens in the same edit block as the
insertion so a single undo puts it back.

The move needs one piece of arithmetic that is easy to get wrong and impossible
to notice in a quick manual test: when the drop point is *below* the selection,
removing the text first moves the drop point up by the length of what was
removed. Dropping at the position as it reads before the removal lands that many
characters late. Its negative control breaks exactly one test.

**Where the drag out goes.** `Drag.dragType: Drag.Automatic` with
`Drag.mimeData` on an `Item` that draws nothing. A press inside an existing
selection can no longer collapse it on the spot - it might be picking the
selection up - so the collapse is deferred to the release, and a move of more
than `startDragDistance` turns the press into a drag instead. If the drop lands
back on this viewport, `onDropped` has already moved the text and
`Drag.onDragFinished` must not remove it a second time; a flag set in the one
and read in the other is what keeps those apart.

**Two diagnoses, one right.** Two tests hung for 300 seconds. The first
explanation - that synthetic drag events deadlock against a `QQuickWidget`'s
offscreen window - was invented to fit the evidence and was wrong. Sampling the
hung process said so in one stack:

    ~QScopeGuard -> EditorManager::closeEditors -> saveModifiedDocuments
                 -> saveModifiedFilesHelper -> QDialog::exec()

Both tests dirty the document, and closing an editor with unsaved changes opens
a modal "save changes?" that nothing in a headless run will ever answer.
`closeEditors({editor}, false)` is the fix, and thirteen of the fourteen tests in
that file were already written that way - the one that was copied from happened
to be the one that never modified anything. The lesson is the cheap one: a
five-minute hang has a stack, and reading it costs less than a theory.

**Testing the QML half.** It is testable, and against the real component: a
`QQuickView` loading `CodeViewport.qml`, with `QDragEnterEvent` and `QDropEvent`
sent to the window. That covers the part with no C++ to interrogate - that a
dropped *file* is refused so that whoever opens files still gets it, and that a
read-only view refuses text before the drop rather than after. Seven controls
were run; all seven bit, and the URL one and the read-only one bite on the QML
edit alone, so the plumbing is covered and not just the behaviour.

## Reimplementing a Qt widget: Help > Filters

The first of the two pages that were never aspect-driven, and the one this file
described as "reimplementing a Qt widget against `QHelpFilterEngine`". It was,
and the interesting part was refusing to do it from memory.

**Read the widget rather than remember it.** `QHelpFilterSettingsWidget` lives
in `qttools`, which is not checked out here, so there was no source to read -
only the four functions the documentation lists. Rather than guess at the rest,
a forty-line program linked against QtHelp instantiated the widget, walked its
children and printed them. That gave the exact control set in one run:

    QLabel#filterLabel "Filter" / QListWidget#filterWidget
    QLabel#componentsLabel "Components" / QOptionsWidget#componentWidget
    QLabel#versionsLabel "Versions"    / QOptionsWidget#versionWidget
    QToolButton "Add..." / "Rename..." / "Remove"

A second probe, against a real `QHelpEngineCore` on a temporary collection
file, answered the questions that matter for Apply and could not have been
guessed:

- `applySettings()` returns **true only if something actually changed**, which
  is what the page's `onChanged` hangs off - a page that reported a change
  every time would re-filter the documentation on every OK.
- It leaves the **active filter** alone.
- With nothing registered, the option lists are simply **empty** - there is no
  placeholder row to reproduce.
- The blank row that shows up in a populated component list is a **separator**:
  the widget floats ticked options to the top. That is presentation, not
  behaviour, and the port keeps a stable order instead - a list that reorders
  itself under the pointer is harder to use, not easier.

Two probes, about twenty minutes, and everything after them was writing rather
than guessing. The alternative - porting from a memory of the dialog - would
have got the three panes right and `applySettings`'s return value wrong, which
is exactly the kind of thing no test of mine would have thought to check.

**Shape.** `FiltersAspect` follows `DocsAspect` next door: a plain `BaseAspect`
owning the models, with `apply()`/`cancel()` and an `isDirty()` that compares
the working copy against what the engine holds. Add, rename and remove are the
model's `insertRows`, `setData` and `removeRows`, which is what `TableDelegate`
already calls - so the widget's three buttons and its two dialogs become
editing in place, and the page needs no dialog of its own.

The two option lists are a small component of their own rather than
`MultiSelectionDelegate`, which renders `presentation().options` into a
`Repeater` and does not scroll. Components come from whatever documentation is
registered and there can be a lot of them.

**qmllint earned its keep.** It reported that `display` and `checkState` are
*final* members of `CheckBox`, so the delegate's `required property` of each
name would have shadowed the control's own rather than read the model's - every
box would have drawn unticked, and the page would have looked like it had lost
the user's settings. Same failure as the `ActionModel`/`MenuItem` collision:
**role names have to be prefixed whenever the delegate is a Control.** The roles
are `optionText` and `optionChecked`, and a test asserts they are neither of the
two names that clash, because the QML that would prove it needs registered
documentation to draw anything.

**A skip that was worth removing.** The test for the part that matters - that
the two lists belong to the *current* filter - first shipped as a `QSKIP`: a
test run registers no documentation, so the engine offers nothing to tick. The
honest skip was also a hole over the feature's centre. The fix was a seam
rather than test-only API: `setAvailableOptions(components, versions)` takes the
lists, and `updateAvailableOptions()` is the one line that asks the engine for
them. The aspect never needed to know where the options came from; separating
the two is the same "what it lists versus what it does" split this migration
applies to layout closures, applied to a data source.

Nine controls, all biting on the test that names them.

## A bug report, and resisting the fix that fits it

The first bug reported from someone actually using the Quick editor: the line
with the caret got "a weird full width highlight" - a near-black band across a
white editor, with the line number inside it.

It was two bugs, and only one of them was the Quick editor's.

**The one that was.** The highlight was `x: 0; width: parent.width`, so it
spanned the whole component including the gutter, hiding the current line
number's own colour - which is how the gutter says which line is current. The
widget editor draws this inside its viewport. Worth recording why no test caught
it: **the test asserted the bug.**

    QVERIFY2(highlight->width() > viewport->width(),
             "the highlight does not span the editor");

That was written to match what the implementation did rather than what the
widget does, and it locked the behaviour in. A test written from the code it
tests can only ever confirm it.

**The one that was not.** The colour came from
`toTextCharFormat(C_CURRENT_LINE).background()` - the identical expression the
widget uses. Reading that was enough to say the widget had it too, but not
enough to *know* it, so both were measured under the reporter's own theme by
writing the theme into a scratch settings file. The reason the first attempt
measured nothing is worth remembering: the key is in `[Core]`, not `[General]`.

    light-2024   text bg #fcfcfc   current line #273951   <- both editors
    dark         text bg #000000   current line #232323

Same number in both, so it was never a Quick-editor regression. The cause is
that `light-2024.xml` names no `CurrentLine` at all, and
`FontSettingsData::loadColorScheme()` fills what a scheme omits from
`FormatDescription`'s default - which for this category is
`QPalette::Highlight` blended into `QPalette::Base` of
**`Utils::Theme::initialPalette()`**, the *system* palette. A light scheme under
a dark system appearance therefore gets a dark band on a white editor.

**Calibrating the fix from the schemes themselves.** The first attempt blended
toward the scheme's selection colour and produced `#8395ae` on white for the
Classic scheme - trading a black slab for a blue one. What settled it was
measuring what the eleven schemes that *do* name a `CurrentLine` chose:

    creator-dark 0.06   grayscale 0.05   intellij 0.05   inkpot 0.09
    dark-2024 0.15      dark 0.21        solarized-light 0.22

- all of the way from the background toward the *text*, none of them toward the
selection. So the tint is 0.12 of the way to the text colour (0.07 for a search
scope, which covers far more of the screen), and only two schemes are affected -
`default.xml` and `light-2024.xml` - because everything else names its own.

The test asserts the property across every shipped scheme rather than a colour
per scheme: whatever the current line ends up, it is a *lift* of that scheme's
background, under 0.4 of the way to the text. Its control names both offenders
with their numbers - `light-2024.xml` at 0.80 and `default.xml` at 0.77 - which
is what a good failure message looks like: it says which scheme, and by how much.

## The setting behind the bug report

Fixing the reported highlight was worth doing, and it was still treating the
symptom. The question the report should have prompted first is *why there was a
band at all* - and the answer is that `DisplaySettingsData::m_highlightCurrentLine`
defaults to **false**. On a stock Creator the widget editor marks no current
line. The Quick editor marked one unconditionally, so the reporter was not
looking at a mis-coloured highlight so much as a highlight that should not have
been drawn.

**Measured rather than assumed.** `DisplaySettings` is a checklist of what an
editor draws, so it is also a checklist of what a port can quietly drop. Of the
26 fields, the Quick editor consulted five - `textWrapping`,
`highlightMatchingParentheses`, `highlightSelection`, `displayFileLineEnding`,
`displayFileEncoding`. The rest it ignored, and two of them it contradicted:
`displayLineNumbers` and `displayFoldingMarkers` were hard-coded `true` in
`MainEditor.qml`, so turning either off in Preferences did nothing.

Three are now wired through, with the same shape wrapping already used: an
initial property when the form is built, and a push on
`AspectContainer::changed` so a change reaches an editor that is already open.

**Two lessons about the tests, both about assertions that cannot fail.**

*Geometry is not visibility.* Gating the band on the setting broke nothing: the
existing test asserted `y`, `height`, `width` and `x`, and **an invisible item
has all of those**. It went on passing while the thing it was about stopped
being drawn. It now asks `isVisible()` first, and its control - flipping the
fixture's `highlightCurrentLine` to false - makes it fail.

*A push covers for an initial value that was never read.* The first version of
the new test set the settings, opened the editor, checked the current line, then
changed a setting and checked the numbers. Two controls did not bite:
hard-coding either initial property back to `true` still passed, because the
change in the middle pushed all four values in again and corrected it. The fix
is ordering - assert everything the form was *built* with before touching any
setting, then change them one at a time. With that, all six controls bite.

One control returned exit -6 with no failure recorded, which the harness
reported as "nothing bit". It was a crash, not a pass - the same intermittent
`QImage::toCGImage` abort seen twice earlier - and re-running it gave a clean
`FAIL!` on the expected assertion. A control harness has to treat a non-zero
exit with no parsed failure as a *result it did not understand*, not as a pass.

**Still not honoured**, and listed rather than quietly left: `visualizeWhitespace`,
`visualizeIndent`, `markTextChanges`, `displayAnnotations`, `highlightBlocks`,
`animateMatchingParentheses`, `centerCursorOnScroll`, `scrollBarHighlights`,
`markDiffChangeSigns`, `displayMinimap`, `autoFoldFirstComment`, and the three
`breakindent` fields. Some of these are gates over things the viewport does not
draw at all yet, which is a different job from reading a flag.

## Two more display settings, and a rehighlight that was not earned

Continuing down the list of settings the Quick editor ignored.

**Visual whitespace turned out to belong to the document.** Whether spaces and
tabs are drawn is `QTextOption::ShowTabsAndSpaces` on the *document's* default
text option, not a property of any view - so an editor that never sets it shows
no whitespace however the setting is left, and the viewport needed nothing new
at all. The colour is free: `SyntaxHighlighter::formatSpaces()` puts a
`C_VISUAL_WHITESPACE` format on every whitespace run whenever it runs.

**And that is why the rehighlight is not copied.** `TextEditorWidget::
applyDisplaySettings()` calls `highlighter->rehighlight()` whenever the flag
changes. Reading `formatSpaces()` shows it never consults the flag - the formats
are applied whichever way it is set - so the rehighlight cannot change any
format. It is a full re-run of the highlighter over the whole document on every
display-settings change, which on a large file is not cheap, and it buys
nothing. The Quick editor sets the option and stops.

This was nearly kept out of "match the widget" caution, and the thing that
settled it was the control: removing the rehighlight broke no test, and rather
than accept that as permission, the question was whether any test *could* break.
`formatSpaces()` answers that - no - which turns "the control did not bite" from
a weak result into a reason. A mechanism nobody can write a failing test for is
one to leave out, not one to copy.

**Annotations were a gate over something already drawn**, so the work was one
binding. What took the time was testing it properly.

**The property/outcome split, made explicit.** The first annotation test
asserted `form->property("showAnnotations")` and nothing else. Its control -
removing the gate from the QML - did not bite it; it bit an unrelated typing
test instead, which is how a weak test announces itself. A property assertion
proves the *plumbing* (the editor hands the setting to the form) and says
nothing about the *outcome* (the message is not drawn). Both are worth having,
and they belong in different places:

- `TextViewportTest` asserts the outcome: a real `TextMark` with a
  `lineAnnotation`, `showAnnotations: false` on the viewport, and then a search
  of the item tree for a visible item whose `text` is the message. It waits for
  `visibleLine(2).annotation` to arrive *first*, so the absence that follows is
  "the message is here and is not drawn" rather than "the mark has not landed
  yet" - the ordering trick, applied to a negative.
- `QuickTextEditorTest` asserts the plumbing, as one more line in the existing
  display-settings test.

Split that way, all five controls bite the test that names them. Before it, two
of them bit nothing at all.

## Folding the licence header, and a branch that looked dead

Every file in this repository opens with a licence header, so an editor that
does not fold it starts every file several lines below where the widget editor
starts. `m_autoFoldFirstComment` is on by default, which makes this one of the
more visible of the settings still unread.

**Another extraction, same shape as the others.** `TextEditorWidgetPrivate::
foldLicenseHeader()` reads as widget code and is not: it touches the
`QTextDocument`, the `TextDocumentLayout`, the highlighter's comment markers and
the `TextBlockUserData` statics, and exactly one line of it -
`moveCursorVisible()` - is the view's. So it moved to
`TextDocument::foldLicenseHeader()` with that line left behind at the widget's
call site, along with `singleShotAfterHighlightingDone()`, whose body only ever
asked the document for its highlighter. Third time this pattern has paid:
`handleMoveKeyEvent`, `insertWithIndentation`, now this.

The waiting matters and is not incidental. Which markers open a comment comes
from the file's *language*, and the language is not known until the highlighter
has run - so the fold is a continuation, not a call. Its control (folding
immediately instead of waiting) fails the test, because at that moment nothing
in the document is foldable yet.

**The branch that looked dead, and was not.** One control did not bite: the
exception that leaves a *documentation* comment open when a file starts with
`/*!` or `/**`. Reading why is more interesting than the control. `docMarker` is
only filled in the `else` of

    if (def.isValid()) { ...markers from the definition... }
    else { commentMarker = {"/*", "#"}; docMarker = {"/*!", "/**"}; }

and `def` comes from `qobject_cast<Highlighter *>(...)`. So the first read was
"the exception is unreachable: no definition means no folding regions means
nothing foldable". That was wrong, and the cast is why. It fails for every
editor with a highlighter **of its own** - CppEditor's, QmlJS's - which do set
folding indents. The branch is live there and dead only on the generic
KSyntaxHighlighting path, which is the one the Quick editor uses.

Which is also how to test it: a `TextDocument` with folding indents set by hand
and no `Highlighter`, i.e. what a language with its own highlighter looks like
to this function. Two tests on the extracted function directly - a `/*` header
folds, a `/*!` header does not - and both controls now bite.

The general lesson is the one from `layouter() cannot fail`: **"this branch
cannot be reached" is a claim about every caller, and a `qobject_cast` in the
middle of one is exactly where that claim goes wrong.** The cheap check is to
ask which types the cast fails for, not whether the code below it looks
reachable.

## Centring the caret, and a test that used the wrong door

`m_centerCursorOnScroll` decides what happens when the caret leaves the window:
scroll by as little as possible, or put the caret in the middle. The viewport
already had both behaviours - `ensureCursorVisible()` did the first,
`gotoLine(..., centerLine)` the second - so this was one branch, reading the
setting where the viewport already reads `highlightSelection` and the two
file-format ones.

**What the failure taught, which is worth more than the change.** The first
version of the test moved the caret with `setCursorPosition()` and asserted the
view had scrolled. It had not: `scrollY 0, caret rect 0,0 0x0`. The property
setter is the *value* and nothing else; `ensureCursorVisible()` hangs off
`setTextCursor()`, which is what moving the caret actually goes through.

Two things made that quick to find rather than slow. The assertion printed the
state it was judging - scroll offset, caret rect, line height, window height -
so the empty rect and the unmoved scroll were in the failure message rather than
behind another run. And the suite's *time* was the first clue: 22 seconds
against a usual 7, which is several `QTRY_*` blocks each waiting out five
seconds for something that was never going to happen. **A suite that suddenly
takes three times as long has failing waits in it, whatever its totals say.**

The fixed test drives `setTextCursor` and checks which row of the window the
caret lands on - the top edge, the bottom edge, or the middle - which is the
difference the setting actually makes. Three controls: removing the gate,
centring on the wrong fraction, and swapping the two branches. All three bite.

## Indent guides: the first of these that needed drawing

`m_visualizeIndent` defaults to **on**, so the widget editor draws indent guides
on every indented file and the Quick editor drew none. Unlike the settings
before it, this one is not a gate over something already there - the viewport had
to learn to draw it.

**The split fell out the same way as the rest.** The depth computation -
`TextEditorWidgetPrivate::indentDepthForBlock()` - is pure: a block's text, the
tab settings, and a walk to the neighbours when the line is blank. It moved to
`TextEditor::indentDepthForBlock()` in `tabsettings.cpp`, and the widget's copy
is now a cache in front of it. That cache is cleared on every content change, so
holding more entries than before (the old code cached only the blank-line runs)
is safe.

The blank-line rule is the part worth having in one place: a line with nothing on
it takes the *shallower* of the depths above and below, so a gap inside an
indented block keeps the guides running through it and a gap between two blocks
at different depths draws guides belonging to neither. Its control - returning 0
for blank lines - is a real failure, not a cosmetic one.

**Where the arithmetic goes.** The widget's paint loop steps `paintColumn` by
`m_indentSize` and `x` by `charWidth * m_indentSize`, so the guide *count* is the
column depth divided by the indent size, rounded up. That division happens in
C++, where the tab settings already are, and the form is handed a count and a
pixel width. QML draws a one-pixel `Rectangle` per guide at `i * indentWidth`,
behind the text: a child with a negative `z` renders under its parent's own
drawing, and the parent here is what paints the glyphs.

**Two fixtures, and picking the wrong one.** The drawing assertions first ran
against `ViewportFixture`, and found nothing. The dump said why in one line: the
tree under it holds exactly **one** item, because that fixture builds a bare
`TextViewport` - the guides are in `CodeViewport.qml`, and a bare viewport has no
QML around it to draw anything. `CodeViewportFixture`, added earlier for the drop
tests, is the one for anything a form draws.

Worth stating as a rule, because the suite now has both: **`ViewportFixture` is
for what the viewport computes, `CodeViewportFixture` for what the form draws.**
A test that asserts a `visibleLine()` value can use either; a test that goes
looking in the item tree can only use the second, and against the first it fails
in the least informative way there is - by finding nothing.

The suite time flagged it before the assertion did, again: 22 seconds against a
usual 7.

## Change marks, and a name taken from the wrong place

`m_markTextChanges` is on by default: the gutter draws a bar beside every line
edited since the file was read. The rule is one comparison - `block.revision()
!= documentLayout->lastSaveRevision` - and the widget's two colours were
constants buried in a lambda inside `paintGutterFrame()`. They are now
`revisionUnsavedColor()` and `revisionRevertedColor()` in `gutterframe.cpp`,
which the widget's lambda calls and the viewport reads, so there is one place
that decides and no second hard-coded red. Worth flagging: these two are the
only colours in the editor that are not themed, and that is inherited rather
than introduced.

**A test failure that was the test being wrong about the product.** Asserting
that saving *clears* the mark failed, and the code was right: saving turns the
bar green rather than removing it. `TextDocument::saveImpl()` sets an edited
block's revision to `-lastSaveRevision - 1`, which is still not equal to
`lastSaveRevision`, so the mark stays and the negative value picks the other
colour. That is Creator's familiar behaviour - red for an unsaved edit, green
for one you made this session and have saved - and it is easy to assert away by
accident.

**Which makes the enum name matter.** The first version called that state
`Reverted`, copied from `GutterFrame::ColorRole::RevisionReverted`. Nothing was
reverted: the line was changed and then written. Inheriting a name from the
colour role would have left the next reader believing the editor tracks undo,
so the state is `Saved`, with a comment saying what the widget's role is called
and why the names differ. **A name copied from the place you found the constant
is not automatically the right name for what you are doing with it.**

**Two more weak tests caught by their controls**, both of the kind that has come
up before:

- Forcing the gate on did not fail the "setting is off" test, because that test
  read `visibleLine()` immediately after editing the document. The line list is
  rebuilt on polish, so it was asserting against the line as it was *before* the
  edit - unmarked whatever the setting said. Fixed with the ordering trick: wait
  for the line to get **wider**, which cannot happen until the rebuild has
  happened, and only then check the absence.
- Swapping the colours in the QML failed nothing, because the test counted bars
  and never looked at one. It now reads the drawn item's `color` and compares it
  with the viewport's own - before the save and after it. A bar of the wrong
  colour is the same bug as no bar.

qmllint caught a third `property-override` in as many batches: `state` already
exists on `QQuickItem`. That is now three delegates - `display`, `checkState`,
`state` - where a required property would have shadowed the control's own.
**Any short, obvious name for a delegate property is probably taken.**

## The right margin, and looking before choosing

The plan after change marks was `highlightBlocks`, on the grounds that it was
the smallest of what was left. Reading it first said otherwise: it needs the
block ranges around the cursor tracked as it moves, a timer, and nested blended
fills - and it is **off by default**, so it is also the least visible thing
left. Announcing a next step is not a reason to take it once the code says it is
the wrong one.

**Half of that reading was wrong, and stood for several batches.** The cost
held - the ranges, the nesting, the blended fills were all really there - but
"off by default" did not: the highlight is drawn on hover with the setting
off, and the setting only widens what counts as a hover. Reading what a
setting gates is a different act from reading its default, and only the second
was done here. See the display-settings audit above.

What the same look found instead: `DisplaySettings` was audited three batches
ago, but **`MarginSettings` never was, and the Quick editor read none of its
five fields**. That is a whole preferences page with no effect - the right
margin, its column, the tint past it, whether the language's own style supplies
the column, and the centred-content width. Auditing one settings struct and
stopping there is how the next gap gets missed; the other two,
`BehaviorSettings` and `MarginSettings`, should have been counted at the same
time. `BehaviorSettings` is read for two of its seven fields, which is the next
thing to look at.

**Two more extractions.** `calcMargin()` was a lambda inside
`updateVisualWrapColumn()` and `blendRightMarginColor()` a file-static in
`texteditor.cpp`; both are now `visibleMarginColumn()` and `rightMarginColor()`
in `marginsettings.cpp`, called by the widget and read by the viewport. The
first carries the rule worth having in one place: the *indenter* gets first
refusal on the column, because a language whose style has a line length of its
own knows better than a number typed into Preferences.

**A bug the test found rather than the change.** Setting the column to 40 left
the margin where it was. The viewport connects to `displaySettings().changed`
and re-polishes, and nothing was connected to `marginSettings()` - so every
margin setting would have taken effect only on the *next* editor opened. The
connection is one call; finding it needed a test that changed a setting twice
rather than once.

**And the rule from last batch paid immediately.** One control returned exit -6
with no parsed failure, which the harness prints as "nothing bit". Re-running it
gave a clean `FAIL!` on the expected assertion. That is twice now: an
unexplained non-zero exit is a rerun, never a pass.

## The behaviour settings, and where the remaining ones actually go

`BehaviorSettings` has seven fields and the Quick editor read two. Reading the
other five before writing anything is what made this batch small, and the
finding is worth more than the change:

- **`smartSelectionChanging` is not the editor's at all.** Its only two readers
  are in `cppeditorwidget.cpp`. It is a C++ feature that happens to be stored in
  a shared struct, so a generic editor has nothing to do with it.
- **`mouseNavigation`, `constrainHoverTooltips` and `keyboardTooltips` all wait
  on the same thing.** The first needs `findLinkAt`, which `TextEditorWidget`
  declares and does not implement - `CppEditorWidget` and friends do. The other
  two need hover handlers, which are registered per editor factory. That is the
  *same* blocker as completion and auto-insertion: services attached to an
  editor factory with no lookup from a mime type.

So the completion registry, still undecided, now gates four features rather than
one. That is the number to weigh it against, and it was not visible until these
were read one at a time.

**Which left `mouseHiding`,** and its own lesson. The rule is
`isMacHost() ? false : m_mouseHiding` - and that means on the machine this is
developed on, **the feature is off and its code path is unreachable**. Three of
four controls came back "nothing bit" for exactly that reason.

The fix was not to accept them. Both rules became functions that can be asked
directly: `hideMouseWhileTyping(settings, platformHidesPointerItself)` takes the
platform as a parameter rather than reading it, and `isTypingKey(int)` is the
modifier rule the pointer has to survive. Both are real seams and not test-only
API - the widget reads the one-argument overload, and the parameterised one is
the same shape as `setAvailableOptions()` on the filters aspect. With them, the
platform exclusion, the setting, and the modifier rule are all covered on a
machine where none of them fires.

**One control still does not bite, and it is stated rather than removed:** that
the viewport's key handler consults the rule at all. On a Mac the handler's
condition is short-circuited by the platform before the rule is reached, so
nothing observable changes when the call is removed. Testing that needs a run on
Linux or Windows. The alternative - a way to tell the viewport it is not on a
Mac - is test-only API, and a seam that exists only to make a test pass is worse
than a stated gap.

## The registry that was already there

Three times this file has said the Quick editor cannot have completion,
auto-insertion or follow-symbol because "there is no lookup from mime type to
provider anywhere". That is wrong, and the correction is worth more than the
code that came out of it.

**There is a lookup.** `Core::IEditorFactory::preferredEditorTypes(filePath)`
walks the mime type's *parents* and returns the factories that claim it - it is
what the editor manager uses to fill Open With. A `TextEditorFactory` is one of
those factories, and it already holds `m_indenterCreator`,
`m_autoCompleterCreator`, `m_completionAssistProvider`, `m_hoverHandlers` and
the comment definition. So "find the language's services for this file" is two
existing pieces and a missing accessor, not a registry that has to be designed.

**But not for every language, and the difference matters.** These use the
factory's slots, and now work in the Quick editor: JSON, Python, Nim, QML/JS,
`.pro` files. **CppEditor does not.** `CppEditorFactory` sets a *document
creator* that builds a `CppEditorDocument`, and that subclass installs its own
indenter and completion provider in its constructor. Reaching those means
building the language's document, and the Quick editor builds its document
before the path is known - so that path stays shut for now, and it is a
different obstacle from the one previously described.

The first version of the test used a `.cpp` file for exactly the reason a reader
would: it is the language anyone would check. It failed, and the failure is what
uncovered the split above. The test now uses JSON, and says in a comment why it
is not C++.

**How the wrong conclusion survived three tellings.** It came from reading
`TextEditorFactory`'s *own* code, where `setCompletionAssistProvider` is plainly
per-factory, and stopping there. What it never asked was the next question -
*given a file, can I find its factory?* - which is answered two files away in
`ieditorfactory.cpp`. A claim of the form "there is no X anywhere" is a claim
about code that was not read, and it should be worth a grep before it is written
down, let alone repeated.

## Auto-insertion, which the viewport was already shaped for

With the factory lookup in place, auto-insertion was the smaller of the two
things it unblocked - and the viewport turned out to have been built for it.
`TextViewport` already owned an `AutoCompleter` and already used it for
backspace, with a comment saying exactly what was missing:

    // Backspace between the two halves of a bracket pair removes both. That is
    // all the base AutoCompleter offers; inserting the closing half is a
    // language-specific subclass, handed out per editor factory.

So the change is `setAutoCompleter()` replacing the base one, and typing going
through `insertTypedText()` instead of `cursor.insertText()`. The closing text
goes in *after* the caret, which is what makes it something to type over rather
than something to delete, and an electric character re-indents through the
indenter the previous batch supplied.

**A duplicate member is a message.** The first attempt added a
`m_autoCompleter` that already existed, and the compiler said so. That was worth
more than an hour of design: the right shape was not "add an auto-completer to
the viewport" but "replace the one it has", and the comment above it had said so
since it was written. Reading what is already there is cheaper than the second
draft.

**The weak-test pattern, for the fourth time.** The control for "the editor
hands the language's completer to the viewport" did not bite, because the test
installed the completer itself before asserting - it did the thing it was
checking. The fix is the same one as with the display settings: arrange the
world *before* the object under test is built, then assert what it did, and
never perform the step you are testing. Worth naming, since it has now appeared
in four different batches:

- assert what was **built**, not what a later push corrected;
- assert what is **drawn**, not what a property says;
- assert what the object **did**, not what the test just did for it.

All four controls bite. `AutoCompleter` has no `Q_OBJECT`, so the test asks it
what it would do with a brace rather than what class it is - which is the better
question anyway.

## Completion, and the sanitizer catching a design mistake

The last of what the factory lookup unblocked. `CompletionPopup.qml` already
existed in `qtcquick`, and the provider was already on the document, so this is
the wiring between them: Ctrl+Space emits `completionRequested()`, the form asks,
and what comes back fills the popup. Choosing replaces the word already typed
rather than appending to it.

**The first version was synchronous, and wrong twice over.** It called
`processor->start()`, read the proposal, and destroyed the processor. For the
provider every text file gets - `DocumentContentCompletionProvider`, which reads
the words in the document - `start()` returns **null** and the answer arrives
later. So the list was always empty, and destroying a processor that was still
working in a thread was a use-after-free. AddressSanitizer said so on the first
run:

    ERROR: AddressSanitizer: heap-use-after-free ... READ of size 8 thread T52

That is worth writing down as a habit rather than a fix: **the test run is
sanitized, so a lifetime mistake in new code is caught the first time it
executes** - but only if the exit code is read. The failure message alone said
"proposed: " and nothing more; the diagnosis was in the ASan block below it, and
a harness that greps only for `FAIL!` would have shown an empty-list bug and
hidden the memory error under it.

The shape it forced is the right one anyway: `requestCompletions()` returns
nothing, `completionsAvailable(candidates, prefix)` is a second event, the
processor is a member that outlives the call, and a new request cancels the old
one.

**A fallback that is not on the factory.** Asking the factory for a completion
provider comes back empty for plain text, because `TextEditorFactory` applies
its default - the document-content provider - inside its *editor creator*, not
by storing it. The Quick editor now does the same, which is why a plain text
file offers the words already in it.

**What is not done:** the popup is offered on Ctrl+Space and not while typing.
Offering unbidden needs a view on how often to ask a language that may be slow,
and `isActivationCharSequence()` is the hook for it. Stated rather than half
done.

## C++ was not blocked either

Last batch ended by calling the C++ case "the single highest-value thing left,
and a real design question": CppEditor keeps its services in a `TextDocument`
subclass built by its document creator, and the Quick editor builds its document
before the path is known. Both halves of that are true. The conclusion drawn
from them was still wrong.

**There is a second registry, and it is the one C++ actually uses.**
`CppEditorDocument`'s constructor does not build its indenter itself:

    ICodeStylePreferencesFactory *factory = codeStyleFactory(Constants::CPP_SETTINGS_ID);
    setIndenter(factory->createIndenter(document()));

and `codeStyleFactory()` is keyed by a **language id**, which
`TextEditor::languageId(mimeType)` maps a mime type to. So the lookup from "this
file" to "this language's indenter" exists, is used by the C++ editor itself,
and is reachable from anywhere. The Quick editor now asks it first and falls
back to the editor factory's creator for languages that registered there
instead - JSON among them.

That makes three registries found by reading rather than designed:
`preferredEditorTypes()` for the factory, `codeStyleFactory()` for the language's
style and indenter, and the document-content fallback inside
`TextEditorFactory`'s editor creator. Each was found only after a claim that it
did not exist.

**Two things the tests caught that reading had not.**

*The code style has to be the language's too.* An indenter reads its tab
settings from `TextDocument::codeStyle()`, and the editor was setting the
generic global one - so the C++ indenter was installed and produced nothing. It
now uses the language's (`CppGlobal`), chosen in one place because
`applyGlobalSettings()` runs again on every settings change and would otherwise
put the generic one back.

*The mime type has to be set before the language is looked up.* The first
version called `configureLanguageServices()` before `configureHighlighter()`,
and the highlighter is what puts the mime type on the document - so the lookup
asked about an empty string. The test still passed its *mechanism* assertions,
because it computed the language id from the file itself rather than from the
document. That separation is what made the failure legible: the lookup works,
the wiring did not.

**A control run whose baseline crashed.** One control reported "nothing bit" and
so did the baseline, with exit -6. Re-running showed the truth: an intermittent
`QCocoaCursor::createCursorData` -> `QImage::toCGImage` SEGV inside
`GenerigHighlighterTests::initTestCase()`, which aborts the whole run **before
any test executes** - about one run in three, in a test this work never touched.
A harness that reads only failures sees zero of them and calls it clean. The
tell is `passed=0`: a run that passed nothing did not pass.

## What is left, read rather than assumed

Three times running, something called blocked turned out to be a registry that
had not been read. So before saying it again about hover tooltips and
follow-symbol, both were read to the bottom. This time the answer holds, and the
*kind* of obstacle is different from the last three.

**Hover handlers are widget-typed in the signature.**

    virtual void identifyMatch(TextEditorWidget *editorWidget, int pos, ReportPriority);

Not "reachable through a widget" - the parameter *is* the widget, and every
implementation dereferences it. All six start with
`editorWidget->extraSelectionTooltip(pos)`, which is widget state, and go on to
`editorWidget->document()`. Making these callable from another editor means
widening the parameter to something both can supply, and that changes every
implementation in six plugins. That is a real design decision, and a sizeable
one.

**Follow-symbol is a method rather than a parameter.** `findLinkAt(const
QTextCursor &, LinkHandler, bool, bool)` takes nothing widget-shaped at all -
its problem is that it is a virtual on `TextEditorWidget`, overridden by
`CppEditorWidget` and five others, with no factory-side or document-side
equivalent to ask. A hidden widget per editor would answer it and would also be
exactly the thing this migration exists to remove.

So the honest list of what the Quick editor still lacks is two items, both
needing an API change rather than a lookup, and both worth a decision rather
than a batch.

**What did not need one: completion while typing.** The hook is already in the
provider - `activationCharSequenceLength()` and `isActivationCharSequence()` -
and the gate is already a setting, `completionTrigger`. A language says which
characters mean "you are about to name something"; the base provider names none,
so nothing is offered unbidden until a language asks for it. Three controls,
all biting.

## The first dialog, and what a census cannot see

With the settings pages done, the next bucket is the 91 dialogs. Compiler
Explorer's compiler popup is the smallest of them and the ideal first one: a
`QDialog` whose entire body is

    Form { compilerSettings.compiler, br, compilerSettings.compilerOptions, ... }

Seven aspects, no behaviour at all - nothing to separate, because there is
nothing it *does*. The port is a `setQmlSource()` on the settings, a form naming
the seven, and a dialog that asks `Core::createAspectForm()` for the body. The
`QDialog` shell stays until what shows it is Qt Quick too; the content is Quick
now.

**Choosing it took longer than doing it, and that was the right ratio.** The
first candidate was `WindowsSettingsPage`, which is the same shape as Help's
Filters - `setWidgetCreator`, a hand-built widget, aspects that already exist.
It was rejected on a fact worth recording: it is registered only on Windows, so
here it can be neither run, nor censused, nor tested. Converting a page blind is
possible and the standing instruction allows it; doing so *when a testable
candidate is one file away* is a worse use of the batch.

**The test, and what it can honestly assert.** The census only walks
`IOptionsPage`s, so a dialog is invisible to it. The test therefore lives in the
plugin - but the plugin does not link Qt Quick, and the QML items turn out not
to be `QObject` children of the form widget, so the item tree is out of reach.
What is reachable without a new dependency:

- that the form is a `QQuickWidget` at all, so a silent fall back to the widget
  layouter would fail;
- its `status` property, which is a `Q_PROPERTY` and so readable by name;
- **every QML warning raised while the form is built**, collected with
  `qInstallMessageHandler`. That last one is what catches the mistake this port
  is actually exposed to: a misspelt `aspects.Foo` loads perfectly well, leaves
  the control out, and would pass a status check. Its control confirms it.

**Verifying a .qbs edit when the tree will not resolve.** The full `qbs resolve`
cannot run here - the bundled qbs submodule needs `Qt.core5compat`, which is not
installed - so "no error mentioned my file" proves nothing on its own. Putting a
deliberate syntax error in the new group and watching qbs report
`compilerexplorer.qbs:46:21 Expected token ';'` proves the resolver reaches and
parses it, which makes the clean run mean something. **A negative control turns
an unusable verification into a usable one.**

## A project panel, and a pattern that was already written down

CMake's project settings panel is the same shape the dialogs are: a body of
nothing but aspects.

    Column { ps.useGlobalSettings, ps, noMargin }.attachTo(this);

**Most of this batch was spent nearly solving a solved problem.**
`AspectContainer::setEnabled(false)` disables every child, and the panel
disables the settings container while the global settings are in use - so a
"use global settings" flag *inside* that container would disable itself, leaving
no way back. That looked like a trap the port had to design around.

It is not, twice over. The flag's declaration says so:

    ProjectExplorer::UseGlobalAspect useGlobalSettings; // not {this}: excluded from toMap/fromMap

and `ProjectCommentsPanel` next door had already met the same question, solved
it, and left a comment naming the exact failure - a flag inside the container
"would be disabled too, leaving no way back". The solution is a *panel
container* holding the flag and the settings container side by side, with a QML
source of its own, reached through `AspectModels.named(aspects.Settings)`.

So the port is that pattern applied: a `CMakeProjectPanel`, a QML form listing
the same settings the General page lists, and
`ProjectPanelFactory::setSettingsProvider()` in place of
`setCreateWidgetFunction()`. Reading the neighbour first would have saved the
detour, and the neighbour was findable by grepping the one identifier the code
already shared.

**The census still cannot see it.** It walks `IOptionsPage`s, and a project
panel is a `ProjectPanelFactory`. That is now two kinds of Qt Quick form outside
its reach - dialogs and panels - each needing a test of its own. (Half of that
is wrong: **there is a panel census**, in `projectpanelfactory.cpp`, and it
covered this port the moment it landed. See the next section.) The test is the
same shape as the CompilerExplorer one: the form is a `QQuickWidget`, its
`status` is Ready, and no QML warning was raised while building it. The last
check is the one that matters here, because the panel reaches every setting
through a *second* name (`aspects.Settings.Foo`), which doubles the chances of a
typo that loads cleanly and silently omits a control. Its control confirms it.

## The panels are finished, and a census already said so

Before porting more panels one at a time, the question was whether the QuickUi
census could be widened to cover them the way it covers pages. It can - and it
did not need to be, because **that census already exists**:
`ProjectPanelFactoryTest::testPanelsThatSayWhatTheyShowRenderWithQuick()`, with
a project fixture of its own. Running it:

    17 panel(s) say what they show; 0 still build a widget:

So the panel bucket is **done**, CMake's included, and last batch's claim that
"the census cannot see panels" was half wrong - the *QuickUi* census cannot, and
ProjectExplorer's own can. `ProjectPanelFactory::aspects(Project *)` exists for
exactly this, and its header says so: what a panel shows "can be asked for
without opening it, which is what lets a test check the panels the way one
checks the pages".

The lesson repeats: before building an instrument, grep for the one that is
already there. That is now four times in this migration.

**Re-measuring the dialogs** the same way gives **77** files that define a
`QDialog` and use `attachTo`, not the 91 the earlier count suggested (that
number was `.attachTo(` call sites in dialog-and-wizard files, which is a
different thing).

## A dialog that the editor work paid for

`CppPreProcessorDialog` is the first dialog whose body was not already aspects:
it wrapped a `SnippetEditorWidget`, which is a `TextEditorWidget` subclass. That
would have been a blocker three weeks ago. It is not one now - `SnippetEditor.qml`
already exists, built for the settings pages, and it hosts a `CodeBuffer` and a
`CodeViewport` for exactly this: a string aspect edited as code, with a
language's highlighting on it.

So the port is: one `StringAspect` in a container of its own, `TextEditDisplay`,
a form naming it with `mimeType: "text/x-c++src"`, and a dialog that reads the
aspect instead of a widget. The label still names the file, which is what tells
two of these dialogs apart when both are open.

**The control that did not bite, and what it was pointing at.** Removing
`extraPreprocessorDirectives()`'s body failed nothing, because the test asserted
the *aspect's* value rather than the dialog's accessor - it was checking the
thing next to the thing. Replaced with the round trip that matters: seed the
session store, construct the dialog, and ask it. That covers session -> aspect
-> accessor, which is what the caller depends on and what a port from a widget
is most likely to break.

## One check instead of three copies

Three dialogs and panels in, each had the same forty lines of test: build the
form, find the `QQuickWidget`, read its `status`, collect QML warnings and keep
the ones naming the file. Writing it a fourth time was the moment to stop.

`Core::aspectFormRenders(container, qmlFileName)` is that check, once. Two
decisions in it are worth recording:

**Where it lives.** The obvious home was `QtcQuick`, next to
`createAspectForm()`. That was wrong: the plugins that need it link `Core` and
not `QtcQuick`, so the first build after moving it failed to link three times
over, and adding a Qt Quick dependency to a plugin *for a test* is a worse trade
than the duplication it removes. It lives in `coreplugin` beside the function it
wraps, and asks the form by **property name** rather than by type - the
`QQuickWidget` is found by class name and its `status` read through the
metaobject - so no caller has to link Qt Quick either.

**It has its own test**, because it is now what three other tests lean on: no
container, a container naming no QML, and a container naming a file that is not
there.

**Two guards over one case, measured this time.** Removing the status check
broke nothing, which usually means the check is unearned. It is not: the
missing-file case is caught *by* the status check - the test now asserts the
error says `status`, and the first version of that assertion guessed the other
guard and failed, which is how the guess got corrected. With the status check
gone the complaint check catches the same case, because whoever failed to load
the file says so and names it. Both are live; neither one's control can bite
through the other. That is the [[negative-control-two-guards]] shape, and the
way out was to assert *which* guard fires rather than that something does.

**And a control that "did not bite" by crashing.** Removing the "not a
`QQuickWidget`" guard gives exit 134 twice over: without it, the status is read
through a null pointer. A harness that greps only for `FAIL!` calls that clean.
Third time this session; the rule stands - an unexplained non-zero exit is a
rerun, never a pass.

The next dialog's test is now three lines.

## Two dialogs at the new price

With `aspectFormRenders()` in place, a dialog port is the QML, the aspects, and
three lines of test. Mercurial's authentication dialog and Perforce's change
number dialog went together in one batch, which is the first time two have.

Both are the *other* kind of dialog - raw widgets, not aspects - so both needed
aspects introducing. What that buys is visible in what the code stopped doing:

- A `QLineEdit` with `setEchoMode(QLineEdit::Password)` became a `StringAspect`
  with `PasswordLineEditDisplay`. The form does not know it is a password; the
  delegate reads `presentation().control` and decides.
- A `QLineEdit` with a `QIntValidator(0, 1000000)` became an `IntegerAspect`
  with a range. The validator is gone: a spin box cannot hold a number outside
  the range, so nothing has to reject one afterwards.

**Two assertions that were wrong about the product, both caught by running.**

*`SecretDelegate` is not the password delegate.* The presentation enum has both
`PasswordLineEdit` and `Secret`, and the second is for a value that lives in the
keychain and has to be fetched - not for a field that echoes dots. Password
fields are `StringDelegate`, which reads `pres.password`. qmllint would not have
caught it: both are real components.

*`IntegerAspect::setRange()` does not clamp `setValue()`.* The test asserted
that setting 2000000 would come back as 1000000, and it does not - the range
bounds what can be **typed**, not what the aspect can hold. The assertion now
says what the port actually guarantees: the presentation carries the minimum and
maximum, which is what the spin box honours, and that is the same guarantee the
`QIntValidator` gave. A test that had been written from the intention rather
than the behaviour would have encoded a promise the code does not make.

Nine controls between the two dialogs, all biting.

## Two more, and a widget that was hiding a decision

Mercurial's revert dialog and Fossil's configure dialog. Both raw widgets, both
now aspects, and the first carried something worth stopping over.

**A checkable group box is a decision nobody wrote down.** The revert dialog was

    auto groupBox = new QGroupBox(Tr::tr("Specify a revision other than the default?"));
    groupBox->setCheckable(true);

with the revision field inside it, and `revision()` returning the field's text.
A checkable group box *disables* its children when unchecked - it does not clear
them - so a user who ticked the box, typed a revision, and unticked it again got
that revision passed to `hg` while the dialog said the default was wanted.

As aspects the two halves have to be named: a `BoolAspect`, and a `StringAspect`
with `setEnabler()` on it. The enabling that the group box implied is now
written down, and the question the group box hid - *what does the field mean
when the flag is off?* - has to be answered rather than inherited. It is
answered `QString()`, which is a **behaviour change** and the right one.

**The rule needed somewhere to live to be testable.** First it was a line inside
`RevertDialog::revision()`, and its control did not bite: a freshly built dialog
has an empty field, so returning it unconditionally gives the same answer, and
the dialog offers no way for a test to put a revision in it. Moving the rule to
`RevertSettings::effectiveRevision()` makes it askable with the flag either way.
Same move as `hideMouseWhileTyping(settings, isMac)` and
`setAvailableOptions()`: when a control cannot bite, the usual cause is that the
rule has no seam, not that the rule is wrong.

**Fossil's is the round-trip kind.** The plugin passes a `RepositorySettings`
struct around and the dialog is only a way of editing one, so the test is that
the struct goes in and comes back out unchanged - plus one assertion about the
inversion, because the struct stores a *mode* (`AutosyncOn`) and the dialog
shows *"Disable auto-sync"*. A port that dropped that would leave every
repository syncing when it had been told not to, and nothing else would notice.

Eight controls across the two, all biting.

## The heavier kind of dialog, and a bug that had nowhere to be seen

Perforce's pending-changes dialog is the first of the tail: a list built from
parsed command output, one row selected, and a button enabled by whether there
is anything to pick. Taken on purpose rather than another small one, because it
is the shape most of the remaining 70 share.

**The shape it settles on.** A custom `BaseAspect` owning a
`QAbstractListModel` and a current row, presenting as `Table` - the same shape
as `DocsAspect` and the filters aspect. The form is a `TableDelegate` and a
`Binding` writing its `currentRow` back to the aspect, so the dialog can ask
which change is selected without asking the form anything. A
`StringSelectionAspect` would have been less work and would have turned the list
into a drop-down; this dialog has always shown a list and the descriptions are
long enough to want the room.

**And the port found a bug that could not have been seen before.** The parsing
was inline, building `QListWidgetItem`s as it went, so nothing could ask what it
had found. Moving it to a function that returns rows made it testable, and the
first test failed:

    Actual   (currentNumber()): 1
    Expected (12345)          : 12345

The expression is `Change\s(\d+?).*?\s\*?pending\*?\s(.+?)\n`. `\d+?` is
**non-greedy**, so it captures a single digit and lets `.*?` eat the rest: every
pending change was listed as "Change 1", and choosing one ran `p4 submit -c 1`.
Verified against the expression directly before believing it - greedy `\d+`
gives 12345, non-greedy gives 1 - and the fix is that one character. Its control
puts `\d+?` back and the test fails.

The general point is worth more than the fix: **code that parses while it draws
cannot be asked what it parsed.** The bug was not subtle and had presumably been
there for years; it survived because there was no seam at which to ask. Every
dialog in this bucket that reads command output has the same property until it
is ported.

## Auditing the bug rather than just fixing it

The `\d+?` that captured one digit was worth asking about elsewhere before
moving on. There are five uses of `\d+?` in the tree; four of them are fine, and
the reason is precise enough to be a rule:

**`(\d+?)` is only wrong when what follows it can also match digits.** In
`qmakeparser`, both `linuxiccparser` cases and `cmakeoutputparser`, the group is
followed by a literal (`:`, `)`) or by `$`, which cannot absorb a digit - so the
engine has to expand the group to reach them. Perforce's was followed by `.*?`,
which can absorb anything, so it never expanded. Checked by running all five
rather than by reading them: the four give `1234`, the fifth gave `1`.

That is a five-minute audit that turns "I fixed a bug" into "I know the bug was
isolated", and it is worth doing every time a defect is found by porting rather
than by a report - a defect nobody noticed is one nobody was looking for
elsewhere either.

## A dialog whose content was a widget, and a plugin with no tests

SCXML's statistics dialog is 36 lines and almost none of it is the dialog: the
body is a `Statistics` *widget* holding three labels and a tree view. Like
`CppPreProcessorDialog`, the work is in what the dialog wraps.

`Statistics` is now an `AspectContainer` rather than a `QFrame`: three
`TextDisplay` aspects and a `BaseAspect` owning the existing model and
presenting as `Tree`. The model, the parsing and the counting are untouched -
what changed is that a form asks the aspect for the model instead of a view
being handed it directly. The dialog is then the usual three lines.

**Two things this shape costs that the earlier ports did not.** A container is a
`QObject` with no parent, so the dialog has to own it - `std::unique_ptr`, and
therefore an out-of-line destructor, because the implicit one needs the type
complete in every translation unit that includes the header. And the plugin had
no tests at all, so it needed its first `addTestCreator` and its first
`qt_add_qml_module`. Both are small, but they are the difference between "port
the dialog" and "port the dialog in a plugin nobody has tested".

## Radio buttons are a value, and two rules that were hiding in accessors

Fossil's pull/push dialog: three radio buttons, two fields, two check boxes, and
five `connect()`s wiring the buttons to the fields' enabled states.

As aspects the three buttons stop being three widgets and become **one value** -
a `SelectionAspect` with `RadioButtons` display - which is what they always
were. The five connections become one function in the constructor, called once
at the end so that the initial state is arranged by the same code that maintains
it, rather than by `setEnabled(false)` sprinkled at construction:

    const auto followLocation = [this] {
        localPath.setEnabled(location() == LocalFilesystem);
        url.setEnabled(location() == Url);
        remember.setEnabled(location() != Default);
    };
    location.addOnChanged(this, followLocation);
    followLocation();

**And two rules moved out of accessors into the settings.** `remoteLocation()`
returns *nothing* for the default location - that empty string is how fossil is
told to use the remote it already has - and `isRememberOptionEnabled()` is false
for the default however the box is ticked. Both were `if` statements inside
getters on a `QDialog`, which is a place a test cannot reach without building
one and clicking it. On the container they are `remoteLocation()` and
`rememberLocation()`, and both controls bite.

That is the third dialog in a row where the interesting logic turned out to be
in an accessor. It is worth expecting: **a widget dialog's getters are where the
rules end up**, because that is the only place left once the layout has taken
the widgets.

## The same dialog twice, and what that is worth

Bazaar has a pull/push dialog too, and it is Fossil's with more options: the
same three-way location question, the same two fields following it, the same
"nothing to remember about a default" rule in the same accessor - plus five
check boxes, a revision field, and a *direction*.

Porting it took a fraction of the time the Fossil one did, which is the first
time in this bucket that a port has been genuinely cheap because of an earlier
one. Two VCS plugins written years apart converged on the same dialog; the
aspects converge on the same container. That is worth expecting for the rest of
the VCS plugins - Git, Mercurial and Subversion all have pull/push-shaped
dialogs.

**The one thing Bazaar's has that Fossil's does not is a mode**, and it is the
cron's rule in miniature. Before:

    if (m_mode == PullMode) {
        m_useExistingDirCheckBox->setVisible(false);
        m_createPrefixCheckBox->setVisible(false);
    } else {
        m_localCheckBox->setVisible(false);
    }

Three `setVisible(false)` calls, in a constructor, deciding what the layout
below them would show. As aspects it is stated the other way round - each option
says which direction it belongs to:

    local.setVisible(mode == PullOrPushDialog::PullMode);
    useExistingDirectory.setVisible(mode == PullOrPushDialog::PushMode);
    createPrefix.setVisible(mode == PullOrPushDialog::PushMode);

and the form lists all six unconditionally. The form is now the same in both
directions, which is what makes it a *list* rather than a program. Its two
controls - dropping either line - bite.

## The third instance, and where a modal prompt has to stay

The prediction from the last batch held: Mercurial's src/dest dialog is the same
three-way location question a third time. Three plugins, three copies, and now
three containers with the same three enums and the same `followLocation()`.
Whether they should become one shared container is a question for after the
bucket is done - a shared thing extracted from three call sites is a much better
guess than one extracted from two.

**One arrangement difference, said once for all three.** A widget form
interleaves each radio button with the field it enables:

    m_localButton, m_localPathChooser, br,
    urlButton,     m_urlLineEdit,      br,

A `SelectionAspect` is *one* value, so its three options are drawn together and
the fields follow underneath. Nothing is lost - each field is still labelled and
still only editable under its own answer - but the rows are not interleaved.
That applies to Fossil's, Bazaar's and this one.

**Where the split had to fall.** `getRepositoryString()` does two things: it
works out which answer was given, and - for the default answer over a remote
scheme - it opens the *authentication* dialog and merges what comes back into
the URL. The first is a rule and moved to the settings as `chosenLocation()` and
`needsCredentials()`. The second opens another modal dialog and stayed in the
`QDialog`, because that is what it is.

The seam is worth naming: **a rule can move to the settings; an interaction
cannot.** `needsCredentials()` is now testable in all four of its cases -
remote-and-default, local, empty, and a location the user typed - and each has a
control that bites. What is left in the dialog is a `->exec()` and the merging
around it, which is honestly untested and is the smallest that part can be made.

## The fourth radio dialog is not the third one again

Worth checking before extracting anything: Git's branch-checkout dialog also has
radio buttons, and it is **not** the location question. It asks what to do with
local changes - stash, move, or discard - so the location shape stands at three
(Fossil, Bazaar, Mercurial) and a shared container would be extracted from those
three, in `vcsbase`, once the bucket is done. Three call sites in three plugins
is a reasonable basis; the fourth radio dialog being unrelated is the thing that
would have made it a bad one.

**What this dialog is really about is state, not layout.** Two methods -
`foundNoLocalChanges()` and `foundStashForNextBranch()` - are called by the
caller *after* construction, and between them they decided six widget
properties:

    m_discardChangesRadioButton->setChecked(true);
    m_localChangesGroupBox->setEnabled(false);
    m_diffButton->setEnabled(false);
    m_popStashCheckBox->setChecked(true);
    m_popStashCheckBox->setEnabled(true);
    m_diffButton->setEnabled(true);

As two booleans on the settings and one `followState()`, the same six become
three rules that can be stated:

- there is nothing to pop unless the next branch has a stash;
- there is nothing to pop *into* if the local changes are being moved across;
- there is no action to choose when there are no local changes.

**And one rule that was hiding in an `&&`.** `discardLocalChanges()` was

    return m_discardChangesRadioButton->isChecked() && m_localChangesGroupBox->isEnabled();

The group box's enabled state was standing in for "there are local changes" -
so the getter asked a *widget* whether the world was in a particular state. With
no local changes the discard option is the one left selected, and without that
second clause the caller would be told to discard changes that do not exist. It
is now `action() == DiscardChanges && m_hasLocalChanges`, which says the same
thing about the world rather than about a group box, and its control bites.

That is the fourth dialog running where the rule lived in an accessor, and the
second where a *widget's* state was being used as a fact about the model.

## An audit that came back inconclusive, and said so

Having found "a widget's state used as a fact about the model" twice, the same
treatment as the regex bug: grep for it. `return ...->isEnabled()` and
`isVisible()` appear 38 times, and nearly all are legitimate - asking whether a
popup is showing is a fair question *about the popup*. Narrowing to dialogs
leaves two:

- `QnxDeployQtLibrariesDialog::closeEvent()` - `// A disabled Deploy button
  indicates the upload is still running`. The comment admits the trick, so it is
  a documented smell rather than a latent bug.
- `InsertVirtualMethodsDialog` writes `overrideReplacement` only
  `if (combo && combo->isEnabled())`, using enablement to mean "the user opted
  in" - while writing `overrideReplacementIndex` unconditionally one line above.

Neither can be shown to *misbehave* without more digging, and that is the honest
result: unlike the `\d+?` audit, where the other four uses were proved safe by
running them, this one ends at "a smell that porting removes". Worth recording
the difference - an audit that cannot conclude should say so rather than round
up to a finding.

## Two paths and a kit

The perf load dialog: two line edits with a Browse button and a handler each,
plus a `KitChooser`.

The two pairs become two `FilePathAspect`s and **the browsing disappears** -
both handlers, both buttons, both connections. That is the clearest example so
far of the migration removing code rather than moving it.

The kit needed no new aspect either: `StringSelectionAspect` has a fill
callback, which is how Compiler Explorer lists compilers, so listing kits is the
same three lines. A "choose a kit" aspect looked necessary and was not.

**Two assertions that could not fail, found by their controls.**

*Asserting the control says nothing about the kind.* `presentation().control ==
PathChooser` holds for every `FilePathAspect` whatever it looks for, so the
control on `setExpectedKind()` could not bite. The meaningful assertion is
`pathKind`, and with that the *directory* one bites.

*And the file one still did not*, because `PathChooserKind::File` is already
`FilePathAspect`'s default - the line was redundant. It is removed, and the
assertion stays: that a trace is a file is this dialog's requirement, and if the
aspect's default ever changed the test should notice. **A line no control can
defend is either redundant or untested, and the two need telling apart** - here
it was the first.

## The terminal spike: go, with the cleanest split in the tree

**Status update: the spike is being productised.** `TerminalQuick` is now the
third product of the terminal solution - `TerminalSolution::TerminalQuickItem`
linking `TerminalModel` and Qt::Quick only - with the spike's manual test moved
in-tree (`tests/manual/quick/terminal/`, selftest/benchmark/demo modes). Landed
on top of the port: grid-exact per-run rendering (the measured 12.281 px CJK
drift is now 0.000, at one to two percent frame cost), the widget view's full
selection state machine with per-platform clipboard behaviour behind the same
virtual seams, and search-hit highlighting over the SearchHit seam with the
current hit distinguished the widget's way. The selftest is 18 causal steps
against a live zsh. In progress: IME preedit, links, zoom, password mode, the
hosting setters, and a verdict on wavy/dashed underlines through QSGTextNode.
Remaining after that: wiring into Creator's terminal plugin, which is gated on
the shell interfaces rather than on this component.

The integrated terminal was spiked the same way as the editor: a standalone
`QQuickItem` over the **unmodified** `TerminalSolution::TerminalSurface`,
measured rather than reasoned about. Verdict: **TerminalLib can serve a QML
shell with the widget view untouched.** The model layer (`terminalsurface`,
`celliterator`, `keys`, `scrollback`) compiled, linked and ran in a binary
with no QtWidgets at all - verified with `otool -L` - and an 11/11 scripted
selftest against a real forkpty zsh covered keyboard round-trip through the
existing `Keys` encoding, DECSCUSR cursor styles, altscreen, live
`seq 1 50000`, scrollback navigation, CJK round-trip, and the
`SurfaceIntegration` seam (title, clipboard). Rendering is one `QTextLayout`
per visible row (cell runs as `FormatRange`s, backgrounds merged - the same
inverted-merge approach the editor spike validated), laid out in
`updatePolish()`, emitted through `QSGTextNode::addTextLayout`.

Measured on the same machine and window as the editor spike (193x59 grid,
Menlo 12, Release): full-grid restyle every frame holds 120 Hz at ~6.8 ms
CPU; scrollback sweeps lock to vsync. Two honest costs:

- **Wide characters drift off the strict cell grid** - fallback-font advances,
  12.28 px max on a five-CJK-char row, 0.00 for ASCII. The widget's per-cell
  `drawGlyphRun` cannot drift. A production item splits rows into per-run
  layouts positioned at exact grid x, or accepts Konsole-style drift.
- **`dataFromPty()` is synchronous on the GUI thread** (~20 ms per 64 KB of
  scrolling output). Blast-feeding 256 KB chunks drops to ~11 fps while
  churning. The widget shares this property behind its 33 ms flush throttle;
  in Quick the frame loop coalesces naturally, so the throttle is unneeded -
  but feeds must stay at real-pty chunk sizes (<= 64 KB).

**The TerminalLib split** (the cleanest candidate in the tree): `TerminalModel`
= the four model pairs plus `surfaceintegration.h`, plus `SearchHit` and
`defaultFontFamily()/defaultFontSize()` moved out of `terminalview.h` (pure
data / QtGui-only; coreplugin's `TerminalSearch` and compilerexplorer need them
without the view). `TerminalWidgets` = `terminalview.{h,cpp}` and
`glyphcache.{h,cpp}` - the glyph cache exists for the per-cell
`QPainter::drawGlyphRun` path and the QML path never touches it. Consumers:
coreplugin's `SearchableTerminal` follows the view; `TerminalSearch` follows
the model.

Unported interaction, with estimates from reading the widget methods:
selection incl. the mouse state machine (~2-3 days, rendering is the same
format-merge as cell backgrounds), IME preedit (~1 day), find-match highlights
(~0.5 day once selection exists), link hover/activation (~1 day). Wavy and
dashed underline rendering through QSGTextNode is **not verified**, and BiDi
is a real open: QTextLayout reorders RTL runs where the widget paints strict
grid order - inferred, no RTL test ran.

One finding worth a follow-up independent of any QML work, measured:
scrollback costs ~8.2 KB per line (40-byte cells x width) with a hardcoded
capacity of 1e8 lines, so a 200k-line build log costs 1.6 GB of RSS in
*today's* widget terminal too.

Spike code and raw numbers live in the spike worktree
(`tests/manual/quick/terminalspike/`, standalone CMake project with
`--bench-model`, `--selftest`, `--canned` and interactive modes), findings in
its `TERMINAL-SPIKE-FINDINGS.md`.

## What the gutter extraction taught us about the viewport

The gutter display list is done and the five paint methods ported cleanly, so the
approach is validated for the easy half. Three findings about the *viewport*
half, which is the one the plan calls the highest pixel-regression risk:

**Roles, not resolved values.** The frame carries `ColorRole`/`FontRole` and
plain geometry rather than a `QColor` or a `QFont`. That is what keeps it
assertable in a test with no widget, and it means a frame does not go stale when
the theme changes - the same property the theme audit had to chase across 160
statics. Do the same for the viewport.

**Ambient painter state is load-bearing.** Pixel identity in the gutter depended
on reconstructing pen, font and anti-aliasing state that the old code threaded
implicitly through one `QPainter`. The viewport has far more of it - clip
regions, composition modes, per-overlay state - so a viewport display list has to
make clip and composition **explicit primitives from day one** rather than
discovering them as pixel diffs later.

**`QStyle` leaks into painting.** `drawFoldingMarker` compares style names and
nudges rects per style. Behind a callback that is acceptable for one marker; the
viewport's overlays and annotations use `QStyle` and `QStaticText` much more
heavily, and if each one becomes a callback the display list degenerates into a
list of `QPainter` closures - which buys nothing. Those need real neutral
primitives.

**And the actual elephant: `QTextLayout::draw`.** The gutter only ever needed
rects, lines and text-in-a-rect. The viewport's text is painted by layouts with
format ranges, so a neutral frame there must either capture glyph runs or keep
`QTextLayout *` alive across the frame - and the latter violates the
no-live-document rule that makes the display list safe to hand to a render
thread. The diff-sign walk already sits on this boundary: it reads `QTextLine`
geometry at build time, which is only safe because build and render happen inside
one paint event today.

That last point is the one to settle before committing to a viewport frame
builder, and it is the same question the `QSGTextNode` spike is measuring from
the other direction.

## Porting hazards found by measurement

Three findings that change what is portable, each found by reading the code
rather than reasoning about it.

**Undo was never behind `setVolatileValue()`.** Each aspect keeps an
`UndoableValue` holding GUI-side state, and every call that records an undo
command sits in a widget signal lambda inside `addToLayoutImpl()`.
`setVolatileValue()` goes the other way, through `volatileValueToGui()`, which
each aspect implements with `setWithoutUndo`. So a setting edited through a Qt
Quick delegate could not be undone while the same edit through a widget could.
Two links were missing, not one: nothing recorded the command, and nothing
carried an undo back to the aspect - the latter is also a widget-side signal
connection. Fixed with `setVolatileVariantValueFromGui()` plus a connection made
in the aspect's own constructor. **Rule: when a widget delegate does something in
a signal handler, check whether the model half depends on that handler existing.**

**`Icon::icon()` cached on the device pixel ratio alone.** It builds tinted
pixmaps from `creatorColor()`, so an icon kept the colours of whichever theme was
current when it was first requested. Removing an outer `static` that stores an
icon is not sufficient on its own, and neither is fixing the inner cache: the
outer fix makes the call happen again, the inner one makes the call return the
right thing. Both are required, which is easy to get wrong in either direction.

**`updateColumn(n)` does not refresh a QML delegate for n != 0.** QML delegates
bind through the column-0 index, while `TreeItem::updateColumn(n)` emits
`dataChanged` only for cell `(row, n)`. `TreeItem::update()` spans all columns and
is fine. The debugger's watch and breakpoint models optimise with
`updateColumn` on non-zero columns, so those panels need their updates widened
before they can be driven from QML. This is the sharpest of the three because
nothing fails loudly - the view just stops updating.

Related, and the biggest porting risk for the debugger views: `canFetchMore()` /
`fetchMore()` is per-item and widely used for lazy expansion. A plain
`ListView`/`DelegateModel` only ever fetches the root level, and Qt Quick's
`TreeView` goes through `QQmlTreeModelToTableModel` whose fetch-on-expand
behaviour differs across Qt versions. Verify per panel. Eagerly populated panels,
kit and device lists and option pages are portable now; `fetchMore`-driven ones
are not.

## Things learned the hard way

**The per-file taint count overstates progress.** The table above counts a file
as clean if it does not itself reach a widget header. The number that decides
whether a split is possible is the transitive closure: a clean file that is
included by a widget-side file, or whose own `.cpp` needs widgets, joins the
widget side. Computing that gave 140 files / 22201 lines clean against
235 / 82240 - a much worse picture than the per-file table, and the reason the
naive split was not landed. Always compute the closure before claiming a
library can be split.

**Removing a widget include from a widely-included header is only worth it if it
changes the closure - and a header's own `.cpp` is part of the closure.**
Dropping the unused `fancylineedit.h` from `aspects.h` compiles and looks like
progress, but `aspects.cpp` includes two dozen QtWidgets headers of its own, so
`aspects.h` stays on the widget side and the measured gain is zero files. The
cost is not zero: 43 files needed an explicit `fancylineedit.h`, and behind them
another 76 needed `<QLineEdit>`, spreading into vendored `src/shared/qbs`.
Over a hundred files of churn for nothing. Removing `<QFontComboBox>` from the
same header *was* worth it - it is a real QtWidgets header that 12 files were
leeching, and the fallout was 12 honest includes.

Always test the hypothesis by recomputing the closure with the include removed
before doing the sweep. The first version of this note asserted `layoutbuilder.h`
was the blocker; `aspects.h` never included it, and the claim survived into a
commit message before being measured.

**Get the taint propagation direction right, and validate the model before
trusting a number.** Taint flows *downward*: a file is widget-side if it includes
a widget header, or includes a file that is. It does **not** flow to includers -
`elidinglabel.h` including `filepath.h` says nothing about `filepath.h`. On top
of that, a class's `.h` and `.cpp` must land in the same library, so taint does
flow both ways across that one pairing. An early version of the measurement
propagated to includers as well, which marked almost everything widget-side and
made every candidate change measure as "zero files gained" - the same wrong
answer for three different hypotheses in a row. Cheap guard: assert known
outcomes before reading results (`elidinglabel.h` must be widget-side, `id.h`
must be clean) and print the actual include path behind each verdict rather than
just a count.

**Never let an automated build-fix loop write outside the repository.** A loop
that scraped `(/Users/\S+\.(cpp|h|mm)):\d+:\d+: error:` from compiler output
matched paths in the Qt SDK as readily as paths in the checkout, and "fixed" four
errors by injecting includes into `qobjectdefs.h`, `qobject.h` and `qvariant.h`
in the installed Qt. The build then failed everywhere with
`'QLineEdit' file not found` from QtCore. Any such loop must anchor its path
pattern to the repository root.


- Installing a theme with `setCreatorTheme()` applies its palette to the
  application, so it needs a `QGuiApplication`. A `QTEST_GUILESS_MAIN` test can
  build and query a theme but must not install an overriding one; it crashes in
  `QGuiApplicationPrivate::setPalette`.

- `qt_add_qml_module` needs `RESOURCE_PREFIX "/qt/qml"`. Without it the module
  lands at `:/<Uri>` which is not on the default import path, so `import` works
  but the singletons come back undefined. Green in a dev tree that happens to set
  an import path, broken when deployed.
- QML singletons need `set_source_files_properties(X.qml PROPERTIES
  QT_QML_SINGLETON_TYPE TRUE)` before `qt_add_qml_module`. Do not check in a
  `qmldir`; CMake generates a better one.
- A class whose base is a template instantiation cannot be QML-registered:
  `qmltyperegistrar` cannot resolve the base. `SelectionAspect` derives from
  `TypedAspect<int>`, so its options go through a model role instead.
- `QtQuick.Controls` `SpinBox` is integer only, and silently truncated a
  `DoubleAspect` from 1.5 to 1.
- Plugin test slots must be named `test*` (`pluginmanager.cpp`, `isTestFunction`),
  or the test plan is empty and there is no output at all. `-test <id>` loads only
  that plugin's dependency closure, so counts seen there are much lower than a
  source-tree grep suggests.
- Moving a declaration without its definition compiles and then fails to *link*:
  the export macro travels with the declaration, so under hidden visibility the
  symbol is not exported.
- `Qt6PrintSupport` declares `Core;Gui;Widgets`, so `Qt::PrintSupport` in
  `PUBLIC_DEPENDS` keeps QtWidgets public regardless of anything else.
- Removing an unused include exposes files that were getting a type transitively.
  Fix at the using site, not by restoring the leak.
- The "77 of ~130 options pages come free" figure is a source-tree count of
  `setSettingsProvider` call sites. `AspectContainer` has no default layouter and
  `IOptionsPagePrivate::createWidget()` asserts one exists, so each of the 93
  `setLayouter` lambdas (83 files, 38 containing raw `new Q…`) defines its page's
  structure. Rendering the flat aspect list proves the model and keeps
  Apply/Cancel working, but does not reproduce the designed layout. That is why
  the `Layouting` backend work is required rather than optional.

## Effort

25-38 engineer-years total; 10-14 for a Creator a developer could dogfood daily
(phases 1-5 plus part of the plugin porting). If the number has to come under 10,
the only credible reduction is leaving `qmldesigner` (200 k lines, 255 widget
files, plus 18.7 k of vendored ADS) and `scxmleditor` on the compat shim
permanently, or dropping them.

## A key size that means nothing on its own

The SSH key creation dialog offers two algorithms and a list of key sizes, and
the two lists have nothing in common: RSA takes 1024/2048/4096, ECDSA takes
256/384/521. `keyTypeChanged()` did the whole job on the combo box - clear it,
add the right strings, select the first, and disable it if empty - which reads
as a widget being maintained rather than as a rule.

As a `StringSelectionAspect` the rule is a fill callback plus one line:

    algorithm.addOnChanged(this, [this] { keySize.refill(); });

**The `setValue` after `refill()` was redundant, and only the control said so.**
The obvious reading is that refilling leaves the value dangling - 1024 is not an
ECDSA key length - so the port set the first of the new sizes explicitly. That
control did not bite, and the reason is in `volatileValueToGui()`: after a
refill the aspect looks its value up among the new entries and, not finding it,
takes the first one. The aspect already did it. The line came out and the
control moved to `refill()` itself, where it bites.

**The entries must carry their value, not just their text.** The first attempt
built the list as `new QStandardItem(size)`, and the test failed on an
assertion that only exists because of the paragraph above: the value is 1024
before the form is drawn and empty after. `itemById()` matches on `data()`, so
items with text alone are looked up, not found, and replaced by
`m_model->item(0)->data()` - which is also empty. The failure is not "the combo
shows the wrong thing", it is *drawing the form silently cleared the setting*,
and it is invisible to any test that renders first and asserts afterwards.
Assert the constructed value, render, then assert it again.

    QCOMPARE(settings.keySize(), QString("1024"));
    QVERIFY(Core::aspectFormRenders(&settings, "SshKeyCreationDialog.qml"));
    QCOMPARE(settings.keySize(), QString("1024"));

**And the dialog read its own labels back.** `privateKeyFilePath()` was

    return FilePath::fromUserInput(m_privateKeyFileValueLabel->text());

- the path was stored nowhere but in the text of a `QLabel`, having been put
there by `toUserOutput()`, and every caller got it back through a round trip
into display form. The public key was worse: a second label holding the first
label's text with `".pub"` glued on, parsed back the same way. Both are now
derived from the `FilePathAspect`'s value, and the public one is computed
(`privateKey.stringAppended(".pub")`) rather than stored at all - with the empty
case answering empty instead of a bare `.pub`, which is what the string
concatenation used to produce.

This is the third widget-as-storage find in the dialog bucket, after the
Perforce `\d+?` regex and the two `isEnabled()` readers, and the only one where
the storage was a *label* - the one widget with no state of its own to speak of,
which is presumably why it looked harmless.

## The hover handlers: an API that only a widget could satisfy

Every tooltip in Qt Creator - a C++ symbol's documentation, an LSP diagnostic,
a colour swatch, a qmake keyword - comes from a `BaseHoverHandler`, and the
virtual they all implement was

    virtual void identifyMatch(TextEditorWidget *editorWidget, int pos, ReportPriority);

so the Qt Quick editor could not show a single one of them. This was written up
earlier as a decision for the user rather than a batch, because widening it
touches nine handlers across six plugins. The instruction came back: **change
the API, and delete widget code if that makes it easier.**

**What the handlers actually use is much less than a widget.** Grepping
`editorWidget->` across all nine:

| handler | uses |
| --- | --- |
| android manifest, cmake | `textCursor()` |
| languageclient | `textCursor()`, `textDocument()` |
| qmljs | `textDocument()` |
| resource preview, profile, colour preview | `document()`, `extraSelectionTooltip()` |
| cpp builtin | `textDocument()`, `document()` |
| suggestion | `suggestionVisible()`, `cursorRect()`, `viewport()`, `insertSuggestion()` |

Eight of the nine need the document, the cursor, and the message under the
mouse. That is `TextEditor::HoverTarget`, which `TextEditorWidget` implements
and `TextViewport` implements too.

**The ninth says so.** The suggestion tooltip cycles and applies the
suggestion, so it is genuinely about a view that has one - and it now casts,
guarded by `suggestionVisible()`, which is a virtual whose base answers false.
A view that cannot show a suggestion is never asked to drive one.

**And one of the eight turned out not to need a widget at all.** The QmlJS
handler took `qobject_cast<QmlJSEditorWidget *>(editorWidget)` and asserted on
it, then used it for `qmlJsEditorDocument()->semanticInfo()`,
`document()->characterAt()` and `extraSelections(CodeWarningsSelection)`. Every
one of those is the *document's*. The cast is now to `QmlJSEditorDocument`, and
`matchDiagnosticMessage()` takes the document rather than the editor.
`CppElementEvaluator` was the same story: a `TextEditorWidget *` member used
for `textDocument()`, `convertPosition()` and the code warnings, all three of
which the document answers.

**The message under the mouse moves to the document.** `extraSelectionTooltip()`
was on the widget, over the widget's own extra selections. Before moving it,
the question worth asking: *which selection kinds actually carry a tooltip?*
Grepping `format.setToolTip` on extra selections gives four sites, and every one
of them is `CodeWarningsSelection` or `DebuggerExceptionSelection` - both
already published document-wide. So nothing is lost by asking the document, and
a view that is not a `TextEditorWidget` can show diagnostics.

(The two `qmljssemantichighlighter.cpp` hits are *highlighter* formats, not
extra selections, and were never visible to this lookup at all.)

**A test that renders after asserting would not have caught the real bug.**
`TextDocument::extraSelectionTooltip()` is asserted before and after a warning
is set, and the negative control that removes the range check bites - but the
control that mattered was the one on `isDocumentWideSelection()`: with
`CodeWarningsSelection` taken out of the shared set, both this test and the
older sharing test go red, which is the honest coupling.

## Hovering in a Quick view: three things Qt does for a widget and not for an item

`HoverHandlerRunner` - the state machine that asks each handler in turn and runs
the one with the highest priority - was a class inside `texteditor.cpp` holding
a `TextEditorWidget *`. It is now `hoverhandlerrunner.{h,cpp}` over a
`HoverTarget *`, and `TextViewport` owns one. What the item has to do for
itself:

1. **Ask for hover events.** `setAcceptHoverEvents(true)`; without it the scene
   delivers nothing.
2. **Time the resting.** A widget gets `QEvent::ToolTip` from Qt once the mouse
   has been still for `SH_ToolTip_WakeUpDelay`. An item gets no such thing, so
   the item runs the timer, reading the same style hint.
3. **Name a widget to put the tooltip on.** `Utils::ToolTip` is widgets, so
   `tooltipParent()` is the hosting `QQuickWidget`, handed over by the editor.

**A comment that was confidently wrong.** The first version mapped item
coordinates to the screen through the host widget, with a comment explaining
that `QQuickItem::mapToGlobal()` cannot be used because the item lives in a
`QQuickWidget`'s offscreen window whose position means nothing. The negative
control on it did not bite, and printing both numbers showed why: they agree to
within half a pixel. Qt keeps the offscreen window's position in step with the
widget. The helper and its explanation came out.

**And the wiring order was a bug the test found.** `configureLanguageServices()`
runs in the constructor and looks the view up through `widget()` - which
`setWidget()` had not yet been called with, so `viewport()` was null and
everything it configured was silently skipped. It survived because the same
function runs again on `filePathChanged`. `setWidget()` now happens directly
after `setSource()`, with a comment saying why nothing may configure the view
before it.

## Four ways an on-screen hover test lies

The hover test took six attempts to make honest, and every failure was
instructive.

**A synthetic event delivered to the item bypasses the property that makes it
work.** `QCoreApplication::sendEvent(item, &hoverEvent)` reaches
`hoverMoveEvent()` whether or not `acceptHoverEvents()` is set, so the control
that turned it off did not bite. Fixed by asserting the property itself, which
is the precondition the scene actually reads.

**A point taken from the view and used half a second later is a point in a
different layout.** `rectangleAt()` and `positionAt()` both read the same
polished snapshot and are always consistent *at an instant* - but the view goes
on laying itself out, and the tooltip timer fires 500 ms after the point was
taken. First seen as `askedAt == 17` (the second block, because the view had
scrolled a line), then as `askedAt == 11` (the next word, because the font had
changed). Waiting with `QTRY_COMPARE` for the two to agree did not help: they
agreed, and then disagreed again.

**The real mouse is a participant.** The last of these was not a layout problem
at all. The editor is a real window on a real screen, and a physical mouse
moving across it delivers hover events that restart the view's timer with a
point of its own - which is the view behaving correctly and the test losing its
question. The fix is to put the pointer back and ask again, up to five times,
with a comment saying so. Six consecutive full-suite runs, and five controls
that all bite.

**A single-block fixture removes a whole failure mode.** Writing the file
without a trailing newline leaves the document one block, so wherever the view
has scrolled to cannot change which block the pointer is over. Worth doing in
any test that is not about scrolling.

**And an exit code of -6 with no totals is not a result.** The final baseline
came back SIGABRT with no test output, which the harness printed as "NOTHING
BIT". It is the intermittent `QCocoaCursor::createCursorData ->
QImage::toCGImage` SEGV that predates all of this work, and it crashes before
the first test runs. Re-run, not diagnosed - but never counted as a pass.

## Follow Symbol: a virtual on a subclass that the Quick editor cannot have

The second flagged blocker. `findLinkAt()` is a `TextEditorWidget` virtual, and
nine languages override it - **on a widget subclass each**. That is the part
that makes it different from the hover handlers: `CppEditorWidget`,
`QmlJSEditorWidget`, `CMakeEditorWidget` and the rest each *are* the language's
editor, and one editor for every language cannot subclass nine of them.

So the answer is not a wider parameter, it is a different place to register.
`TextEditorFactory::setLinkFinder()`, alongside the indenter creator, the
auto-completer creator, the completion provider and now the hover handlers -
looked up by `preferredFactoryFor(filePath)`, which walks the mime type's
parents. `TextEditorWidget::findLinkAt()`'s base implementation consults it
before falling back to `requestLinkAt`, so **a language that registers a finder
can delete its widget override and lose nothing**.

CMake and qmake are converted: `CMakeEditorWidget::findLinkAt` and
`ProFileEditorWidget::findLinkAt` become free functions over a `TextDocument *`,
both overrides are gone, and `ProFileEditorWidget::checkForPrfFile` - a private
member whose only use of the widget was `textDocument()->filePath()` - is a free
function taking the project file.

**Markdown does not fit and was reverted.** `MarkdownEditorFactory` is a plain
`Core::IEditorFactory`, not a `TextEditorFactory`, so nothing would ever find a
finder registered on it and following a Markdown link would have broken
silently. Its override stays. Worth writing down as the shape of the seam's
limit: registration by *factory* reaches only editors built by a
`TextEditorFactory`, and the alternative - registration by mime type - would
reach more at the cost of not matching how every neighbouring service is
already looked up.

**And a test that counted actions had to learn to name one.** Registering the
two Follow Symbol actions broke
`testTheWrapLinesActionTogglesThisEditor`, which asserted
`editor->findChildren<QAction *>().size() == 1`. Counting children is a test
about *how many* actions an editor has, when what it means is *which* action
Wrap Lines goes through: it now asks the command for the action registered
against the editor's own context. The failure was the test being over-specific,
not the change being wrong - but a count of children breaks on every future
action too, so it was worth fixing rather than bumping to three.

Remaining: C++, QmlJS, Nim, qbs and Compiler Explorer. Compiler Explorer's is
the only one that reads view state (`extraSelections`), so it may need the same
treatment the diagnostics did.

## The other five languages, and the two that do not fit

QmlJS, qbs and Nim follow CMake and qmake off their `findLinkAt` overrides.
Each was the same shape - a widget virtual using only the document - with one
wrinkle apiece:

- **QmlJS** reached the model manager through an `m_modelManager` member that
  is `ModelManagerInterface::instance()`, so the free function just asks the
  singleton. Its `qmlJsEditorDocument()` becomes a cast of the document it is
  handed. The function is exported, because qbs calls it.
- **qbs** is QML plus the qbs language server. Its override called
  `QmlJSEditorWidget::findLinkAt` and wrapped the callback; the free version
  calls `findQmlJSLinkAt` and wraps the same way.
- **Nim** kept the outstanding nimsuggest request, its callback and the
  temporary file it wrote as *widget members*. There is no widget now, so the
  request and the file are captured by the connection that waits for the
  answer, which is where their lifetime actually belongs. One static holds the
  pending request, because nimsuggest answers one question at a time.

**C++ does not fit yet, and the reason is one layer down.**
`CppEditorWidget::findLinkAt` calls
`CppModelManager::followSymbol(CursorInEditor{cursor, filePath, this, textDocument()}, ...)`,
and `CursorInEditor` carries a `CppEditorWidget *` that the follow-symbol
implementations use. Widening *that* is the next piece of work, not this one.

**Compiler Explorer does not fit for two independent reasons**: its factory is
a plain `IEditorFactory`, like Markdown's, and its finder reads
`extraSelections(AsmEditorLinks)` - a view-local kind that is not published to
the document, unlike the diagnostics.

## A census for five conversions with no tests between them

Breaking each language's `setLinkFinder()` call in turn and running that
language's own suite: **all four reported NOTHING BIT**. None of these
languages has ever had a follow-symbol test - the behaviour was a widget
virtual, and nobody wrote one. So the conversion was, at that point,
five untested edits.

`testEveryConvertedLanguageRegistersALinkFinder` is the answer: for
`CMakeLists.txt`, `project.pro`, `Thing.qml`, `project.qbs` and `module.nim`,
`preferredFactoryFor()` finds a factory and that factory has a finder. It does
not test what the finder *answers* - that code is unchanged and was untested
before - but it tests exactly what changed, and it exercises the mime-parent
walk that the lookup depends on. Four of the five controls now bite.

**The fifth cannot, and it is worth saying why.** `QbsEditorFactory` derives
from `QmlJSEditorFactory`, whose constructor registers the QML finder - so
removing qbs's own registration leaves the *inherited* one in place and the
census sees a finder either way. The failure mode is a silent downgrade to the
QML-only answer rather than no answer at all, and distinguishing the two would
mean comparing `std::function`s, which cannot be done. Recorded rather than
worked around.

**And the CppEditor suite is why per-class runs exist.** Running it whole after
this change gave 13 failures, then 111, then 114 - the same binary, three runs,
with an abort on two of them. The classes that failed are green on their own
(`LocatorFilterTest` 15/15; `ModelManagerTest`'s one failure is
`projects.open()` returning false, with 29 of its tests skipped for want of a
kit). `FollowSymbolTest`, the only class that exercises what this change
touches, is 154/154. A whole-suite number from CppEditor is not evidence about
anything.

## C++ follow symbol: the widget was a presentation detail after all

`CppEditorWidget::findLinkAt` looked like the hardest of the nine, because it
goes through `CursorInEditor`, which carries a `CppEditorWidget *`. Reading
what that pointer is *for* changed the estimate: every use is
`->semanticInfo()` or `->updateSemanticInfo()`, and `CursorInEditor` already
has a `cppDocument()` that `BuiltinModelManagerSupport::followSymbol` falls
back to when there is no widget. Half the widget-less path was already written.

What was missing was in `FollowSymbolUnderCursor::findLink`:

    CppEditorWidget *editorWidget = data.editorWidget();
    if (!editorWidget)
        return processLinkCallback(link);

- an early return that threw away the answer. Below it the widget is used
exactly twice, and both are things only a widget can put on screen: the
preprocessor popup for a macro defined in the editor configuration, and the
assist popup that offers the overrides of a virtual. Guarding those two and
deleting the early return is the whole change. A view without a widget gets the
link; it does not get the two popups.

`CppEditorWidget` keeps its own `findLinkAt` - it can show both, and it also
has the "follow a leaf symbol into Designer" wrapper and `followUrl()`, which
reads the widget's last semantic info. So C++ has two entry points now: the
widget's, unchanged, and a registered finder for everyone else.

**With clangd - what most users run - none of this matters**, because
`ClangModelManagerSupport::followSymbol` asks the language server through
`symbolSupport().findLinkAt(document, ...)`, which never wanted a widget.

**The test is in CppEditor, not TextEditor**, because it needs the code model
to have parsed the file, and `TestCase::parseFiles` lives there. It opens a
`CppEditorDocument` with no view of any kind, asks
`TextEditorFactory::linkFinderFor()` for the finder the Quick editor would use,
and follows three things: an `#include`, a type name and a macro.

**And one line has no control that bites, for a reason worth recording.** The
finder passes `CppModelManager::snapshot().document(filePath)` as the
`CursorInEditor`'s cpp document. Replacing it with a null pointer changes
nothing for any of the three follows - `findLink` re-derives the same document
from the snapshot itself at the top of its own body. The argument is the
correct value and the not-yet-wired `switchDeclDef` and `findParentImpl` read
it, but no test today can tell it apart from nothing. Written down rather than
deleted, and rather than pretended to be covered.

## Ctrl+click, and a QML branch that no C++ test can see

F2 was enough to prove the seam; it is not enough to *use* the editor, because
following a symbol is a Ctrl+click for most people. `followSymbolAt(position)`
takes the position under the pointer rather than the caret's, and **answers
whether it took the click** - a file whose language has no finder has to fall
through to putting the caret down, or Ctrl+click stops working in plain text.

Three controls, and the third is the interesting one:

- following the caret instead of the pointer: bites.
- returning true when there is no finder: bites.
- **the QML never calling `followSymbolAt` at all: did not bite**, because the
  test called the C++ method directly. Adding `QTest::mouseClick(quick,
  Qt::LeftButton, Qt::ControlModifier, ...)` through the hosting QQuickWidget
  makes it bite. A synthetic click is instantaneous, so none of the hover
  test's timing problems apply - the only care needed is the same proportional
  y (`lineHeight() / 2`) so the point is line one whatever the font is.

The general form: **a C++ test of a Q_INVOKABLE proves the method, not the
binding.** Every QML branch that decides whether to call it is invisible until
something drives the form.

## CDB Paths, and a bug that explains why nobody used the accessor

Three `IOptionsPage`s still build a widget: the two CDB ones, Windows App SDK,
and Designer's (which wraps `QDesignerOptionsPageInterface` and is not ours to
port). CDB Paths is the first of them.

It is two `StringListAspect`s and three buttons, and the buttons are where the
substance is. "Set up Symbol Paths..." asks whether to use a cache, a server or
both, and then writes the paths - and **which entry carries the cache
directory depends on the answer**: with both, the cache entry names it and the
server entry does not; with only the server, the server entry names it.
Repeating it would make CDB cache into the same directory twice; omitting it
from a server-only setup means symbols are downloaded and never kept. That was
four lines inside a widget slot and is now `symbolPathsToAdd()`, with a test.

**The dialog was pre-filled with a symbol path where a directory belongs.**
`setupSymbolPaths()` did

    path = FilePath::fromString(currentPaths.at(indexOfSymbolServer));

- the whole `srv*C:\cache*http://msdl.microsoft.com/download/symbols` string,
offered as a directory to write a cache into. `indexOfSymbolPath()` has an out
parameter that extracts the cache directory, and the call ignored it.

**And then the test found out why.** Reading the out parameter gave
`/tmp/cache*h`, because the extraction itself is off by two:

    *cacheDir = path.mid(prefixLength,
                         path.size() - prefixLength - qstrlen(symbolServerPostfixC) + 1);

`+ 1` keeps the separator and the first character of the URL. So the accessor
that would have given the right answer never did, which is a fair explanation
for why the caller worked around it by passing the whole path instead. Both are
fixed, and the control that puts either back bites.

This is the fourth widget-as-storage find in this bucket, and the first where
the *reader* was broken rather than the storage - worth telling apart. A value
kept in a widget is bad because the widget can go away; a value kept correctly
but read wrongly looks identical from the outside, and only a test that names
the expected value distinguishes them.

`CdbSymbolPathListEditor` is no longer a widget at all - the class survives as
the four static functions that say how a symbol path is spelled, and the
`PathListEditor` subclass with its three buttons is deleted.

## CDB break events: a table whose rows are fixed

The other CDB page. `CdbBreakEventWidget` built six check boxes and three line
edits, and `breakEvents()` read the CDB command line back out of them - the
same widget-as-storage shape, with the wrinkle that the *rows* are not the
user's: they are the events CDB knows about. Nothing can be added or removed,
so the table is check states and filter text.

That makes it a good fit for `tableModel()`: two columns, the event one
checkable and the parameter one editable **only for the three events that take
a parameter**. `flags()` says so, `EditableRole` reports it to a Qt Quick view,
and `setData()` refuses the write anyway.

**Three encoding rules, and the tests that hold them:**

- an event with no filter is written as `eh`, not `eh:` - the empty filter is
  left off;
- an event that takes no filter never gets one, however the stored string is
  spelled (`ct:nonsense` reads back as `ct`);
- an event CDB does not know is dropped, not carried along.

**Two controls that did not bite, for two different reasons.**

*A guard on a path the test never drove.* Removing the `hasParameter` check
from `setData()` changed nothing, because the test only called `setValue()` -
and `setBreakEvents()` has a *separate* guard on the same rule. The rule is
enforced twice, and the test was only exercising one of them. Fixed by driving
`setData()` on a row the model itself says is not editable, rather than on a
row index picked by hand.

*A fixture that masked the control.* Making `indexOfEvent()` answer 0 for an
unknown event should have turned `{"eh", "zz:something"}` into two ticked
events - except that index 0 *is* `eh`, which was ticked already, so the result
was unchanged. The unknown event has to be tested **on its own**. A test that
mixes the case under test with a case that would produce the same answer cannot
tell them apart, and this one looked fine for two rounds.

With both pages ported, the only `setWidgetCreator` left in the debugger is
`DebugModeWidget`, which is an `IMode` and not a settings page. Of the pages
that still build a widget anywhere, Windows App SDK remains, and Designer's
wraps a `QDesignerOptionsPageInterface` that is not ours to port.

## Windows App SDK: the last ordinary page that built a widget

Three path choosers, two download buttons and a summary. The choosers were the
storage - the `validate*()` slots read them and wrote the aspects - so with
aspects those slots keep only the half that reports, and the widget class stops
being a widget: it is the settings container's private part, holding the two
downloads and nothing to draw.

**Two rules came out of it, and both are about the SDK not being where its name
says.**

`hasWindowsAppSdkPackage()` - the summary row reads "Windows App SDK path
exists.", but the check was never that the path exists. It is that a
`Microsoft.WindowsAppSDK.*.nupkg` is *in* it. A directory named after the SDK
with nothing in it is not the SDK, and the label is misleading about a check
that is right.

`windowsAppSdkPackageDir()` - NuGet unpacks into a versioned directory of its
own under the download path, so after a download the SDK path has to become
that directory rather than the one it was downloaded to. That was six lines of
`QDir::entryList` inside a task-tree callback.

**"All changes on this page take effect immediately" had to go.** The widget
wrote each aspect as the chooser changed and persisted on Apply. A container
rendered by `setSettingsProvider` has to `setAutoApply(false)` - the apply path
asserts on it, because a page whose aspects auto-apply is never dirty and Apply
silently does nothing. So the page is deferred like every other, the download
buttons read `expandedVolatileValue()` so they act on what is typed rather than
on what was last saved, and the two downloads apply and write the container
themselves once the files are on disk. The label is deleted rather than left
saying something that is no longer true.

**And the fixture masked a control for the second time this session.** Making
the unpack glob match any directory changed nothing, because the only directory
in the fixture *was* the right one. Adding an `Extras` directory - which sorts
before `Microsoft...`, so a search that takes the first entry rather than the
first match answers with it - makes it bite. Same shape as the CDB unknown
event: the wrong answer and the right one coincided for the input chosen.

With this page ported, every `IOptionsPage` in Qt Creator that can be aspect
driven is. The one left is Designer's, which wraps a
`QDesignerOptionsPageInterface` supplied by Qt Designer - not ours to port.

## Where the page migration actually stands, and the one thing left

An audit rather than a batch, because the answer changed what was worth doing
next. Snippets and Font && Colors - the last two named in workstream 3 - turn
out to be ported already; both call `setQmlSource()`. So does every code style
factory: C++, Nim, QmlJS and the project panel.

What is left is **one** page, and it is not on the list because it hides behind
another one. `ClangFormatCodeStylePreferencesFactory` replaces the C++ factory
by registering with the same id, and it is the only factory that still calls
`setValueEditorCreator()`. So with the ClangFormat plugin loaded - which is the
default - the C++ Code Style page falls back to `CodeStyleAspect`'s widget
layouter. That is why the two remaining `AspectWidgets::setLayouter()` calls
cannot go yet:

- `codestyleeditor.cpp` - the fallback that renders a factory's value editor;
- `tabsettings.cpp` - whose `TabSettingsForm.qml` already lists exactly the
  same six aspects. The closure is only reached through
  `CppCodeStylePreferencesWidget`, and grepping for that turns up **one**
  instantiation, in ClangFormat.

Everything else that still names `setLayouter` is either a test fixture, the
Lua binding that lets a script supply its own layout, or a comment.

## Why that page was not ported in this batch

`ClangFormatConfigWidget` is two text editors side by side: an `IEditor` opened
on the real `.clang-format` file, and a `SnippetEditorWidget` previewing what it
does. The shape is already there for it - `CodeDocument` exists, is QML
registered, opens a file path, and its own header comment names `.clang-format`
as the case it was built for - and the preview half is what every other Code
Style page already draws with `CodeStylePreview.qml`.

The blocker is the build. `add_qtc_plugin(ClangFormat CONDITION TARGET
${CLANG_FORMAT_LIB} AND ...)` - the plugin is not configured here, so
`ninja ClangFormat` is an unknown target. It cannot be built, its QML module
does not exist so `qmllint` has nothing to check, and the page census cannot
reach it. A port written under those conditions is three unverifiable things at
once: that the C++ compiles, that the form names aspects that exist, and that
the two editors behave.

**One of the three is checkable, and worth recording as a technique.** An LLVM
22.1 with `libclangFormat` is installed; taking a sibling plugin's command line
out of `compile_commands.json`, dropping `-c`/`-o`, adding the LLVM include
directory and running `-fsyntax-only` compiles the file against the real
headers. Tried on a draft of the aspect container it found three errors
immediately - `setIconType()` takes `AspectControls::InfoType` and not
`InfoLabelType`, and `ClangFormatGlobalConfig` is constructed per use rather
than reached through a global accessor. So the technique works and bites.

It still leaves the QML unverified, which is the half that fails silently: a
form naming an aspect that is not there renders blank, and only the census
catches it. Recorded as a decision for the user rather than guessed at:
enabling LLVM in the build configuration makes this an ordinary batch.

## Porting a plugin this build cannot compile

The ClangFormat page, from the previous entry - the last one left. The
configuration here excludes the plugin, so the usual three verifications are
all unavailable: no build, no `qmllint` target, no page census. Rather than
write it blind or leave it, three substitutes, each negative-controlled before
being trusted:

1. **C++**: a sibling plugin's command line out of `compile_commands.json`,
   `-c`/`-o` dropped, `-fsyntax-only` and the LLVM include directory added, run
   over every `.cpp` in the plugin. Control: a bogus call in
   `clangformatutils.cpp` - caught.
2. **QML**: `qmllint` with `-I <build>/qml_modules -I <Qt>/qml`, which is what
   the generated `.rsp` files use. The modules the form imports are built even
   though this plugin is not. Control: renaming `BoolDelegate` to a type that
   does not exist - caught.
3. **Aspect names**: the one thing `qmllint` cannot see. Every
   `aspects.X` in the plugin's `.qml` files, checked against every
   `setQmlName("X")` in its `.cpp` and `.h`. Control: `aspects.NoSuchAspect` -
   caught.

`clangformatplugin.cpp` is skipped by (1): it includes its generated `.moc`,
which only exists once the plugin is configured. Said out loud rather than
quietly passed.

**Check (3) earned its place immediately.** Deleting the four widget classes
was two range deletions, and the first one - `ClangFormatSelectorWidget` up to
`ClangFormatConfigWidget` - swallowed the new aspect container, which had been
inserted just above the second marker. The C++ still compiled, because nothing
in the file referenced the container by then. The QML still linted, because
`qmllint` does not know what an aspect is. The name check reported five missing
names on the next run. Without it this would have been found by a user opening
the page and seeing it blank.

## What the port actually changed

`ClangFormatConfigWidget` was two editors: an `IEditor` on the real
`.clang-format` file, and a `SnippetEditorWidget` previewing what it does. The
preview half is what every other Code Style page already draws, so it is now
`CodeStylePreview.qml` like the rest. The file half is a `CodeBuffer` plus a
`CodeViewport` - the same pattern - with a `StringAspect` holding the text.

**`CodeBuffer` rather than `CodeDocument`, and the reason is worth recording.**
`CodeDocument` opens a path and has a `useLanguageServer` property, which is
closer to what the widget did - it registered the document with the language
client manager, so a YAML server could complete in it. But `CodeDocument`
exposes no `text`, so the form cannot hand what was typed back to the aspect;
reaching the document from QML would mean a `Q_INVOKABLE` on the container,
which means `Q_OBJECT`, which means a generated `.moc` - and a `.moc` is
exactly what this build cannot produce for this plugin. So the shape that can
be verified won, and the language-server registration is the price. Worth
revisiting from a build that has LLVM.

Three rules moved out of the widget and into the container:

- what the text on screen would do, or why it would do nothing -
  `checkStyleText()` feeding the warning and the preview, off the *unsaved*
  text rather than the file;
- whether the file may be typed into at all - a read-only style, or "Use custom
  settings" turned off;
- which file is being edited - it follows the style, and switching style has to
  show the new file's contents rather than leave the old ones on screen.

**The widget fallback stays for now.** `CodeStyleAspect`'s layouter and
`ICodeStylePreferencesFactory::createValueEditor()` now have no production
users - only `codestyleaspect_test.cpp`, which exercises them on purpose. They
could go, and deliberately do not: if this port has a runtime fault, that
fallback is the only thing between a user and a blank C++ Code Style page, and
a fault is exactly what cannot be ruled out from here. Deleting it is the first
thing to do from a build that can run the page.

## How far the unbuildable plugin could actually be verified

The previous entry left the ClangFormat port checked three ways and never
built. It can be taken further, and it is worth writing down how far - because
the answer is four of five, and the fifth has a definite cause rather than
being merely unavailable.

The plugin can be **built by hand against the tree that is already there**.
Nothing in the user's build configuration changes:

- `moc` for the one `.cpp` with `Q_OBJECT`, the one header with one, and the
  test source;
- the plugin metadata, `${IDE_VERSION}` and friends substituted from a sibling
  plugin's generated `.json`;
- the QML module faked as a plain resource - `qmldir` plus the two `.qml` files
  under `qt/qml/QtCreator/ClangFormat`, through `rcc`. That is all the page's
  `qrc:` URL needs; only `qmlcachegen` is lost.
- every `.cpp` compiled with a sibling's command line plus the LLVM includes;
- linked against the built `libCore`, `libTextEditor`, `libCppEditor`,
  `libProjectExplorer`, `libUtils` and Homebrew's `libclangFormat`.

**Link strictly - without `-Wl,-undefined,dynamic_lookup`.** With it the link
succeeds while leaving missing symbols to be discovered at load; without it,
every symbol has to resolve. It does: zero undefined symbols. That is a much
stronger statement than the syntax check, and it is the one that says the new
container really does call things that exist.

**The one that cannot be done, and why.** Dropped into the plugin directory,
the plugin fails at `dlopen` - and the first attempt failed honestly and
loudly, on `createClangFormatTest` missing, because the main build has tests on
and the `WITH_TESTS` source had not been compiled. With that fixed it crashes
instead, in `_GLOBAL__sub_I_SPIR.cpp` - a static initialiser inside LLVM's own
static libraries, during `dlopen`, before any Qt Creator code runs. Homebrew's
LLVM 22.1 is built without ASan and for a newer deployment target than this
build targets; this build is ASan. That mismatch is exactly what
`QTC_CLANG_BUILDMODE_MATCH` in the plugin's CMake `CONDITION` exists to
prevent, so the exclusion is correct and the crash is not about the port.

So the page still has never been *shown*. What is now known is that it
compiles, that every symbol it references resolves, that its QML type-checks,
and that every aspect it names exists. What remains unverified is behaviour:
that editing the file re-indents the preview, that the warning appears on
invalid YAML, and that read-only styles stay read-only. Those need an
LLVM whose build mode matches - not a different technique.

The build tree was put back: the hand-built plugin is removed and the census
re-run clean afterwards, so nothing of this is left in the user's build.

## The ClangFormat page, verified - and the bug that found

The previous two entries stopped at "compiles, links, type-checks, names
resolve, never run". It can be run, and the way to get there is worth writing
down because it took one wrong turn and one genuinely wrong assumption.

**A second build, not a changed one.** `WITH_SANITIZE=OFF`,
`CMAKE_BUILD_TYPE=Release`, LLVM on the prefix path, and `BUILD_PLUGINS` cut to
the dependency closure of what is needed - `ClangFormat;Core;CppEditor;
ProjectExplorer;QuickUi;TextEditor`, six plugins rather than a hundred. Nothing
in the user's build directory changes. Two things that are not obvious:

- `BUILD_TESTS_BY_DEFAULT=OFF` *breaks* the configure, on an unrelated manual
  test whose resource list picks up a directory. Leave it at its default.
- `BUILD_EXECUTABLES_BY_DEFAULT=OFF` leaves an app bundle with no executable
  in it, so `-test` cannot run at all. Turn the `qtcreator` executable back on.
- The bundle has no `Resources`, so Core fails with "No themes found in
  installation" and every plugin below it fails to load. Symlink them from a
  full build.

**On macOS the exclusion was never about the build mode.**
`QTC_CLANG_BUILDMODE_MATCH` is only computed on Windows; elsewhere
`FindClang.cmake` sets it ON unconditionally. So the plugin's `CONDITION`
reduces to "was Clang found", and this build simply did not have LLVM on its
prefix path. The ASan crash from the hand-built attempt was real, but it was
not what the condition is about.

**Then the page did not render, and the reason was a real bug in the port.**
`Core::aspectFormRenders()` reported "the form is a widget layout, so the Qt
Quick one was declined". Three wrong guesses later - the wrong container, a
code style that does not exist yet, a missing accessor - the diagnostic that
settled it was printing the class names of what `createAspectForm()` returned:
`QWidget`, `QVBoxLayout`, `CodeStyleSelectorWidget`. A widget layout, from a
container whose `qmlSource()` was set. That can only happen when
`Core::setAspectFormFactory()` was never called - **and it is called by
QuickUi's `initialize()`, so `-test ClangFormat` without `-load all` never
installs it.** The instruction has said `-load all` all along; dropping it
turned a passing page into a failing one and cost half an hour.

With `-load all` the form came back as `QtcQuick::QuickWidget` containing a
`QQuickWidget` - and then the *page* was wrong, in a way none of the three
static checks could see. The form said `root.aspects.ClangVersion`, but
`CodeStyleAspect` hands the language's own aspects over as a **child container
named `Settings`**; every other code style form reads
`AspectModels.named(aspects.Settings)` first. The aspect-name cross-check
passed because `Settings` was in its allow-list and it never modelled the
nesting. The page would have rendered blank.

So the check that mattered was the one that could not be faked, and the
previous entry's claim - "the QML half is what fails silently" - was exactly
right about the risk and wrong about having covered it.

**Where it stands now.** Six tests in the plugin's own suite, all six controls
biting: invalid YAML is reported and the warning clears when it is fixed, a
read-only style is not offered for editing, the form knows which file it is
showing, and the form renders as Qt Quick against the page container. 78
pre-existing ClangFormat tests still pass.

**One thing that is not fixed and is not mine.** The census in the cut-down
build segfaults after 76 passes and 0 failures, in the tree-reordering test.
Removing the ClangFormat plugin and running again gives 76 passes, 0 failures
and the same segfault at the same point, so it belongs to the six-plugin
Release configuration rather than to this work.

## The widget fallback, and what it took with it

With the ClangFormat page verified, the last widget path in the settings pages
had no production user left - only the tests written for it. Removing it is one
dependency chain:

`CodeStyleAspect`'s `setLayouter` closure → `createValueEditor()`,
`setValueEditorCreator()`, `valueEditorHasPreview()`,
`setValueEditorHasPreview()` on the factory → the `CodeStyleEditor` base class
→ `createCodeStylePreview()` and `createCodeStylePreviewNote()` →
`codestyleselectorwidget.{h,cpp}` entirely. Plus the `TabSettings` closure,
whose `TabSettingsForm.qml` had listed the same six aspects for some time and
which was only reachable through `CppCodeStylePreferencesWidget` - a class
ClangFormat was the only user of.

**Deleting a fallback is not the same as deleting a branch.** The closure was
what a language with no Qt Quick form of its own fell back to; taking it away
leaves such a language with *nothing*, and `Core::createAspectForm()` asserts on
a container with neither a form nor a layouter. So the constructor changed
shape rather than shrinking: the selector, the preview and the two preview
actions are now built for **every** language, and a language that names no form
gets `CodeStyleDefaultPage.qml` - the selector and the preview and nothing
else. That is what a Code Style page is without a language's own settings, and
it is a better answer than a widget.

The test that guarded the old behaviour said "a language that names no form
keeps its widget editor". Its replacement says the language gets the default
form and still has a selector and a preview, and that it contributes no
settings of its own. Same distinction, opposite default.

**Three tests went, and the reason each went is different.**
`testValueEditor` and `testLiveWriteEditor` were *about* the two widget-editor
shapes, so their subject no longer exists. `testDelegateCancelReverts` drove a
`QSpinBox` inside one. `testEditMakesDirtyAndApplyCommits` and
`testCancelReverts` looked like the same case but were not - they are about
dirty/apply/cancel, which is still real, and they now edit the language's
`TabSize` aspect directly instead of finding a spin box. No widget, same claim.

**And a substring replace mangled three class names.** Rewriting
`TestCodeStyleFactory factory;` to `PlainTestCodeStyleFactory factory;` also
rewrote `QmlTestCodeStyleFactory` into `QmlPlainTestCodeStyleFactory`, and two
others besides. The compiler caught all three, which is the only reason this is
an anecdote rather than an entry in the bug list - the same edit against a
weaker language would have been silent. Anchor on the whole declaration, not on
a suffix.

Every `AspectWidgets::setLayouter` left in the tree is now a test fixture, the
Lua binding that lets a script supply its own layout, or a comment.

## The widget the fallback was keeping alive

`CppCodeStylePreferencesWidget` said what it was for in its own comment: *"for
ClangFormat's legacy indenter panel, which is still a widget page."* That page
is not a widget page any more, and with the fallback gone the widget had one
user left - a test asserting that the widget renderer also shows one category
at a time. The aspect-level test directly above it makes the same claim without
a renderer, so the widget and its test are deleted together and
`cppcodestylesettingspage.h` stops including `<QWidget>`.

The control that mattered was not on the deletion but on what the deletion left
behind: breaking `group.setVisible(selected)` in `showCategory()` still turns
the remaining aspect test red, so the claim survived losing its second
renderer.

**A rule next to it has no test, and trying to write one turned up something
worth looking at.** `showCategory()` also does

    group.setEnabled(current && !current->isReadOnly());

so a style delegating to a built-in should be shown and not editable. A test
for that fails: with the C++ global style, which does delegate to the read-only
`qt` built-in, the General group reads as *enabled*. Instrumenting
`syncFromReal()` shows the page copy is set up correctly -
`realDelegate "qt" pageDelegate "qt" pageCurrentReadOnly true` - and nothing
else in either file calls `setEnabled` on those groups. So the value is
computed as false and read back as true.

The likeliest explanation, untested: `m_selector.setup()` runs between
`syncFromReal()` and `createSettingsAspects()`, and selecting a default entry
re-points the page copy's delegate at an editable style before the settings are
built. Recorded rather than guessed at - the test was written, failed, and was
**removed rather than committed**, because a test whose premise has not been
established is worse than none. The next step is to instrument
`m_selector.setup()`.

**And two suites went red without this batch touching them.** The indent-guide
test and a QML `hasActiveFocus()` test both started failing between one run and
the next. Restoring the whole `src/plugins/cppeditor` directory to HEAD and
rebuilding reproduces both, so they belong to the machine's window state rather
than to any of this - the focus one is the key-window case that has bitten
before. Isolated by reverting rather than by argument.

## The read-only rule, and a no-op write that was not one

The previous entry left a question: a style delegating to a built-in should be
shown but not editable, the rule is right there in `showCategory()`, and the
group read as enabled anyway. Settled by instrumenting rather than reasoning,
and it is a bug in `Utils::AspectContainer` rather than in any page.

The trail, three diagnostics deep:

1. In `CodeStyleAspect`'s constructor the page copy delegates to `qt` and
   reports `currentReadOnly true` at **every** stage - after `syncFromReal()`,
   after the selector, after the settings are built. So the setup was never
   wrong.
2. Inside `showCategory()` the cast succeeds, `isReadOnly()` is true, and
   `group.isEnabled()` immediately afterwards is **false**. The rule runs and
   does the right thing.
3. By the time the page hands the group out, it is enabled again.

What happens in between is `registerAspect()`, and this line in
`insertAspect()`:

    aspect->setEnabled(aspect->isEnabled() && isEnabled());

Read as a value it is a no-op: an enabled aspect going into an enabled
container is set enabled. But `setEnabled()` is virtual, and
`AspectContainer::setEnabled()` **pushes its argument onto every child**
unconditionally - including when its own flag did not change. So re-asserting
"enabled" on a container flattens whatever per-child state that container had
set for itself moments earlier.

    if (!isEnabled())
        aspect->setEnabled(false);

is the whole fix: the only case where the old line could change anything was a
disabled container, and now that is the only case it acts on.

**Two tests, both controlled.** The behavioural one is on the C++ Code Style
page, where the bug was visible: a style delegating to a built-in is shown and
not editable. The unit one is in `tst_aspects`, in both directions - a disabled
child survives registration, and a disabled container disables what goes into
it. The second direction had no test at all before; the old line was its only
implementation and nothing checked it.

**Worth generalising.** A setter whose argument is computed to be a no-op is
only a no-op if the setter has no side effects, and a virtual setter that fans
out to children is exactly the case where it has one. The line had been read as
"normalise the child's state on the way in" for as long as it existed.

The page migration itself is finished; this is the first thing found by
*asking* the finished pages a question they had never been asked.

## How far the enabled bug reached

The fix landed with one test, on the page where it was found. The obvious next
question - which other pages were quietly losing per-child state - has an
answer worth writing down, because the shape that is vulnerable is narrow.

**Two axes, one of them safe.** `insertAspect()` writes three things into a
child: the container, auto-apply, and enabled. There is no
`AspectContainer::setVisible()` override, so visibility never fanned out and
the `setVisible()` half of every `showCategory()`-style rule was always fine.
Auto-apply *does* fan out, but there the propagation is the intent - a
container's children follow its mode - and it is a documented trap rather than
a silent one.

**The vulnerable shape is narrow: a container that sets per-child state in its
own constructor and is registered into a parent afterwards.** Most pages are
safe by accident of ordering - `EditorProjectPanel`, for one, calls
`updateForUseGlobal()` *after* all its `registerAspect()` calls, so its state
was applied to already-registered children. The pages that were not safe are
the ones reached through `createSettingsAspects()`, because that builds the
container - constructor and all - and hands it back for the page to register.

Of the four factories that supply one, two set per-child enabled state:
CppEditor's `showCategory()` and QmlJSTools' `updateState()`. Nim's registers
none, and ClangFormat's carries editability as an aspect *value* rather than as
enabled state, so neither was affected.

**So the Qt Quick Code Style page had the same fault as the C++ one**, and
nothing said so: the existing QmlJSTools test builds the settings container
through `createSettingsAspects()` and never registers it into a page, which is
exactly the step where the state was lost. A test that goes through
`CodeStyleAspect` now covers it, and both controls bite - reverting the Utils
fix fails it, and breaking the page's own `editable` rule fails it too, so it
is about the page and not only about the fix.

**Which group is on show is not something to assume.** The first version of
that test asserted `BuiltinSettings` was visible; the global Qt Quick style
does not use the built-in formatter, so it failed on a true premise wrongly
stated. The test now looks the visible group up and asserts on whichever it
finds, with a guard that one was found at all.

**And the control harness reported three clean controls that were crashes.**
Every run came back SIGABRT with no test output - the `QCocoaCursor::
createCursorData -> QImage::toCGImage` SEGV that predates all of this and fires
roughly one run in three. The harness read "no FAIL lines" as "nothing bit".
It now retries up to six times for a run that actually starts, and reports
"DID NOT RUN" rather than a pass if none does.

## The guard that keeps it done, and one thing it caught

The census already had the guard that matters - *no aspect-driven page renders
as a widget* - but it carried an exemption:

    if (ClangFormat is running)
        unported.removeOne("Code Style [A.Cpp.Code Style]");

written when ClangFormat replaced the C++ factory with a widget editor and
named no form. It names one now, so the exemption is stale and the page it
produces has to answer for itself. Removed, and the census reports **0 pages
still on widgets** in both builds - including the one with ClangFormat actually
loaded, which is the case the exemption existed for.

Controlled by taking `setQmlSource()` off the ClangFormat factory: the census
fails. Interestingly it fails on `undrawn` rather than on `declined`, and that
is the default page earning its keep - a language that loses its form now falls
back to `CodeStyleDefaultPage.qml` rather than to a widget, so what the census
notices is the language's settings going missing rather than a widget
appearing. Either way it is caught, which it would not have been before.

**And running the census with ClangFormat loaded surfaced a defect the port
introduced.** Four soft asserts per run:

    QTC_ASSERT(!m_fileName->isEmpty(), return {});   clangformatbaseindenter.cpp:638

The widget preview used to do `m_indenter->setFileName(fileName)` with a
`snippet.cpp` in the user resource directory. `CodeIndenting` - which is what
builds the indenter for a Qt Quick preview - never did, so ClangFormat's
indenter refused to run and **the C++ preview never re-indented at all**. It
did not fail, it silently did nothing, which is why nothing caught it: the one
test in this area drives a preview *formatter*, and the test languages all use
`PlainTextIndenter`, which needs no file name.

`CodeIndenting` now derives one from the source's mime type, and the test is a
recording indenter that captures what it was handed. Both halves bite: no file
name at all, and a name without the language's suffix - the suffix matters,
because that is how the tool decides which settings apply.

Worth noting how this was found: not by reading the code, but by running the
census in the build that has the plugin, and reading the *warnings* rather than
the totals. The suite was green both before and after.

## Reading a green suite's warnings, deliberately

Last entry's indenter bug was found by reading the *warnings* of a passing run
rather than its totals. That is worth doing on purpose rather than by accident,
so: both suites, green, warnings collected and triaged.

`-test QuickUi` is quiet - two soft asserts, one of them a test fixture that
deliberately leaves an `AspectList` without its data callback. `-test
TextEditor` is not:

    CodeEditor.qml:159: QML QQuickRectangle: The current style does not
    support customization of this control (property: "background")

`CodeEditor.qml` wrapped its contents in a `Frame` and gave the Frame a
`background: Rectangle` with the scheme's colour, a radius and a border. A
`Frame` is a `Control`, and a **native style declines Control customisations** -
so on macOS the colour and the border were dropped, silently, and the component
drew on whatever the platform style painted. The component has no production
user yet; it is offered for pages to use, and it would have been wrong for the
first one that did.

A `Rectangle` instead of a `Frame`. There is nothing a Frame was providing here
that is missed: its padding was already bypassed, because the contents anchor
to the Frame rather than to its content item.

**The test asserts the colour reaches the item that draws it**, compared
against `CodeHighlighting::backgroundColor()` - the same source the QML binds
from - rather than a literal, so it follows the scheme instead of pinning it.
Both controls bite: putting the `Frame` back fails it, and so does a colour
that is not the scheme's. Note which one matters - the *Frame* control is the
actual regression, and an assertion that only checked "some valid colour" would
have passed it, because a Frame's own background is a perfectly valid colour.

**And one warning is left that is not cosmetic.** Thirty-three occurrences of

    SOFT ASSERT: "it != m_watchClients.end()"   devicefileaccess.cpp:729
    Failed to remove watcher for <file>, it was not found.

one per Quick editor test that opens a file. Something removes a file watcher
that was never added, or removes it twice. It is not this migration's doing -
it fires for every editor test - but it is the same shape as the indenter bug:
a green suite saying, every single run, that something is wrong. Recorded here
rather than chased, because it belongs to the file-watching code and not to the
settings pages.

## The watcher imbalance: diagnosed, not fixed

The 33-per-run soft assert from the previous entry, chased far enough to be
worth someone's time and then deliberately left alone.

    SOFT ASSERT: "it != m_watchClients.end()"   devicefileaccess.cpp:729

**What it is.** `DesktopDeviceFileAccess` keeps two stores: a
`QFileSystemWatcher` holding paths, and `m_watchClients` mapping a path to the
watchers interested in it. `_watch()` only calls `addPaths()` for paths
`m_watchClients` does not already know, and treats anything `addPaths()` reports
back as a failure to watch.

Instrumenting the failure says exactly what is happening:

    watch failed: "revert.txt"   alreadyInWatcher true  watched 2  deleted 0  clients 1
    watch failed: "edited.txt"   alreadyInWatcher true  watched 8  deleted 6  clients 1

Two facts, both load-bearing. **Every** failing add is for a path the
`QFileSystemWatcher` *already holds* - Qt reports an already-watched path as a
failure to add, which is not a failure to watch. And the watcher is holding
paths for files that no longer exist - six of the eight in that line - while
`m_watchClients` knows about one. The two stores have drifted apart.

So the chain is: a path stays in the `QFileSystemWatcher` after
`m_watchClients` has forgotten it → the next client for that path is reported
as a failed add and never recorded → removing that client finds no entry and
soft-asserts.

**The obvious fix makes it worse, which is why this is a report and not a
patch.** Registering the client when the path is already watched removes every
"Failed to watch" warning - 20 to 0 - and takes the soft asserts from 33 to
**53**. Measured against two runs of the unmodified binary that both gave
exactly 33 and 20, so the comparison is real and not run-to-run noise. Trading
one symptom for more of another means the model is incomplete: the entry is
disappearing between the add and the remove, and that second defect has to be
understood before the first is touched.

Reverted. This is shared file-watching code, not settings-page code, and a
half-understood change to it is worth less than the diagnosis. What is now
known and was not before: it is not a watcher limit, it is not deleted files
being unwatchable, and it is not the settings migration - the two stores drift
apart, and the assert is the second-order effect.

### The missing piece: a relative path is being watched

Instrumenting the *removal* side, which the previous pass had not done, gives
the fact the rest turns on:

    remove: "/var/folders/.../qtc-revert-test.nWlwlH/revert.txt"  clientsLeft 0
      erase+removePath -> true   stillListed false  exists true
    remove: "revert.txt"                                          clientsLeft 0
      erase+removePath -> false  stillListed true   exists true

The second is a **bare relative file name**. `m_watchClients` is a
`QHash<FilePath, ...>`, so `revert.txt` and `/var/.../revert.txt` are two
different keys for one file - which is how the two stores drift apart, and why
"already watched" and "no entry to remove" happen for what looks like the same
path.

Where a relative path comes from is visible in the same area:
`EditorManager::openEditorWithContents(id, &title, contents)` gives an
in-memory document a *suggested name* - `"find.txt"`, `"revert.txt"` - as its
file path, and `DocumentManager`'s `FileWatchers::addPaths()` watches whatever
a document reports. `path.exists()` on a bare name resolves against the process
working directory, so the failure is not even reported:

    // Too much noise if we complain about non-existing files here.
    if (path.exists())
        qWarning() << res.error();

Watching a name that is not a location cannot work, and asking for it is the
first wrong step - but *where* to refuse it is a `Core`/`Utils` decision with
callers this migration does not own, and the one change tried here made the
symptom worse in a measured way. So the report stops at a mechanism that is now
complete: relative paths enter the watcher, the two stores key them
differently, and the soft assert is what surfaces at the end of that.

Whether production hits it depends on scratch documents, which take the same
`openEditorWithContents` path that the tests do - so this is likely not
test-only, but that has not been demonstrated here and should not be assumed.

## What making the Quick editor the default actually reaches

Switching the default raised the obvious worry: forty-odd places in the tree
do `qobject_cast<BaseTextEditor *>` on the current or a freshly opened editor,
and a Quick editor makes every one of them null. Counting those casts makes
the change look far-reaching. Counting what a *file* opens in does not.

The factory claims one mime type:

    addMimeType(QLatin1String(Constants::C_TEXTEDITOR_MIMETYPE_TEXT));

which is the list the plain text editor already had, minus `text/css`. So the
claim set is a strict subset of the factory it displaced: nothing that used to
open in a language editor can have moved. C++ still resolves to CppEditor, and
not because of registration order - the lookup walks a mime type's parents, so
C++ matches `text/plain` too, and CppEditor wins by being the *more specific*
match. The blast radius is exactly "files that used to open in the plain text
editor", which is what the switch was asking for.

That reduces the cast sites to three kinds:

- **Unaffected**: the language-editor ones (the CppEditor quick fixes, the C++
  outline, the .pro and CMake paths), and `debuggerplugin.cpp`'s scratch
  buffers, which pass `K_DEFAULT_TEXT_EDITOR_ID` explicitly and so never see a
  Quick editor at all.
- **Degrading by design**: the inline diff. Its guard already reads *"custom
  text based editors and too large documents get the classic diff view"*, so a
  plain-text file gets the side-by-side diff instead of ghost rows. Not a
  crash and not silent breakage - a fallback the code was written to take.
- **Genuinely broken**: two sites in `vcsbase` that jump to a line after
  opening a file. Both discarded the jump for anything that was not a
  `BaseTextEditor`, so the file opened and the cursor stayed put.

The last two needed no new API. `gotoLine` is already virtual on `IEditor`,
and the Quick editor overrides it - the cast was narrowing to a subclass to
call a method the base class declares for exactly this purpose. Deleting the
cast fixes both, and every one of the eight callers of `gotoLineOfEditor`
discards its `bool`, so widening what it accepts changes nothing observable.

The lesson worth keeping is that the count of `qobject_cast<BaseTextEditor *>`
sites was a bad estimate of the damage, off by more than an order of
magnitude, because it measured code that *could* be reached rather than code a
file can actually reach. The mime claim was the thing to read first.

### Two gaps left open, deliberately

Two sites are reachable with a plain-text file and are not fixed here, because
neither has a base-class virtual to fall back on the way `gotoLine` did:

- `HighlighterHelper::reload()` re-configures open editors after generic
  highlight definitions are downloaded or reloaded. Its useful half is
  `m_document->resetSyntaxHighlighter(...)`, a document operation, but it is
  reached through a private `TextEditorWidgetPrivate` method that also sets
  widget state (`setCodeFoldingSupported`, the syntax info bar). A Quick
  editor's open text file will not pick up a newly downloaded definition until
  it is reopened.
- `AcpChatController` attaches the current editor's cursor position and
  selection to the chat context. `textCursor()` is a `BaseTextEditor` method
  with no `IEditor` equivalent, so for a plain-text file the *file contents*
  resource is still attached but the editor-state one is not.

Both are small, both have obvious workarounds, and both want an API decision
(what the `IEditor` cursor interface should be) rather than a cast removed.
Recorded rather than patched around.

## Where a row is, as distinct from how tall its text is

The inline diff is the one feature the Quick editor falls back out of, so it
is the next thing to build. Reading how the widget does it corrected the
assumption I started with.

Ghost rows are not rows. They are `Utils::LayoutItem`s attached to a block in
`Utils::TextEditorLayout` - `TextLayoutItem` for a removed line rendered as
text, `EmptyLayoutItem` for a spacer that only takes up room - and the row
numbering does not count them:

    int effectiveLineCount(int index)
    {
        const LayoutData &data = layoutData(index);
        return data.editorHidden ? 0 : data.lineCount;
    }

`data.lineCount` is the block's own wrapped line count. So an item contributes
no line number, and `firstLineNumberOf()` is unchanged by one being there.
What it contributes is height, through a separate call:

    int additionalBlockHeight(const QTextBlock &block, bool includeEmbeddedWidgetsHeight) const

That is the shape of the problem. Rows stay uniformly tall and keep their
numbering; some *blocks* claim extra space between them. Which means the thing
in the way is not the row model but the arithmetic `row * m_lineHeight`,
written out in a dozen places across `textviewport.cpp`.

This batch replaces that arithmetic with three functions - `yOfRow(row)`,
`rowAtY(y)` and `rowSpan(row)` - which for now return exactly what the
arithmetic did. No behaviour changes; the point is to have one place to teach
about `additionalBlockHeight` instead of a dozen.

Worth saying which `m_lineHeight` uses are *not* row positions, because they
look identical and must not be converted: the caret rect, the selection fill,
the newline tail, the wrap-width margin and the page-up/down step are all
sized by the text, not by the row's claim on the page. Converting those would
make a caret grow to fill a ghost gap.

A refactor has no new behaviour to test, so the control was to make `yOfRow`
lie - `row * m_lineHeight + 7` for every row past the first - and check the
suite noticed. It did, in the right places: 307 passed and 10 failed, across
what is drawn on screen, screen and document positions agreeing, where the
current line is highlighted, wrapping, caret movement between rows and every
drag and drop test. Restored, the suite is 317 passed and 0 failed.

What is left for the next batch is the arithmetic itself: `yOfRow` has to add
up `additionalBlockHeight()` for the blocks above a row, which wants a prefix
sum built once a layout rather than a walk per call, and `rowAtY` has to
invert it. The gutter needs nothing - it reads the same `visibleRows` model
the viewport builds, so the rows carry their own positions with them.

### Teaching the one place

With the arithmetic behind `yOfRow()`, the gap model itself is small. A gap is
space above a row, claimed by something that is not one of the document's
rows, and the invariant that makes it cheap is that rows keep both their
numbering and their height. Nothing about wrapping, folding, hit testing or
the caret has to learn a second notion of "row"; only where a row sits moves.

    struct Gap
    {
        int row = 0;
        qreal height = 0;
    };

Held sorted, with a running total beside it, so `gapAbove(row)` is an
`upper_bound` rather than a walk - the count of gaps at or above a row indexes
straight into the sums. `rowAtY()` inverts it with the same search over gap
*starts*, and then has one case worth naming: a y inside a gap belongs to no
row at all, so it answers with the row underneath rather than the one the
division lands on part way through. That is what makes a press in the removed
lines of a diff put the caret on the line that replaced them.

`rowSpan()` deliberately did not change. A gap is not part of the row above
it, so a caret there is still text-sized and scrolling to that row need not
drag the gap onto the screen with it.

Three tests: that a gap moves the rows under it and leaves the ones above
alone, that a click inside one lands on the row under it, and that the screen
to document round trip survives two of them - two, so that the running total
is exercised rather than a single subtraction that would look right either
way.

The controls were run separately, because "does the feature work" and "does
the test test it" are different questions:

- `gapAbove()` returning 0 - the whole model off.
- `rowAtY()` ignoring gaps, with `yOfRow()` left correct - only the inverse
  broken. Placement still passed and both mapping tests failed, which is the
  result that matters: the round trip tests are not coasting on the forward
  direction being right.

### Rows that are not in the file

Gaps gave the height half. The other half is that something has to be drawn in
them, and the shape that falls out is a second kind of row:

    struct GhostRows
    {
        int row = 0;
        QStringList lines;
        QColor background;
    };

They are not in the document, so they have no position in it - a ghost row's
`blockPosition` is -1, and nothing maps a screen coordinate onto one. They are
laid out in `updatePolish()` and drawn in `updatePaintNode()` exactly like the
document's own rows, before them, so a real row drawn over the same pixels
wins.

The one thing that could not stay as it was: a gap's height was being set,
and a ghost row's is not given but follows from how many rows there are times
the line height. So the gaps are no longer set at all - they are *derived*,
from the spacers someone asked for and the ghost rows they installed, and
rebuilt in `updatePolish()` once there is a line height to multiply by. Two
things opening a gap above the same row open one gap, or the running total
counts a row twice.

Ghost rows off screen are not laid out, for the same reason the document's own
rows are not: a diff of a long file has more removed lines than fit, and
laying out the ones nobody can see would make scrolling cost more the bigger
the diff got.

Two things worth recording about the tests rather than the code.

The culling test failed first time, and the code was right: a ghost at row 350
of a 400 line file is still off screen when you scroll to the bottom, because
the bottom shows the last thirty-odd rows and not the last fifty. The test's
premise was wrong, not the culling.

And the first three tests were weaker than they looked. They asserted that the
ghost text was laid out, that the gap opened to the right size, and that the
document's positions were untouched - and every one of them would have passed
with the ghost rows drawn at the top of the viewport, because none of them
asked *where* the text went. That is the assertion that cannot fail. They now
check that the rows run up from the row they sit above, one line apart, and
the control for it is to draw them below that row instead.

### Reading a diff without a widget to decorate

The computation was already done and already widget-free:

    DIFFEDITOR_EXPORT InlineDiffRenderModel mapChunkToRenderModel(
        const ChunkData &chunk, bool baselineEndsWithNewline, bool editorEndsWithNewline);

It produces `ghosts`, `changes` and `hunks` from a chunk, and its
`GhostBlock` type lives in **texteditor**, not in the diff editor. So nothing
about turning a diff into removed lines needed porting. What was missing was
only a sink: something to hand those ghosts to a viewport instead of to a
widget. `applyInlineDiffGhosts()` is that, and it sits beside the decorator
because it reads the same description.

Reading it is where the work is, and it is all conventions:

- `anchorLine` counts from one, rows do not.
- `anchorLine == lineCount + 1` is not a line at all - it means below the last
  one, which for a gap is the row one past the end.
- Anything beyond that describes a document this no longer is, which happens
  while the diff of the current contents is still being computed. The widget
  drops those and so does this.
- A removal of more than a hundred lines is elided to a hundred and a line
  saying how many are not shown, or deleting a thousand lines would put a
  thousand rows between two lines of the file.

Ghost rows also stopped taking a colour from whoever installs them. They are
drawn in `C_DIFF_SOURCE_LINE`, the scheme entry the decorated widget uses for
the same lines, so the two views of one diff are coloured alike and follow the
theme together without a caller having to know that.

What is still missing before this is a feature rather than a mechanism: the
`changes` half - the full width highlight on lines a diff has added or
modified - has no equivalent in the viewport yet, and character level
highlights within a ghost line are carried in the description but not drawn.

### The other half of a diff

Removed lines needed a new kind of row. Changed lines need nothing of the
sort: they are the document's own rows, with a background under them and the
characters that differ marked over the top - the same two scheme entries the
widget uses, `C_DIFF_DEST_LINE` and `C_DIFF_DEST_CHAR`.

One thing about where the marks go in. A row's formats are assembled by
clipping the block's formats to the row's slice, and then the row is compared
against last layout's rows to see whether one can be reused:

    if (it->layout->text() == rowText && it->layout->formats() == formats) {

The diff marks have to be appended to `formats` *before* that comparison. Put
them in afterwards and a row whose diff changed still matches last time's, so
the old marks stay on it - the row would be reused precisely because the thing
that changed was not being compared. The comparison is the reuse rule and
anything that affects what a row looks like has to be inside it.

The applier grew the second half at the same time and became
`applyInlineDiff(viewport, ghosts, changes)`, since a caller has both and
wants one call. A `ChangedRange` is a run of lines; the viewport takes them
one at a time with whichever characters of each differ, because that is what
it looks a row up by.

Controls, run together because they fail different tests at different
assertions: filling only the line a range starts on, and appending no
character marks. The first fails the range test with `{2}` where `{2, 3, 4}`
was expected, the second fails the fill test on its marks while its fill
assertion still passes.

### The arithmetic QML was doing behind the viewport's back

Adding gaps broke something that had been right for as long as it existed. Nine
bindings across `CodeViewport.qml` and `EditorGutter.qml` placed themselves
like this:

    y: row * viewport.lineHeight - viewport.scrollY

Which is exactly the formula the C++ side had just stopped using. Every one of
them - line numbers, fold markers, change marks, indent guides, whitespace
dots, wrapped-line markers, annotations - would sit a gap's worth too high the
moment anything claimed space between rows. Nothing installs a gap in
production yet, so it was latent rather than live, but it was wrong in code
that was already committed.

The fix is for the row to carry its own position instead of QML deriving one.
The model now has a `y`, in document space - *before* the scroll is taken off
it, which matters: QML has to keep subtracting the live `scrollY`, or a row
would hold the scroll position it had when it was last laid out and lag a
frame behind every scroll.

The first attempt wrote `modelData.y ?? row * viewport.lineHeight`. That
fallback is dead - `y` is always supplied - and what it falls back to is
precisely the bug being fixed, so it would have hidden a regression rather
than caught one. Removed.

The test for this went through two wrong premises before it bit. It first
looked for the gutter's line numbers, which the fixture does not have:
`CodeViewportFixture` loads `CodeViewport.qml` alone, so the gutter is not in
the scene at all and "line 3 is not drawn" was the truth about the fixture,
not about the code. Rewritten against the indent guides, which that file does
draw, it passes and its control - putting `row * lineHeight` back in the
guide's binding - fails it.

### A message on a line of its own

`AnnotationAlignment::BetweenLines` was in the switch and did nothing:

    case AnnotationAlignment::NextToContent:
    case AnnotationAlignment::BetweenLines:
        break;

Choosing it in the settings gave you `NextToContent` - the message after the
text, exactly where it goes when you have not asked for it to be anywhere
else. The comment above the switch already said why: it "needs the block to be
taller than the text in it", which nothing could do until rows could have
something between them.

So this is the third thing that opens a gap, and unlike the other two it is
not installed from outside - the viewport works it out from what it is already
drawing. The cost is worth naming: the gaps have to be known before the rows
are placed, and which lines carry a message is not something a visible-row
walk can answer, because a message above the screen still moves everything
under it. `TextDocument::marks()` enumerates them, and there are as many marks
as there are diagnostics rather than as there are lines, so this is paid per
mark and not per document.

The message then needs its own y: the row is where it belongs to, not where it
is drawn. The row model carries `annotationY` beside `annotationX`, and for
this alignment it is the row's y plus a line. Its x changes too - lined up
with the text rather than trailing it, which is the point of giving it a line
of its own.

The control is the one that separates the two halves: leave the gap open but
draw the message at the row's y. The setup assertions - the document is a row
taller, the line below moved down - still pass, and the placement one fails.
Had the control been "turn the whole thing off", all three would have failed
together and the placement assertion would not have been shown to do anything.

## Counting what is left, rather than saying it is done

The migration this document has been tracking is finished, and the way to see
that is to count layouters rather than to count ported pages. Three mentions
of `setLayouter` remain in the tree:

- `aspectwidgets.cpp`, which defines it.
- `quickui_test.cpp`, which builds a widget page on purpose. Its comment says
  why: *"The widget path says the same thing, so a page that has not been
  ported behaves the same way."* That is a test **of** the fallback, and it
  has to keep working for as long as the fallback exists.
- `lua/bindings/settings.cpp`, where a Lua script passes its own `layouter`
  function. That is a public API for other people's scripts, not a Creator
  page; porting it would mean changing what those scripts are allowed to say.

So no Qt Creator settings page builds itself from a Layouting closure any
more.

A page can also be a widget without a layouter, through
`IOptionsPage::setWidgetCreator`, so that route is worth checking too. Twelve
uses, and all but one are `IMode::setWidgetCreator` - whole modes (Edit,
Debug, Profiler, the extension manager), which were never in scope. The
remaining one is Qt Designer's:

    vbox->addWidget(m_designerPage->createPage(nullptr));

`QDesignerOptionsPageInterface` is Qt Designer's own interface and
`createPage()` hands back a widget Qt Designer built. Creator does not own
that UI and there is nothing in it to express in QML - it is a widget from
another library, embedded. That one is not a gap in this migration; it is the
edge of what this migration could ever have covered.

### A gap that turned out not to want an API decision

This was recorded a few batches ago as one of two things that "want an API
decision rather than a cast removed", on the grounds that the useful half of
`configureGenericHighlighter` sat behind a private widget method that also set
widget state. Reading the widget state is what settles it:

    m_commentDefinition.singleLine = definition.singleLineCommentMarker();
    ...
    q->setCodeFoldingSupported(true);

Which comment markers the shortcuts insert, and whether folding is offered.
Neither is a property of the document, and neither has anything a view without
a widget would do with it. What is left over is two calls - put a highlighter
carrying the definition on the document, and give it the font settings - and
those are the whole of what a reload has to do.

So the seam was already there and only needed naming. `setDefinitionOn(document,
definition)` is the document half, the widget's private method now calls it
instead of repeating it, and `reload()` handles an editor that is not a widget
by applying the definitions itself. The reason to have skipped such an editor
would be that it has no info bar and no comment markers to update; neither is a
reason to leave its text coloured by a definition that has just been replaced.

The control is the branch removed again, and its message is the bug as a user
would meet it: after downloading definitions, a document that should be back to
`JSONC` is still `Bash`.

Worth keeping: "this needs an API decision" was a conclusion about a call
stack, reached without reading what the private method actually did. The half
that looked entangled was four lines of widget state.

## A regression found by widening the net, not by reasoning about it

Everything so far had been verified against TextEditor, QuickUi and DiffEditor.
Running the suites of the plugins that merely *depend* on TextEditor found two
red tests in Git - `testInlineDiffFile` and `testInlineDiffConflictedFile` -
expecting an inline diff called "file.txt (Unstaged)" and getting a classic one
called `Git Diff "file.txt"`.

This is production code, not a test fixture:

    auto textEditor
        = qobject_cast<TextEditor::BaseTextEditor *>(EditorManager::openEditor(filePath));
    if (!textEditor || !textEditor->editorWidget())
        return nullptr;

`openInlineDiff()` opens the file *itself* to get at a widget. Once text files
opened in the Quick editor that cast returned null, the function returned
nullptr, and the caller fell back to `diffFile()`. Asking for Inline Diff on a
plain text file gave the side by side view instead. Earlier this document said
the inline diff "degrades by design", which was true of the diff editor's own
guard and not of this: here the fallback exists for deleted files and was being
taken for every text file.

The widget was only ever a way to reach `textDocumentPtr()`. What the inline
diff needs is the document, and the obstacle was that the two editors held it
differently - `QSharedPointer<TextDocument>` in the widget, which is what
`TextDocumentPtr` and `openInlineDiffEditor()` are written in, and
`std::shared_ptr<TextDocument>` in the Quick editor. Three lines to align, and
then `TextDocument` can hand out its own handle through `QEnableSharedFromThis`,
so `textDocumentPtr(IEditor *)` answers for any view without knowing what kind
it is.

The lesson is about the earlier blast radius analysis rather than about this
call. Reading the mime claim bounded which *files* moved correctly, and the
cast survey classified the sites that a moved file could reach - but that survey
was of `src/plugins` for `qobject_cast<BaseTextEditor *>` and this site is one
of the ones it listed as "needs editorWidget - blocked on porting". It was
filed as blocked when it was in fact broken. What separated the two was running
the tests of a plugin I had no reason to think I had touched.

## The survey that could not have found them

The Git regression was in a site my earlier cast survey had listed as "needs
editorWidget - blocked on porting". Filed as blocked; actually broken. Looking
at why, the survey itself was worse than that:

    grep -rn "qobject_cast<BaseTextEditor" src/plugins --include=*.cpp

Forty hits, duly classified. But the Git site reads
`qobject_cast<TextEditor::BaseTextEditor *>`, and that string does not contain
`qobject_cast<BaseTextEditor`. **Every namespace-qualified use was invisible to
the search**, and there are twenty-five of them - a survey that missed 38% of
its subject while reporting a confident three-way classification of the rest.

Of the twenty-five, most are language editors a plain text file cannot reach.
Two were not:

- `macros/texteditormacrohandler.cpp` records keystrokes by installing an
  event filter on the current editor's widget. The cast is null-checked, so
  recording a macro in a plain text file now quietly records nothing.
- `lua/luaplugin.cpp` adds a Run button to the tool bar of a script:

      auto textEditor = qobject_cast<TextEditor::BaseTextEditor *>(editor);
      TextEditor::TextEditorWidget *editorWidget = textEditor->editorWidget();

  Unchecked, because when it was written every text file opened in a widget.
  Lua registers no editor of its own, so a script is a plain text file, and
  opening one from the scripts directory is a null dereference.

That last one is the shape to remember. The other twenty-four sites are all
null-checked; this one was not, and the reason it was not is that the
assumption held when it was written. A default that changes does not break the
code that tested the assumption - it breaks the code that had no reason to.

The reachability is now asserted rather than reasoned: a test says the default
editor for a lua script is the Quick editor. If someone gives Lua an editor of
its own the test fails, which is the right moment to ask whether the guard
still has a reason.

### The macro handler wanted nothing a widget had

The second of the two sites the corrected survey turned up. Recording a macro
in a plain text file recorded nothing, because the handler kept

    TextEditor::BaseTextEditor *m_currentEditor = nullptr;

and every use of it in the file is `m_currentEditor->widget()` - watching
keystrokes, and sending them back on replay. `widget()` is on `Core::IEditor`.
The narrower type contributed nothing at all; it only decided which editors the
handler would agree to attach to.

It now attaches to an editor whose *document* is a `TextDocument`, which is
what "a text editor" was trying to say. That keeps macros away from views
showing something other than text, which is presumably why the cast was there,
without tying the handler to one implementation of showing it.

The test asserts the thing that changed: `executeEvent()` returns false when
the handler has adopted nothing, so replaying into a Quick editor is exactly
the difference between the two versions. It also asserts that the editor it
opened is *not* a `BaseTextEditor`, so it cannot pass by opening the widget
editor and proving nothing.

What it does not assert is that the keystroke arrives in the document - that
needs focus, and focus-dependent assertions here have been the least reliable
thing in this whole effort.

### Three callers wanted the same missing thing

Sweeping for casts that are *dereferenced without a check* found one crash
(Lua) and, among the guarded ones, a quieter problem in cpaster:

    if (auto textEditor = qobject_cast<const BaseTextEditor *>(editor))
        data = textEditor->selectedText();
    if (data.isEmpty()) {
        if (auto textDocument = qobject_cast<const TextDocument *>(document))
            data = textDocument->plainText();

Perfectly guarded, and wrong in a way a guard cannot help with: for a view
that is not a widget the selection is unreachable, `data` stays empty, and the
fallback pastes **the whole file** to a paste service. Not a crash - the wrong
end of a fallback that was written for a different reason.

That is the third caller for the same missing thing. The ACP chat controller
wants the cursor and the selection to describe the editor's state; a
multicursor implementation would have to define it; cpaster wants it here. It
had been recorded twice in this document as "wants an API decision", which was
true only until the third instance made the answer obvious: `IEditor` already
has `currentLine()`, `currentColumn()` and `gotoLine()`, so a `selectedText()`
beside them is the existing design and not a new direction. Both editors
already implement it; it was simply not on the interface.

**Adding it cost a full rebuild, and skipping that produced a crash that had
nothing to do with the change.** A new virtual on `IEditor` moves the vtable of
every editor in the tree. Building only TextEditor, Core and CodePaster left
every other plugin's editor compiled against the old layout, and the suite died
with

    SEGV on unknown address 0x000000000000 (pc 0x000000000000)
    #1 TextEditor::TextEditorFactory::setEditorCreator(...)::$_0::operator()()

a call through a slot that was no longer where the caller thought. 968 targets
rebuild for one virtual; the partial build is not a shortcut, it is a
different program.

Worth a further note: with everything rebuilt, TextEditor ran 332 passed 0
failed and QuickUi 89 passed 0 failed - both of the tests this document has
been calling "known focus flakes" passed. One run is not evidence that they
were never flaky, but it is a reason to stop assuming a partial build is
equivalent to a whole one.

## The flake was mine, and it was not flaky for the reason I said

Two tests have been failing about one run in ten for weeks of this effort, and
this document has been calling them "known focus flakes" and moving on. Both
are mine. Counting the session's logs put a number on them:

    testIndentGuidesFollowTheIndentation   340 pass  36 fail
    testMultiLineStringGetsATextArea       282 pass  29 fail

A first guess - that they were artefacts of partial builds - is wrong, and the
same counting says so: 10% is far too high, and they had failed after full
builds too. What they are is a test identifying an item by an accidental
property:

    if (candidate->width() == 1 && candidate->isVisible()
        && qFuzzyCompare(candidate->mapToItem(viewport, ...).y() + 1, wanted + 1))

An indent guide is a one pixel wide Rectangle. So is the **caret**, and it sits
on whichever row the reader left it on - row 0 in this test. Instrumenting said
so exactly:

    PROBE focus= false windowActive= false
    PROBE caret visible= false w= 1 y= 0

Width 1, y 0: the same shape in the same place as the thing being counted. The
caret's `visible` follows `activeFocus` (its blink is opacity, not visibility),
so the count is right whenever the window has no focus and one too many when it
does. That is the one-in-ten.

The fix is for the items to say what they are - `objectName: "indentGuide"` -
and for both tests to ask for that instead of for a width. The second test was
one I had added earlier this session with the same predicate copied across, so
it carried the same latent flake without ever having failed.

Worth being precise about the evidence, because a control here is awkward:
`requestActivate()` does not reliably get focus in this environment, so the
obvious red/green - force focus, watch the old predicate miscount - passes
vacuously by skipping. Three clean runs afterwards are not proof either, at a
90% base rate. What actually establishes this is the probe: the caret is width
1 at y 0, and the old predicate selected on width 1 at y 0. The mechanism is
demonstrated by construction rather than by sampling, and the test now records
that collision so the next person does not have to rediscover it.

### And the second one asked for focus without a window

The other long-running flake, at 282 pass and 29 fail, is
`testMultiLineStringGetsATextArea`. What it checks is that a multi-line editor
writes its value back when it *loses* the focus rather than on every keystroke,
so it does this:

    QMetaObject::invokeMethod(area, "forceActiveFocus");
    QTRY_VERIFY(area->hasActiveFocus());

on a form it never showed. An unshown window hands the focus to nothing, so
whether this worked depended on what else was on screen - and about one time in
ten it did not.

Every other focus-using test in that file goes through `showPage()`, which
resizes, shows, waits for `qWaitForWindowExposed` and processes events before
touching focus; a scan of the file found this was the only one that did not.
Adding the same three lines is the fix.

Both fixes are the same mistake in different clothing: a test that describes
what the user sees, checked through something the test never actually put on
screen. The indent guides one counted an item by its width because the window
was usually not focused enough for the caret to appear; this one asked for
focus in a window that could not give it. Neither is a timing problem, and
neither would have been fixed by waiting longer - which is what "flaky" had
been quietly implying for weeks.

The evidence is worth stating plainly: four clean runs at a 9% failure rate is
about a 69% outcome by chance, so the runs are supporting and not conclusive.
What carries it is that the test now does what every working focus test in the
same file does.

## Multiple cursors: what it would actually take

Asked whether the Quick editor has them: it does not. The four mentions of
`MultiTextCursor` in `textviewport.cpp` are all `MultiTextCursor({cursor})` -
wrapping *one* cursor to call an API shaped for many, then taking
`.mainCursor()` straight back out. Nothing adds a second one.

A first estimate here counted the state uses - 18 of `m_cursorPosition`, 15
each of `m_selectionStart` and `m_selectionEnd`, 22 cursor references in the
QML - and concluded this was a bigger piece of work than the inline diff. That
was wrong, and reading rather than counting says why.

**The state is concentrated, not scattered.** By function:

    6  updatePolish        4  textCursor         2  ensureCursorVisible
    2  setSelectionStart   2  setSelectionEnd    2  setCursorPosition
    2  selectedCharacterCount  2  removeSelectedText  2  dropText
    1  each of a dozen accessors (cursorLine, cursorColumn, cursorBlock, ...)

The dozen one-line accessors all answer *about the caret*, and would keep
answering about the main one. `textCursor()` is the funnel: it builds a
`QTextCursor` from the position and the selection, and thirteen callers - all
the editing - go through it.

**And the hard part is already written.** `Utils::MultiTextCursor` has
`setCursors`, `addCursor`, `mainCursor`, `hasMultipleCursors`, `mergeCursors`
and an `insertText` that applies to every cursor. The subtleties that make
multiple cursors hard - applying edits so earlier ones do not shift later
positions, merging cursors that collide, one undo step for the lot - are in
there, used by the widget editor. The viewport would hold one instead of three
ints rather than inventing any of it.

What is genuinely new is the drawing, and it is small:

- `line.selectionFill` is one `QRectF` per row, set in one place and drawn in
  one place. It becomes a list.
- The caret is a single QML `Rectangle` bound to `cursorRectangle`. It becomes
  a `Repeater` over a list of caret rectangles.

Then the entry points: Alt+click to add one, Escape to collapse to one, and
the two actions the widget has (`ADD_CURSORS_TO_LINE_ENDS`, add-cursor-at-next
match).

So a plausible staging, each part committable on its own:

1. Hold a `MultiTextCursor`; every accessor answers for the main cursor.
   Nothing changes for anyone, and the suites should be untouched.
2. Draw them: selection fills and carets as lists.
3. Edit through all of them, which is mostly deleting the loop-avoidance the
   current code does with `MultiTextCursor({cursor}).mainCursor()`.
4. The ways a second cursor gets created.

The correction worth keeping is about the estimate rather than the feature:
counting identifier occurrences measured how much text mentions the cursor,
not how many decisions depend on it being single. Those are different numbers,
and here they differ by a lot.

### Why the whole suite has never been run here

Every verification in this document is per plugin - `-test TextEditor`,
`-test QuickUi` and so on. Trying `-test all` explains why:

    [ QmlDesigner ] Plugin error: Symbol not found:
      __ZN5Utils15AspectContainer11setLayouterE...
      Referenced from: libQmlDesigner.dylib
      Expected in:     libUtils.20.0.82.dylib
    Errors occurred while loading plugins, skipping test run.

Not a source problem, and not this branch: `AspectContainer::setLayouter` was
removed by this migration long ago - what exists now is the free
`AspectWidgets::setLayouter` - and QmlDesigner's sources do not mention
`setLayouter` at all. The Release configuration compiles all 388 of them
without complaint.

The dylib is simply old:

    Jul 16  libQmlDesigner.dylib
    Aug 28  libUtils.20.0.82.dylib

This Debug configuration does not build QmlDesigner, so nothing regenerates
that file and nothing removes it. It sits in the plugin directory failing to
load, and one plugin failing to load makes Creator skip the test run entirely
- which `-noload QmlDesigner` does not prevent, because the failure happens
while reading what is there rather than while starting it.

So `-test all` is unavailable in this build directory until the stale dylib is
deleted, which is a decision about someone else's build tree rather than
something to do quietly. Per plugin runs are unaffected, and between them they
cover Core, TextEditor, QuickUi, CppEditor, Git, DiffEditor, VcsBase, Macros,
ProjectExplorer, QmlJSEditor, Python and LanguageClient.

### Drawing more than one caret

The first step of multiple cursors, and the one that stands up on its own: the
viewport draws what its list of carets says rather than assuming there is one.

- `caretRectangles` is a list, one entry per caret, and the QML caret becomes a
  `Repeater` over it.
- `Line::selectionFill` - one `QRectF` per row - becomes `selectionFills`, and
  the row loop emits one per selected range reaching into that row.
- `multiTextCursor()` and `setMultiTextCursor()` sit beside the existing
  `textCursor()` pair. The main cursor is still the position and selection this
  has always kept, so every accessor - `cursorLine`, `cursorColumn`,
  `cursorBlock`, the lot - keeps answering exactly what it did.

Nothing creates a second caret yet, so nothing changes for a reader. What the
test does is install two cursors directly and check that two carets are drawn
and that a row with two selected runs reports two fills - the part a single
rectangle could not have expressed.

Two things went wrong, and both were caught rather than reasoned about.

The first was the renamed model key. Three existing tests read
`selectionFill` and went red immediately, which is the rename working as it
should: they were reading a key that no longer exists.

The second is subtler and was a real bug. `caretRectangles` was given
`NOTIFY cursorPositionChanged`, which sounds right and is not: where a caret is
*on screen* is only known once the rows are laid out, and that happens after
the position last changed. The existing property for this,
`cursorRectangle`, is notified by `cursorRectangleChanged` for exactly that
reason. With the wrong signal the carets kept a pre-layout, zero width
rectangle - and what caught it was `testIndentGuidesFollowTheIndentation`,
which now asserts the caret is one pixel wide at row 0. That assertion existed
only because of the flake fix in the previous batch; a week ago this would have
shipped.

Two hover tests failed in one run and passed in the two after it. They are the
physical pointer artefact recorded earlier, not the wrapping `Item` - which is
zero by zero, accepts nothing, and sits where the caret Rectangle used to.

### Typing at more than one caret

The second step: an edit happens at every caret rather than at the one.

    void applyToEveryCaret(const std::function<void(QTextCursor &)> &edit);

Two things it has to get right, and both are the reason `Utils::MultiTextCursor`
exists rather than something to reinvent:

- **Order.** The carets are taken later in the document first, because an edit
  moves everything after it. Going the other way, typing at the first caret
  would leave every caret after it pointing one character too early.
- **One undo step.** The whole run is wrapped in a single edit block. Typing
  once should not take two undos to take back merely because it happened in
  two places.

The carets are sorted *by index* rather than by value, so the main caret is
still the first one afterwards however the order came out - everything that
asks about "the caret" gets the same answer as before.

The other half is `setTextCursor()`, which now clears the extra carets. Putting
the caret somewhere - a click, a jump, a search result - means putting *the*
caret somewhere, and the extra ones are gone. Whoever wants to keep them says
so with `setMultiTextCursor()` afterwards. Without that, clicking somewhere
would leave invisible carets behind that the next keystroke would type into.

Three paths go through it so far: Return, Space and the default text case.
Backspace, Delete, Tab and Backtab still edit only the main caret; nothing
creates a second one yet, so no reader can meet the difference, but it is the
next thing.

The control - edit only the main caret - fails with `"alpha\nbetaX\n"` where
`"alphaX\nbetaX\n"` was wanted, which is the feature stated as a diff.

### Deleting, indenting, and where the carets go away

The rest of the editing keys. Backspace and Delete go through
`applyToEveryCaret()` like typing did. Tab and Backtab do not need it: the
indenter already takes a `MultiTextCursor` and knows how to apply one indent
to all of them, so what was

    cursor = doc->indent(Utils::MultiTextCursor({cursor})).mainCursor();

- wrapping one cursor to call an API shaped for many and unwrapping the answer
- becomes `setMultiTextCursor(doc->indent(multiTextCursor()))`. Four of those
round trips existed; this is what they were always waiting for.

Writing the test for it turned up something the previous step had got wrong.
`setTextCursor()` cleared the extra carets, but `setCursorPosition()` did not -
and they mean the same thing. So after an edit, setting the position left the
extra carets in place, invisible to whoever set it and waiting to receive the
next keystroke. The clearing now lives in `setCursorPosition()`, which is the
setter that says "the caret is here", and it happens **before** its early
return: putting the caret where it already is still says there is one of it.

That is the sort of mistake that only shows up when the second step is
written. The first step's test could not have caught it - it never edited and
then set a position - which is an argument for writing the next step rather
than for having tested the first one harder.

### Making a second caret

The part a reader can reach. Alt+click puts another caret where it lands, and
Escape goes back to one - the same two gestures the widget editor has, and
Alt+click is what it uses too (Alt without Control; Alt *with* Control already
means "follow this symbol into a split").

`addCaretAt()` hands the work to `Utils::MultiTextCursor`: `addCursor()` makes
the new one the main caret, which is right - the caret just placed is the one
being worked with - and `mergeCursors()` means clicking where a caret already
is does not put a second one on top of it.

The Alt+click test drives a real click rather than calling `addCaretAt()`,
because the branch that decides to call it is in QML and calling the invokable
directly would test the viewport while leaving its caller untested. That has
been a mistake here before.

Escape moved, and where it ended up is the interesting part. Put in the
switch, it never ran: `keyPressEvent()` returns early for a read-only viewport
long before reaching there, and the test pressed Escape while the fixture was
still read-only. The fix is not in the test. **Collapsing the carets is not an
edit** - a view that cannot be typed into can still have been given several
carets by Alt+click, and has to be able to be rid of them - so Escape is
handled before the read-only check. And only when there is more than one
caret: Escape means other things elsewhere, and taking it always would be
taking it from them.

### A caret at the end of every selected line

The widget editor's "Add Cursors to Line Ends", answered by the Quick editor
too. The action id is global and the context decides who handles it, which is
the same arrangement Follow Symbol and Text Wrapping already use here - so
this is a registration and a method rather than anything new.

The method is the widget's algorithm, which turned out to need nothing from a
widget: for every caret that has a selection, walk the blocks from where the
selection starts, and put a caret at the end of each one that ends before the
selection does. The line the selection *stops on* is left out, because
stopping part way through a line was not asking for that line's end.

Two things the test pins besides the obvious one: that a selection covering
two lines gives carets at 5 and 10 rather than merely "two carets somewhere",
and that calling it with nothing selected leaves the single caret alone rather
than clearing it - `setMultiTextCursor()` with an empty set would have, which
is why the method checks before calling it.

### A caret on the next occurrence, and a convention I had got backwards

The last of the widget's two actions: select a word, ask again, and the next
one of it gets a caret too. Ported from the widget, where it is also pure
cursor work - with one honest difference. The widget searches with the find
bar's current flags (`m_findFlags`); this view does not have them, so it looks
for the text as it stands. Case-insensitive multi-caret selection is therefore
the one thing here that the widget does and this does not.

Writing it found a bug in the API from three steps ago. `multiTextCursor()`
returned the main caret **first**. `Utils::MultiTextCursor` keeps it **last**:

    QTextCursor MultiTextCursor::mainCursor() const
    {
        ...
        return m_cursorList.back();
    }

So `cursors().first()` means "the oldest one", and the widget's algorithm uses
exactly that as the anchor it stops at when the search comes back around. With
the main caret first, the anchor was whichever caret had just been added - the
search wrapped straight onto it and stopped, so a third caret could never be
made. Two carets worked, three did not.

Both `multiTextCursor()` and `setMultiTextCursor()` now keep the main one last,
and `applyToEveryCaret()` follows the same order so there is one convention
rather than two.

Worth naming the shape: the first two steps were consistent *with themselves*,
so their tests passed. What they were not consistent with was the type they
hand out, and nothing found that until an algorithm written against the type
was reused. A borrowed algorithm is a test of the borrowed type's conventions.

## A flake my own fix made visible

`GitTest::testInlineDiffFile` failed 4 times in 10 runs, always on the same
assertion - staging a hunk and finding `+four` in the index. Not noise, and
not pre-existing in any useful sense: before the inline diff was fixed to open
from the document, this test died thirty lines earlier and never reached the
staging step at all. Making the feature work is what exposed it.

The mechanism is in how the hunk controls go away:

    row.widget->deleteLater(); // an action button may be the caller

`deleteLater()` means the old buttons are still children until the event loop
runs. The test waits like this:

    QTRY_VERIFY((buttons = diffWidget->findChildren<QAbstractButton *>(),
                 buttons.size() == 2));
    buttons.first()->click();

Two buttons existing is not two of the *right* buttons. When the diff is
recomputed the previous hunk's controls are pending deletion, the count passes
through 2 on the way down, and the click lands on a button whose hunk is gone -
so nothing is staged and the assertion after it fails.

The fix is to make the count mean what it says, by flushing the deferred
deletions before taking it:

    QTRY_VERIFY((QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete),
                 buttons = diffWidget->findChildren<QAbstractButton *>(),
                 buttons.size() == 2));

Six of these in the test, four counting and two checking for none. Six runs
afterwards, all green: against a 40% failure rate that is about a 5% outcome by
chance, which is the strongest evidence available short of understanding the
refresh timing exactly.

Worth keeping generally: `QTRY_VERIFY` on a *count* of objects is only as good
as the objects being the ones meant. With `deleteLater()` anywhere nearby, a
count can be satisfied by things on their way out.

## The workstreams are done, and reading the warnings found the one defect left

The instructions have named "Debugger General is next" for a long time. It is
not next; it is done - `CommonSettingsPage.qml` draws
`TableDelegate { aspect: root.aspects.SourcePathMap }`, the aspect hands out a
model from `SourcePathMapAspect::tableModel()`, and there is a test file for
it. All three workstreams check out item by item:

| | |
|---|---|
| Tables | `AspectTable`, `tableModel()`, `TableDelegate`; CPU Usage and Debugger General both drawing one |
| The three aspects | AnalyzerMessagesAspect, FrameworksAspect, SourcePathMapAspect all have `tableModel` **and** `presentation()` |
| The editor pages | Snippets, Font && Colors and Code Style: `setQmlSource`, no `Layouting::` |

What that verification turned up is more interesting than the verification.
The Debugger suite passes, and in among the passes:

    QDEBUG : SourcePathMapTest::testHalfFilledRowStaysARowButNotAMapping()
      SOFT ASSERT: "!guiToVolatileValue()" in aspects.cpp:622

The code it comes from says what it means:

    // We assume m_volatileValue to reflect current gui state as invariant after
    // signalling settled down. It's an aspect (-subclass) implementation problem
    // if this doesn't hold. Fix it up and bark.
    QTC_CHECK(!guiToVolatileValue());

`SourcePathMapAspect` had **no `connect()` calls at all** - it pulled from its
model only when someone asked, in `isDirty()` and inside `guiToVolatileValue()`
itself. So between an edit and an apply the volatile value was stale, and
`apply()` fixed it up and barked. Its sibling `FrameworksAspect` does connect
to `dataChanged` and does not bark; only this one of the three was wrong.

Three things worth keeping about how this was nearly missed:

- The totals said 48 passed. A `QTC_CHECK` is a debug message, so the suite is
  green either way. This is the second time in this effort that reading the
  warnings of a passing run found a real defect.
- It is not cosmetic: `QTC_CHECK` is fatal under `QTC_FATAL_ASSERTS`, so a
  build configured that way crashes where this one merely complains.
- A test asserting the *outcome* - apply, then check the value - passes with
  or without the fix, because the fix-up in `apply()` makes the value right
  either way. The test added here installs a message handler and asserts the
  **complaint** is absent. Its control, removing the connections, fails it with
  the soft assert quoted back.

### Clearing the noise that hid it

Having found one defect by reading a passing run's warnings, the obvious next
move is to read all of them. Sorting every soft assert in the recent logs:

     95  "isLoaded()" qtversionmanager.cpp:738          - startup, unrelated
     18  "inner.spanCols == 1 && ..." layoutbuilder      - ProjectExplorer's two
     15  "listViewDataCallback" aspectlist.cpp:249         known-failing tests
      4  "!guiToVolatileValue()" aspects.cpp:622        - the one just fixed
      2  "renderAspect(*this, parent)" aspects.cpp:553

The `renderAspect` one reads alarmingly - an aspect the renderer could not
draw - but its comment says "Reaching the check means no renderer was
installed", and it only appears in the two ProjectExplorer tests that fail
here anyway. A test-setup condition, not a page that loses a setting.

The `listViewDataCallback` one is worth acting on. `AspectList` answers with
the literal string "No listViewDataCallback set" when its owner never supplied
one - text a reader would see in the list. Every list in the product does
supply it: cppeditor's custom templates, gitlab's servers, axivion's mappings,
todo's keywords. The fifteen barks all come from one QuickUi test whose
fixture did not, because that test is about the *details pane* and never looks
at the row text.

Setting a callback in that fixture takes the QuickUi suite from sixteen soft
asserts to one. That is worth doing for its own sake: the reason the
`guiToVolatileValue` defect went unnoticed for so long is that a run's output
was mostly complaints nobody had a reason to read. Noise is not free; it is the
thing a real complaint has to be spotted among.

## Scrolling, measured again - and the build is the headline

Reported as still laggy. The obvious suspect was `updatePaintNode()`, which
deletes the whole scene graph and rebuilds a `QSGTextNode` per row every
frame - the classic Qt Quick anti-pattern. Measuring says it is not:

    updatePaintNode   median 0.261 ms   p90 0.538   max  1.395
    updatePolish      median 0.812 ms   p90 5.698   max 16.902

The layout is typically fine and has a long tail, and the tail is where lag
lives. Splitting the layout:

    rebuildVisibleLines   median 0.465   p90 3.630   max 14.910   total 398 ms
    ghost rows            median 0.000   p90 0.001   max  0.066   total   0.4
    scroll bar            median 0.016   p90 0.111   max  0.222   total  10.1

**55% of all layout time is turning rows into QVariantMaps** - the very code
added earlier in this effort to make scrolling faster. The most expensive part
of a row is the list of syntax formats over it, and no QML reads it: the four
mentions of `formats` in .qml files are all comments, and the twelve readers
are all tests. Building the model without it is 12% off a layout.

That leaves the QVariantMap itself, which is the real remaining cost: about
twenty-seven keys per row per layout, each a string key and a QVariant.

And the thing that matters more than any of it: **both configurations in this
tree are AddressSanitizer builds.**

    WITH_SANITIZE:STRING=ON
    SANITIZE_FLAGS:STRING=address
    Debug:   4300 of 4423 entries built with -fsanitize=address
    Release: 4715 of 4836

ASan costs a few times the CPU generally, and it is worst on exactly what this
hot path does: many small allocations. Twenty-seven QVariants per row per frame
is a shape that a sanitised allocator punishes far more than an ordinary one.
So the numbers above are inflated, and a reader scrolling either build is
scrolling a sanitised one.

Which means there are two separate answers. A build without the sanitizer is
very likely to feel different, and that costs a reconfigure rather than a code
change. Independently, allocating twenty-seven QVariants per row per frame is
worth fixing on its own terms - either by not rebuilding rows a scroll did not
change, or by giving the model real roles instead of a map. Both are larger
than this batch and neither should be guessed at while the measurement is
taken through a sanitizer.

## The row map's keys were being built from UTF-8 every frame

Half of a layout is turning rows into `QVariantMap`s, and the question was
what inside that is expensive. It is not the values: `whitespace` and
`scopeBands` are already `QVariantList`s on the row, `text` is a shared
`QString`. It is the keys.

`QVariantMap{{"text", ...}, {"lineNumber", ...}, ...}` builds a `QString` from
a `const char *` for every key of every row of every layout - twenty-three
UTF-8 conversions and allocations per row, before the red-black tree nodes
`QMap` needs to hold them. Fifty rows on screen is over a thousand string
allocations a frame, for keys that never change.

`QStringLiteral` makes each one a static, allocation-free `QString`. Nothing
else changed - same keys, same values, same map. Three before/after pairs, run
alternately so a drifting machine cannot favour one:

    rebuildVisibleLines  before 0.419 0.420 0.424   after 0.355 0.350 0.357
    updatePolish         before 0.801 0.731 0.772   after 0.683 0.665 0.685

16% off building the rows and 12% off the whole layout, with the two sets not
overlapping at all. All six runs 339 passed, 0 failed.

A key name is what the tests read rows by, so the control is direct: spelling
one of the twenty-three `linenumber` instead of `lineNumber` fails eleven
tests.

What is left is the `QMap` itself. The next step is the one that removes it -
giving `VisibleRowsModel` a role per value instead of one map per row, so that
a binding on `y` costs a `data()` call rather than a whole row. That needs the
delegates in `CodeViewport.qml` and `EditorGutter.qml` to name their roles,
and the names have to be prefixed: a role called `y` or `width` shadows the
`Item` property of the same name.

## A role per value instead of a map per row

Making the keys static took 16% off building the rows, which left the `QMap`
itself: a red-black tree of twenty-three entries per row per layout, handed to
QML as one `modelData` object.

The map was doing two jobs. It was what the delegates read, and it was the
fingerprint `setRows()` compares to tell a scroll from a change - so it had to
carry values no delegate ever reads. Asking which keys QML actually binds to
settles it: fifteen, and `text`, `formats`, `selectionFills`, `scopeBands`,
`newlineTail`, `preedit`, `folded` and `foldable` are not among them. The text,
its highlighting and its selection are drawn by `updatePaintNode()` straight
from the `Line`, and `updatePolish()` ends in an unconditional `update()`, so
what the model says has no bearing on whether they are repainted.

So the model now holds a `RowView` - a plain struct of the fifteen values a
form reads - and `VisibleRowsModel` has a role for each. A binding reads the
one role it wants; the comparison that finds a scroll compares two structs
instead of walking two trees. `visibleLine()` still builds the whole map, which
is what the tests read a row through.

    rebuildVisibleLines  before 0.359 0.353 0.355   after 0.013 0.016 0.014
    updatePolish         before 0.733 0.716 0.706   after 0.364 0.402 0.396

Building the rows is now about 25 times faster at the median and half the total
time; a whole layout is 45% faster. Together with the keys, a layout went from
0.80 ms to 0.39 ms at the median - it more than halved.

Two details that this depends on and would silently break:

- The delegates read `model.y`, not a required property called `y`. A role of
  that name would shadow `Item.y`, and so would `width`.
- A role name is a string, and a wrong one is `undefined` rather than an error.
  The control is direct: renaming `lineNumber` in `roleNames()` fails six
  tests and renaming `y` fails five, all of them reading through the
  delegates rather than through `visibleLine()`.

The `?? ""` fallbacks in those bindings were dead once `data()` answered every
role, and they were what would swallow a misspelled one. Removing them changed
nothing - both suites stayed green and not one new warning appeared, which is
what "dead" means - but it changed what a mistake looks like. Renaming
`annotationX` in `roleNames()` now fails
`testTheComponentTurnsAClickIntoACaretAndADragIntoASelection`, which asserts
that QML complained about nothing, with the file and line of the binding that
broke. With the `?? 0` it drew every annotation at x=0 and no test minded.

## Where a layout's time goes now, and telling QML less

With the map gone, the phases of `updatePolish()` measured across the suite:

    selections   median 0.062  p90 0.234  max  12.729  total 189.6
    gaps         median 0.005  p90 0.005  max   0.046  total   1.8
    colors       median 0.019  p90 0.039  max   0.214  total  10.0
    reuse        median 0.007  p90 0.020  max   0.048  total   3.7
    shape        median 0.080  p90 0.327  max   0.995  total  58.2
    widest       median 0.001  p90 0.001  max   0.002  total   0.3
    scrollclamp  median 0.001  p90 0.001  max   0.042  total   0.3
    ghosts       median 0.000  p90 0.001  max   0.063  total   0.4
    rowbuild     median 0.005  p90 0.007  max   0.021  total   2.2
    setrows      median 0.006  p90 1.650  max  16.089  total 227.4
    scrollbar    median 0.019  p90 0.108  max   0.207  total  14.6
    emitMetrics  median 0.000  p90 0.155  max   1.301  total  25.6
    emitCaret    median 0.000  p90 0.131  max   0.191  total  12.3

Two things worth reading twice. Shaping the text - the thing a text editor is
supposed to spend its time on, and the thing the row cache exists to avoid -
is 58 ms of it. And building the rows, which was half a layout three changes
ago, is now 2.2 ms. What is left is `setRows`, at 227 ms, and it is not doing
arithmetic: it is telling QML that rows changed, and QML is believing it.

It announced the whole screen whenever anything at all was different, so typing
a character made every delegate on every visible row evaluate every binding it
has. The model can do better now that it holds comparable structs: work out
which rows actually differ, in runs, and announce only those. Over the suite
that is **853 row-announcements instead of 2293** - a number that owes nothing
to the sanitizer, unlike the timings here.

The timing improvement is real but small: `rebuildVisibleLines` 231 ms to 212
(-8%), a whole layout -4%, over three alternating pairs. That is because a test
suite opens and scrolls far more than it types, and opening really does change
every row. The case it is for is the one a reader is in all day, and the test
pins it exactly: typing announces one row, and with the old policy the same
assertion reports row 0 of the whole range.

The `selections` figure above is a correction. It first measured a 539 ms
maximum, reproducibly, and a half-second stall is exactly what "laggy" feels
like - so it went in as the outstanding thread. It was not real.
`updatePolish()` returns early for an empty document, and that return sits
*after* the calls the first phase marker enclosed, so every early return left
that phase's clock running until the next layout and charged it all the idle
time in between. Closing the phase before the return brings its maximum to
12.7 ms and its total from 726 ms to 190.

The lesson is worth more than the number: a phase timer that brackets a region
containing a `return` measures wall-clock between calls, not work. Three things
were blamed and cleared by measurement before the instrument itself was
suspected - `updateParenthesesMatch`, `updateDocumentSelections` and
`editorLayout()` are all under 0.02 ms.

What the same investigation did turn up is real, and is in the `rowsBlock`
line: when wrapping is on, every layout runs

    for (QTextBlock b = text->firstBlock(); b.isValid(); b = b.next())
        rows->blockBoundingRect(b);

over the *whole document*, not the screen. Fifty such layouts in the suite:
median 2.1 ms, max 10.8 ms, total 155 ms, on documents of 81 and 201 blocks.
That is roughly 0.05-0.13 ms a block, every layout, which is a full block
layout rather than a cache hit - so a wrapped file of ten thousand lines would
spend something like a second per layout on it. The view is otherwise careful
to be O(screen): its own comment says the arithmetic is what "holds at a
million lines".

The fix is to prime only when something that invalidates the cached block
layouts has changed - the wrap width, the font, the tab width, breakindent or
showbreak. It is not a change to make at the end of a batch: the invalidation
set has to be right or the row count is wrong, and a wrong row count misplaces
every line on screen.

## A wrapped document was laid out again on every layout

The last batch left this as the thing to look at, and the guess in it was
wrong. `setTextWidth()` is not slow - 0.010 ms median. What it *does* is throw
away the block layouts that could wrap differently, and the priming loop right
after builds every one of them again. It was called on every layout, with the
same width 38 times out of 50.

Counting rather than timing, because timings here go through a sanitizer and
counts do not: of 2618 blocks walked, **2585 were cold and every one of them
was laid out again**. Not the first layout of each document - one document of
101 blocks shows six consecutive layouts relaying 101, 100, 100, 100, 100, 100
of them, with the width unchanged in five.

So the width is only handed over when it changed:

    blocks laid out   2585 -> 1007
    priming loop      median 2.162 ms -> 0.081 ms, total 157 ms -> 67 ms

The loop itself stays. What `lineCount()` answers depends on every block having
been laid out, and an edit or a fold leaves some that have not; it is now a
walk of cache hits rather than a rebuild. The 1007 that remain are the genuine
first layout of each document.

Three notes on how this was arrived at, because two of them are corrections:

- `setTextWidth` was blamed first for being slow. It is not. It is cheap *and*
  destructive, which is worse, and only counting the blocks it invalidated
  showed that.
- The new test written to guard the change did not guard it. Breaking the guard
  deliberately - applying the width once and never again - left
  `testNarrowingTheViewWrapsOverMoreRows` passing and failed two tests that
  were already there, `testWideningTheViewportDoesNotLeaveItScrolledPastTheEnd`
  and `testGoingToALineStillWorksAfterTheWidthChanges`. The premise it was
  written on, that nothing covered a width changing after the first layout, was
  false. It was removed rather than kept: a test that passes either way is
  worse than no test, because it reads like cover.
- The remaining walk is still O(document) per layout. Making it O(screen) means
  `lineCount()` not needing every block, which is a change to
  `TextEditorLayout`, not to this view.

## Only walk the document when a block layout was thrown away

Last batch stopped re-applying the wrap width, which took the priming walk from
2.16 ms to 0.081 ms a layout. What was left was the walk itself: every block of
the document, every layout, to find the handful that need laying out.

Deleting the walk to see what it holds up answers that first - **14 tests fail,
all of them saying nothing wrapped.** It is not a row-count refinement: it is
the only thing that ever lays those blocks out, so wrapping does not happen
without it.

It cannot go, then, but it can be skipped. `TextEditorLayout` already tracks
`layedOut` per block; what was missing was an aggregate. Three methods throw
layouts away - `clearBlockLayout` in both its forms and `relayout()` - so a
counter bumped in those three and read through `layoutGeneration()` says
whether anything was discarded since a view last walked. The view remembers the
generation it primed at and walks only when it differs.

    blocks walked   2618 -> 1010     (26 of 50 layouts skip it entirely)
    blocks laid out 1007 -> 1007     (unchanged: the necessary work)
    priming walk    median 0.081 ms -> 0.000 ms

1010 walked against 1007 laid out is the point: three wasted block visits in a
whole suite, against 1611 before. The necessary work is untouched.

The Utils side is additive - a private counter, three increments, a const
accessor nothing that existed reads - so it cannot change what the widget
editor does. FakeVim was run anyway: 253 passed, 2 failed, and HEAD gives the
same two (`test_vim_visual_selection_focus_out`, `test_vim_script_throwpoint`).

### The test written for this did not guard it, again

`testTypingMoreTextWrapsOverMoreRows` was added because making
`layoutGeneration()` a constant failed only one test, which is thin cover for a
change in shared code. It passes with the constant too: an edit is served by
the lazy path, not by the walk, so it never discriminated. That is the second
batch running where a test written alongside a change turned out not to guard
it - both times found by breaking the change on purpose and watching which
tests noticed.

It was kept this time rather than removed, because unlike last batch's it
covers something real that nothing else covers - a wrapped line re-wrapping
when it is typed into - and its comment now says so instead of claiming to
guard the walk. What guards the walk is
`testGoingToALineStillWorksAfterTheWidthChanges` and
`testScrollingAWrappedDocumentLandsOnTheRightLine`.

## Where the layout ended up

Five changes later, measured with the same instrument on the same machine as
the first reading in this effort:

    updatePolish        median  0.882 -> 0.355    p90 6.041 -> 2.712
    rebuildVisibleLines median  0.465 -> 0.011    p90 3.630 -> 1.696

A layout is about two and a half times faster at the median and rather more
than twice as fast at the ninetieth percentile, which is the one a reader
feels: the median frame was never the problem, the slow one was. Building the
rows, which started as more than half of a layout, is now 0.011 ms.

The five, in the order they were found, and none of them where the search
started:

1. The rows QML reads carried the syntax formats, which no delegate reads.
2. Every map key was built from UTF-8 again for every row of every layout.
3. The map itself: a red-black tree per row, replaced by a role per value.
4. `setRows` announced the whole screen whenever one row differed.
5. The wrapped-document walk, twice - re-applying a width that had not
   changed, and then walking the document at all when nothing was thrown away.

What is left is shaping the text, at 58 ms over a suite, which is the work the
editor exists to do. There is no obvious next thing to take out, and looking
for one would be guessing rather than measuring - the last four batches each
began by disproving where the time appeared to be.

Two standing caveats on all of the above. Every number here is from an
AddressSanitizer build, because both configurations in this tree are; the
ratios hold but the absolute figures do not. And a test suite opens, scrolls
and closes far more than it types, so the numbers under-report the changes
aimed at editing - which is most of what changes 3 and 4 were for.

## Column selection, which the Quick editor did not have

The widget editor takes a rectangle of text on Alt+drag and extends one on
Alt+Shift+click - a caret per line, all selecting the same columns, which is
what makes it typeable over. The Quick viewport had Alt+click for an extra
caret and nothing for the rectangle.

The arithmetic is worth sharing rather than writing twice. It is tab-aware in
a way that is easy to get subtly wrong: what lines up on screen is columns, a
tab is one character and several columns, and a line too short to reach either
column carries no caret at all rather than an empty one. So
`generateCursorsForBlockSelection` came out of `TextEditorWidgetPrivate` into
`cursorsForBlockSelection()` in `blockselection.h`, with the widget calling it
and keeping its own `BlockSelection` name for the struct that moved with it.

The viewport gets `anchorBlockSelection(position)` and `selectBlockTo(x, y)`,
and QML keeps only the policy - which modifiers mean a rectangle. The
coordinates go to C++ rather than a document position because past the end of
a line there are no characters to count: how far beyond it the pointer is has
to be measured against the character width, and that is what lets a rectangle
be dragged out over lines shorter than it.

`testAltDraggingTakesARectangleOfText` drags from column 2 of the first line
to column 5 of the fourth and asks for four carets each with "cde" selected.
With the drag branch taken out it reports one caret, which is the ordinary
selection it would otherwise be - the first test in three batches that failed
the way it was meant to on the first try.

Alt+Shift with the *arrow keys* is deliberately not ported: the widget editor
guards it with `!HostOsInfo::isMacHost()`, so there is no behaviour to match on
this platform and no way to test what was written for the others.

## The qbs resolve that could not be run, run

Last batch reported the `.qbs` edit as verified only statically, because the
qbs binary hung. It was the wrong binary: the one in
`Qt Creator 18.0.1` hangs on `--version`, never mind a resolve. `16.0.1` ships
qbs 2.6.0 and works.

Two things that were not obvious. Global options go *after* the subcommand, so
`setup-toolchains --detect --settings-dir <dir>` - which also keeps the user's
own qbs settings untouched. And the generated Qt profile needs
`baseProfile clang`, not one of the xcode ones: the xcode module dies on this
Xcode with "MacOSX Architectures.xcspec: No such file or directory".

The resolve still does not succeed overall - every product under vendored
`src/shared/qbs` fails with "product 'qbscore' is disabled", which is
environmental and aborts the run. So "exit 0" is not available as a check, and
the useful question is whether the resolve reached *this* product. It does, and
the way to know is a negative control: adding a `"thisfiledoesnotexist.cpp"` to
`texteditor.qbs` makes it print

    texteditor.qbs:6:5 Error while handling product 'TextEditor':
    texteditor.qbs:24:16 File '...thisfiledoesnotexist.cpp' does not exist.

It names the product when broken, so its silence when whole means something.
`blockselection.cpp` and `.h` resolve.

## Alt+Shift+click, tested rather than assumed

The same batch shipped two entry points to a column selection and tested one.
`testAltShiftClickingTakesTheRectangleFromTheCaret` covers the other: a plain
click to put the caret at column 2 of the first line, then Alt+Shift+click at
column 5 of the fourth, and four carets each with "cde". Taking the Shift
branch out leaves two carets - the plain Alt+click that adds one - so it bites.

## What the Quick editor still cannot be asked to do

The widget editor registers 96 commands; the Quick one registers three, plus
follow-symbol. Matching method names between the two says 86 are missing, and
that number is wrong in a way worth writing down, because it is the obvious
survey to run and it overstates the answer badly.

Most of those 86 are movement and clipboard - `gotoNextWord`,
`selectAll`, `redo`. The viewport does all of them; it just does not have
methods of those names, because it answers `QKeySequence::SelectAll`,
`QKeySequence::Undo` and the rest directly, and routes cursor movement through
the same `handleMoveKeyEvent` the widget uses. Press the key and it works.

So the gap is not behaviour, it is that the behaviour cannot be *asked for*.
An action registered in the widget's context is dead when a Quick editor is
current, which means the menu entry is greyed, a shortcut the user rebound
does not arrive, and `setScriptable(true)` reaches nothing. The three that were
already done say exactly this in their comments.

Underneath that there is a second, smaller gap that is real absence: the line
commands. `moveLineUp`, `joinLines`, `duplicateSelection`, `sortLines`,
`uppercaseSelection` and their neighbours have no viewport implementation under
any name and no key handling either, so they are unreachable in a Quick editor
by any route.

Join Lines is the first of them, done the whole way: the implementation was
nothing but a `MultiTextCursor` transformation, so it moved out of
`TextEditorWidget` into `textoperations.h` - a home for the ones that follow,
since every one of them is the same shape and every one has to be answerable
by both editors. The widget calls it, the viewport calls it, and the Quick
editor registers `JOIN_LINES` so the menu entry stops being dead.

Two tests: joining pulls the next line up with its leading whitespace
collapsed and undoes in one step, and a read-only buffer is left alone.
Removing the read-only guard fails the second, which is the one worth having a
control for - the first would fail if the command did nothing at all.

The remaining line commands are the obvious next batch, and they are all the
same shape as this one.

## Four more line commands, and two controls that did not bite

Case transformation and line insertion, the same shape as Join Lines: pure
`MultiTextCursor` transformations that belonged to neither editor, so they
moved into `textoperations.h` and both call them. `insertLineAbove` and
`insertLineBelow` take the `TextDocument` as well, because the line they open
is indented the way that document would indent it. The Quick editor registers
`UPPERCASE_SELECTION`, `LOWERCASE_SELECTION`, `INSERT_LINE_ABOVE` and
`INSERT_LINE_BELOW`, so four more menu entries stop being dead.

The interesting part of this batch was that both negative controls passed.

The first was meant to guard the rule that upper casing with nothing selected
takes the word under the caret, but only while there is one caret - several
carets each swallowing a word is not what one key press meant. Taking the
`&& !several` out changed nothing, because the test asked two carets sitting
in `alpha` and `gamma` to **lower** case. Words that are already lower case
come back identical whether or not they were selected, so the assertion could
not tell the two behaviours apart. Asking for upper case instead makes the
same test fail when the rule is removed.

The second was worse: removing the read-only guard from `insertLineAbove`
failed nothing at all, because the read-only test written the batch before
covered only `joinLines`. Each command carries its own guard, and a test per
command is exactly the kind of thing that gets written for the first one and
then not for the rest - so there is now one test that puts all five commands
to a read-only buffer and compares the whole text before and after. Removing
any single guard fails it.

Both now fail when the behaviour they describe is removed. The pattern in
these three batches is consistent enough to be worth stating plainly: a test
written alongside a change is not evidence until the change has been broken
and the test has been watched to fail. Four times out of six here, the first
version of the test did not.

## Sort and Duplicate, and a control that cancelled itself out

Two more, the same shape. `sortLines` needed the tab settings, because what it
sorts without a selection is the run of lines around the caret sharing its
indentation, and indentation is measured in columns. `duplicateSelection`
needed the comment definition - and that is where the Quick editor stops:
which markers a language comments with is something it is not told, so
`DUPLICATE_SELECTION_AND_COMMENT` and `UN_COMMENT_SELECTION` stay with the
widget editor until it is. That is one gap blocking several commands rather
than several gaps, which is worth knowing before porting them one at a time.

Six commands now share one read-only test, and getting it to actually catch
anything took three goes. It is the same lesson as the last two batches with a
new mechanism each time, which is why it is worth writing all three down:

1. The fixture said `first\nsecond`. Removing the guard from `sortLines` let
   it sort - and those lines were already in order, so nothing changed and the
   test passed. Content that the command would visibly change is part of the
   assertion, not scenery. It says `zebra\napple` now.
2. Removing the guard from the case commands still changed nothing, because
   the test called `uppercaseSelection()` and then `lowercaseSelection()`.
   Both go through the same guard, so removing it enabled both, and the second
   put back exactly what the first had done. The uppercase call is last now.
3. Only after those two did removing any one of the six guards fail the test,
   which was checked by removing each of the six in turn rather than by
   removing one and assuming.

The third point is the one worth keeping. A control that bites tells you about
the line you removed; it says nothing about the five you did not.

## The comment definition, which was one gap holding three commands

`highlighterhelper.h` already said this out loud - "which comment markers its
shortcuts use ... a view that is not a widget has none of it". It was one
missing piece, not three: `UN_COMMENT_SELECTION` and
`DUPLICATE_SELECTION_AND_COMMENT` both wanted the same thing, and the widget
derived it in a private method nobody else could reach.

Deriving it needs only the syntax definition and the typing settings - which
decide whether a line comment goes at the margin or in front of the text - and
both are document-level. So `commentDefinitionFor()` and
`definitionForDocument()` join the other helpers there, the widget's
`setupFromDefinition()` and `currentDefinition()` become calls to them, and the
viewport asks the same question of the same document. `Utils::unCommentSelection`
needed no porting at all; it was already a free function taking a cursor and a
definition.

### Three notes on a test that would not bite

The read-only test now covers eight commands, and getting there found three
different things, only the first of which was interesting:

1. Its document was a `.txt`, so it had no language, so the comment commands
   had no markers and did nothing whether or not they were guarded. It is a
   `.cpp` with a definition put on it now. Content has to be something every
   command would change; a language is part of the content.
2. Removing the guard from the two comment commands was "not caught" for the
   dull reason that the test never called them: the edit that was supposed to
   add the calls used a `replace()` whose anchor did not match, and nothing
   asserted that it had. Every other edit in these batches asserted its anchor.
   A silent no-op edit looks exactly like a subtle behaviour question, and
   several minutes went into treating it as one.
3. Only then did removing each of the eight guards in turn fail the test -
   checked one at a time, because six of them passing says nothing about the
   seventh.

The two controls that mattered did bite first time: a comment definition with
its single-line marker taken out fails the commenting test, and a
`definitionForDocument()` that finds nothing fails both comment tests.

## Delete, cut and copy a line

Five more, and the two pieces underneath them: `maybeSelectLine`, which grows
a caret that has selected nothing to cover its whole line, and
`copyLineUpDown`, which puts a copy above or below and leaves the caret on the
copy. Both were private to the widget and are now shared.

One distinction worth keeping rather than smoothing over: Copy Line has no
read-only guard and Cut Line does. Copying does not change the text, so there
is no reason a buffer that cannot be edited should refuse it, and a test says
so - it copies out of a read-only buffer and checks the text is untouched.

Delete Line takes the main caret's line even when there are several carets,
which is what the widget editor does. That looks like it should take all of
them, but the Quick editor matching the widget matters more than the Quick
editor being right on its own, so it is written down here rather than quietly
changed.

The sweep of read-only guards caught three of four. The fourth was not a guard
but a branch: `maybeSelectLine` handles the last line of a file that does not
end in a newline separately, because there is no newline after it to swallow
and it has to take the one before instead. Nothing exercised it - every test
so far used a file ending in a newline, where that branch is never reached,
including the ones written this batch. A file without one, and deleting its
last line, fails when the branch is taken out.

That is now the fourth kind of hole these sweeps have turned up: content that
cannot show the difference, two commands that undo each other, an edit that
silently did not apply, and now a branch no fixture ever reached. None of them
were visible from a passing test.

The Quick editor answers 18 commands now, from three.

## Moving a line, which was the one that needed reading

The 112 lines of `moveLineUpDown` were the reason this command was left until
last, and most of them turned out to be one thing: the widget editor's
refactor markers have to be measured against the old text, because a
`QTextCursor` inside text that is removed collapses with it. That measurement
sits in the middle of the operation, so the operation could not simply be
lifted out around it.

It splits in two instead. `selectLinesToMove()` says which lines are about to
move, and `moveSelectedLines()` moves them. The marker bookkeeping stays in the
widget, between the two calls, where it belongs - the Quick editor has no
refactor overlay and now says so in one line rather than carrying the code for
one.

Two details came with it. The moved text is re-indented unless it is entirely
commented out, on the grounds that where a comment sits is the reader's
business - which needs the comment definition, and that arrived last batch, so
this command could not have been ported before it. And moving a line twice is
one thing the reader did, so the second move joins the first one's undo step;
the widget tracks that with a flag cleared on every key press, and the viewport
now has the same, cleared in its own key handler.

### An assertion that could not see what it was checking

The control for "the caret goes with the line" did not bite. The test asked
for `blockNumber()`, and the two candidate positions - the start of the moved
text and its end - are on the same line, so the number is the same either way.
Comparing `cursorPosition()` against the block's own position tells them
apart, and then removing the behaviour fails the test.

That is the fifth distinct way one of these tests has failed to bite, and the
first where the assertion was about the right thing but not precise enough to
see it.

The Quick editor answers 20 commands now. What is left of the list is
`rewrapParagraph`, and the movement and clipboard commands that already work
by key but are not registered as actions.

## Rewrap Paragraph, and testing the claim the rest of these batches made

`rewrapParagraph` is the last of the line commands and the least interesting
of them: a long function with no widget in it at all, needing only the tab
settings and the margin column. It moved across whole. The test reflows three
short comment lines into one and checks the ` * ` they share is still at the
front rather than folded into the middle, because keeping that prefix is the
only subtle thing the function does.

The more useful part of this batch was testing something six batches have
asserted and none had checked. Every test so far called the viewport's method
directly, which shows the command works, not that anything can reach it - and
"the menu entry stops being dead" was the reason for registering them at all.

Testing it took three tries, and the first two looked exactly like the feature
being broken:

1. `command->action()->isEnabled()` is **false**, and asking the same of a
   widget editor in the same test gives false too. Whether the front action is
   enabled depends on what has focus, and in a `-test` run nothing does. It
   cannot tell a registration from a missing one, in either editor.
2. `command->actionForContext(editor->context().at(0))` finds nothing, because
   an editor's context is two ids - the editor type and one of its own - and
   the commands are registered against the second.

Asking every id in the editor's context finds the action, and triggering it
joins the lines. Taking the registration out fails the test. So the claim
holds, and there is now something that would notice if it stopped holding.

Worth keeping in mind for the commands still unregistered: a shortcut is
processed before the key event reaches the item, so registering an action for
a key the viewport already handles takes that key away from the viewport's own
handler. Those registrations are only safe where the action and the key path
run the same code, which for movement means exposing what `handleMoveKeyEvent`
does rather than writing a second version of it.

## A locked file was being edited, and the clipboard commands

Two questions decide whether this view may change its text, and they are
separate: the view can be told to be read only, and the file itself can be one
the filesystem will not take back. Nothing sets either from the other. The key
handler has always asked both - `m_readOnly || doc->isFileReadOnly()`, in five
places - and every command added over the last six batches asked only the
first.

So opening a file that is read only on disk and pressing Ctrl+J edited it,
while typing into the same file did nothing. A test that locks a file with
`QFile::setPermissions` and runs six of the commands over it fails on the old
code with `"\n\nzebra apple\n"` where the file had said `"zebra\napple\n"`.

`canEdit()` is now the one place that answers it, and the fourteen guards -
thirteen commands and the key handler the definition came from - all ask it.

### Registering the commands whose keys already worked

Select All, Copy, Cut, Paste, Undo and Redo were handled inline in
`keyPressEvent`, so the keys worked and the menu entries did not. Registering
them needed care rather than more of the same: **a shortcut is processed
before the key event reaches the item**, so an action for Ctrl+C would not add
a way to copy, it would take the existing one away and put its own in place.
Had the two drifted, the key would quietly have started doing something
slightly different.

So the bodies moved out of the key handler into methods, the key handler calls
them, and the actions call the same methods. Whatever a shortcut does now,
the key does, because there is only one of each. The reachability test covers
Select All and Copy through the action system on a real editor, and removing
either registration fails it.

Twenty-eight commands now, from three. What is left is movement - and it needs
the same treatment, one level harder: the movement keys go through
`handleMoveKeyEvent`, so the actions have to be given a way to ask for the
same thing rather than a second implementation of it.

## Moving about, and a test that passed for the wrong reason

The movement commands were the ones left, and the constraint from the batch
before applied: their keys already worked, a shortcut is handled before the key
reaches the item, so an action must ask for the very thing the key asks for.
`handleMoveKeyEvent()` moves every caret through
`MultiTextCursor::movePosition()`, so the actions call that - one level below
the key event, not a second implementation of it. Which layout the movement is
measured against went the same way: it was inline in the key handler and is now
`movementLayout()`, which both use, because a wrapped view moves down a row
rather than over a whole line and only its own layout knows that.

Twenty commands: sixteen that the widget maps straight onto a
`QTextCursor::MoveOperation` - taken out of `texteditor.cpp` by reading the
mapping rather than by guessing it - and four camel case ones, which go through
`Utils::CamelCaseCursor` and were already shared.

The test for it passed while being wrong, which is worth writing down because
the assertion looked careful. It moved the caret to 0, triggered Go to Next
Word, and checked the result was past position 1 - "a word, not a character".
It reported 19. The document was nineteen characters long, and the caret had
not moved at all: the Select All two lines earlier had left a selection,
setting the position does not clear one, and a move with `MoveAnchor` over a
selection collapses it to its end. So the assertion held for a caret that never
moved, and would have held whatever the operation was.

Clearing the selection first and comparing against 6 - the start of the second
word in "first second" - makes it a real question. Wiring the same action to
`NextCharacter` instead now fails it, and so does ignoring the move mode.

That is the sixth kind: content that could not show the difference, two
commands that undid each other, an edit that never applied, a branch no fixture
reached, an assertion too coarse to see the answer, and now a fixture whose
earlier steps made the later ones meaningless. Every one of them was a passing
test.

Forty-eight commands now. What is left of the ninety-six is what needs
machinery rather than wiring: the assist commands, the symbol commands, folding
and the block ones, and Go to Line Start with its two-step home.

## Folding, and two tests that were wrong in different ways

Five commands. The viewport could already fold a line the pointer landed on;
what it could not do was fold *the block the caret is in*, which is what the
menu entry means - standing in the middle of a function and asking to fold
means the function. That choice is pure, so `blockToFold()`, `blockToUnfold()`
and `hasUnfoldedBlocks()` are shared and the widget's `fold()`, `unfold()` and
`toggleFoldAll()` are now three lines each.

`hasUnfoldedBlocks()` is deliberately that way round rather than
`hasFoldedBlocks()`. It was written the other way first and would have
inverted Fold All: the widget closes everything while anything is still open,
and only opens everything once nothing is. Reading the widget's loop rather
than naming the predicate by intuition is what caught it, before it was wired
to anything.

The rest of the viewport's folding - waiting for the highlighter, asking the
layout to redraw, bringing a caret back out of what was just folded - was
already there for `toggleFold()` and now sits in `waitsForHighlighter()` and
`foldingChanged()`, which all five commands use.

Both tests failed, and neither failure was the code:

1. They waited for `hasUnfoldedBlocks()` and folded as soon as it was true.
   That is true as soon as the highlighter has produced *some* folding
   indents, and folding while it is still running defers until it has
   finished - so the fold was scheduled rather than done, and the assertion
   ran first. Waiting for `syntaxHighlighterUpToDate()` is waiting for the
   right thing.
2. The fold was then asserted to leave the brace line showing. It does not:
   the block that owns the fold is `int f()`, and the brace is inside what was
   folded. The test said what was expected rather than what folding means.

That is the seventh and eighth. Neither would have been visible without
watching them fail for a reason and checking whether the reason was the one
being claimed.

Fifty-three commands. What is left needs machinery this view does not have
yet: the assist commands want a completion widget, the symbol ones want the
language client, and Go to Line Start wants its two-step home lifted out of
the key handler the way the clipboard ones were.

## Home, the View commands, and selecting a word

Eight, and the last of the ones that needed only wiring.

Home is its own command because the key does more than move to the start of
the line: it goes to the first thing *on* the line and only from there to
column zero, and back again. The viewport already had `moveToFirstCharacter()`
for the key, so the command calls that.

The View commands move what is shown without moving the caret, which is the
whole difference between View Page Down and Page Down. How big a page is was
inline in the key handler and is now `rowsPerPage()` - a screen of rows less
one, so the line the reader was looking at is still there afterwards - which
both use, for the same reason every shared thing in these batches is shared.

Three controls, and the third missed at first for a reason that has now come
up four times in different clothes: `selectWordUnderCursor()` leaves a caret
that already has a selection alone, and the test gave neither caret one, so
removing the guard changed nothing. Giving one caret a selection *wider than a
word* is what makes the two behaviours look different - a selection of exactly
one word would be re-selected as itself and the control would still not bite.

The other correction was smaller: the test expected the two carets in the
order they were made, and adding a caret makes the added one the main one, so
the order is the other way about. Sorting the selected texts asks what the
test actually means.

Sixty-one commands. What is left wants machinery rather than wiring - the
assist commands need somewhere to show a completion list, the symbol commands
need the language client, and the block ones need the parenthesis matching the
widget keeps in its overlay.

## The bracket commands, and where an anchor may be dropped

Six: to the brackets around the caret, with and without taking the text on the
way, and growing and shrinking a selection a pair at a time. The finding of
brackets was already shared - `TextBlockUserData` has been static all along -
so what moved was the growing and shrinking, which have real logic: growing
remembers where it started so that shrinking can walk back in from the outside
towards it.

That anchor is the interesting part. It has to be dropped when the selection
goes, or shrinking a selection made some other way would walk back towards
somewhere the reader never was. The widget drops it in `slotSelectionChanged`,
and doing the same here would have been wrong: this view sets the position and
the selection one after the other, so anything listening to `selectionChanged`
sees a moment with no selection in the middle of *every* update - including
the one that grew the selection and set the anchor a line earlier. It is
dropped in `setTextCursor()` instead, where the whole new state arrives at
once.

That was reasoned rather than observed, so it was worth a control: putting the
drop on the selection change instead fails
`testSelectingABlockGrowsAndShrinksAgain`, which is the reasoning confirmed
rather than assumed.

Sixty-seven commands. The two groups left both want something this view does
not have rather than something it has not been told: the assist commands need
somewhere to show a completion list, and the symbol commands need the language
client. Neither is wiring, and neither should be started as if it were.

## The symbol commands, and the machinery they were waiting for

Find Usages, Rename Symbol and Open Call Hierarchy are three lines each in the
widget editor - `emit requestUsages(textCursor())` and so on - and the whole
difficulty is that they are *signals on a widget*. `LanguageClientManager`
connects to them in `editorOpened()`, keyed on
`TextEditorWidget::fromEditor()`, which is null for a Quick editor, so none of
it happens.

The obvious fix does not work. Connecting to signals on the viewport means
including `textviewport.h` in LanguageClient, and that header pulls in
`QQuickItem`: a plugin with no Qt Quick of its own would have to grow one to
be able to ask a question about a symbol. That is the wrong direction, and the
build says so before any of it can be argued about.

`setLinkFinder()` had already met this and its comment says what the answer
looks like - "registered here rather than overridden on an editor widget, so
that a view which is not one can follow symbols too". Follow Symbol works in
the Quick editor for exactly that reason. What was missing was the same idea
for the questions that go the other way, from the view outwards, so
`SymbolRequests` is a small relay in a header with no Quick in it: the view
owns one and asks through it, and the language client connects to that without
knowing how the view is built.

The test asks the three questions and watches the relay, reached the way
another plugin reaches it. What the language client then does needs a server
and is not tested here - the half that belongs to this plugin is the asking.
Three controls: not handing out the relay, not registering Find Usages, and
passing an empty cursor instead of the caret all fail it.

`QuickTextEditor` gained a `Q_OBJECT` and a public `viewport()` on the way,
which is what lets `symbolRequestsForEditor()` recognise it at all.

Seventy commands. What is left is the assist group - completion, quick fixes,
the function hint - and that one really does need somewhere to show a list.

## The assist group, which was mostly not missing

Six batches have ended by saying the assist commands need somewhere to show a
completion list. That was wrong, and finding out how wrong took most of this
batch.

`CodeCompletion` already existed as a QML element, and `CodeEditor.qml` already
drew a popup with it. Widening it from `CodeDocument` to `CodeSource` - the
Quick editor shows a document somebody else opened, which is the latter and
not the former - made it usable here, and a popup went into
`CodeViewport.qml`. Then that file turned out to have completion in it
already: a `completions` component at the caret, fed by
`onCompletionsAvailable`, applying through `viewport.applyCompletion()`. Ten
lines above the place the new popup was being added.

So all of it came back out - the widened property too, because a public QML
API should not change to serve a need that turned out not to exist. What was
actually missing was one line: Ctrl+Space reached the viewport as a key and
the form drew the answer, but `COMPLETE_THIS` was not registered, so the menu
entry and any rebound shortcut reached nothing. It now calls
`requestCompletions()`, which is the method the key handler calls.

The other two are genuinely absent rather than unregistered: there is no
`FunctionHint` or `QuickFix` anywhere in the viewport, by any name. They are
deliberately **not** registered - an enabled menu entry that does nothing is
worse than a grey one, and registering them would produce exactly that.

Seventy-one commands. The remaining two are the first real feature work rather
than plumbing, and the lesson from this batch applies to them: read what the
form already does before deciding what it needs.

## Quick fixes, which needed less than they looked like

The audit first, after last batch: the widget answers this through
`CodeAssistant`, which mentions `TextEditorWidget` forty-two times and owns a
proposal *widget*. Porting that would be a large job, and it is not the job.
The viewport has never used `CodeAssistant`: it asks a provider directly and
hands the words to the form, and quick fixes fit that shape too.

What made it small is that the pieces were already there and already say so.
`AssistTarget` exists because a proposal item used to need a widget to apply
itself to - "naming them here is what lets a Qt Quick view offer completion
too" - and `DocumentAssistTarget` is described as "what a Qt Quick view has".
Neither had a caller in the viewport. So `requestQuickFixes()` asks the
document's quick fix provider, `applyQuickFix()` hands the item a
`DocumentAssistTarget` and lets it rewrite the file itself, and the answer
goes to the form as a list of descriptions.

Applying through the item rather than inserting its text is the whole point.
A completion puts a word in; a fix can move code about, and only the item
knows how. The test says so by giving the view a provider whose one fix
replaces the word it was asked about, and the control that applies at the
caret instead of the proposal's base position fails it.

The fixes get their own list rather than reusing `CompletionPopup`. That one
narrows what it shows by what has been typed and shows nothing when nothing
has - right for finishing a word, wrong for a list of fixes that is complete
as it stands. Bending it would have changed the completion path to fix the
quick fix path.

Seventy-two commands. `FUNCTION_HINT` is the last, and is still genuinely
absent - no hint anywhere, and unlike this one it has no half-built machinery
waiting for a caller.

## The function hint, and the end of the command list

Last batch said this one was genuinely absent with no half-built machinery
waiting. That was half wrong: `TextDocument` has offered
`functionHintAssistProvider()` all along and `IFunctionHintProposalModel` was
already there. What was missing was the view half - the widget shows it in
`FunctionHintProposalWidget`, and nothing else knew how to ask.

It is the one assist command that is not a list to choose from. Completion and
quick fixes are asked for, offered, and taken or dropped; a hint says what the
call being typed takes, and has to keep saying it while the arguments are
written. So the proposal is kept, `updateFunctionHint()` asks the model which
argument the caret is in every time the caret moves, and the whole thing ends
when the model answers -1, which is what it says when the call is closed.

The form shows every overload at once rather than one with a way to cycle
between them, which is what the widget does. Cycling exists because a label
can only hold one line; a column can hold them all, and a list that can be
read needs no cycling. The active argument is carried to the form and not yet
drawn - the widget emboldens it - so that doing the same later needs no new
plumbing.

The test gives the view a model that counts commas and gives up at the closing
bracket, then types through a two argument call: the hint starts at argument
zero, moves to one without being asked again, and goes when the call is
closed. Three controls - not following the caret, never ending, never working
out the prefix - each fail it.

That is the last of the ninety-six. Seventy-three are registered; the rest are
the ones the widget answers with machinery this view does not need or share -
its snippet overlay, its refactor markers, printing.

## Taking a completion, which was being done by hand

The commands are finished, so this is one of the two behavioural gaps noted
while doing them. Taking a completion inserted the word the list was showing,
replacing whatever had been typed of it. That is right for most items, because
most items do put their own text in - which is why it looked correct for as
long as it did. It is wrong for any item that does something else: one that
expands a snippet, or adds the brackets of a call, or rewrites what is around
it. Only the item knows.

Quick fixes were built the right way round last batch, and this is the same
change on the older path: keep the proposal rather than harvesting its words
and dropping it, find the item whose text is the one the form showed, and ask
it to apply itself to a `DocumentAssistTarget`. When there is no proposal to
ask - the words came from somewhere else - the old insertion stays as the
fallback rather than doing nothing.

Every one of the 373 tests already there still passed after the change, which
is the point about most items: with a plain `AssistProposalItem` the two paths
do the same thing, so nothing that existed could tell them apart. The new test
uses an item labelled "expand me" that puts in `expanded()`, and the three
controls - not asking the item, dropping the proposal as before, and applying
at the caret instead of where the word began - all fail it. The last of those
also fails two of the older completion tests, which is worth noting: the base
position was already load-bearing, and nothing had ever moved it.

The other gap is still open and is a question rather than a defect: Delete Line
takes the main caret's line when there are several, because that is what the
widget does. Making the Quick editor right on its own would make the two
disagree, so it stays written down until someone decides which matters more.

## What is still dead, counted rather than guessed

With the assist group done it was worth asking what a reader would still find
grey, rather than working from memory. Comparing the ids the widget registers
against the ids this editor answers: 104 against 73, so 31 were still dead.
The list is worth having because it sorts itself into groups that need quite
different work, and only one of those groups was wiring:

- **Deleting to somewhere** (6). The movement commands' twins: each is that
  move with the anchor kept and what is under it taken out. Done here.
- **Indentation** (4) - Indent, Unindent, Auto-indent, Auto-format.
- **Zoom** (3), **paste variants** (2), **whitespace** (2).
- **The rest** - printing, encoding, the context menu, suggestions, the
  remaining symbol commands - each wanting something of its own.

The six deletions are the movement work from two batches ago used again:
`deleteTo()` is `moveCursor(op, KeepAnchor)` and `removeSelectedText()`, and
the camel case pair go through `moveCamelCase()` the same way. Because they
are the same primitives, the control that matters is the one that shows they
are *not* the same command: wiring the camel case deletion to `NextWord`
deletes `oneTwoThree` where it should take `one`, and fails.

Seventy-nine of a hundred and four now. The twenty-five left are in the table
above, and none of them is a line of wiring away.

## Indentation, and two tests that assumed rather than asked

Four commands, all of them a question for the document's indenter with the
view only saying which text to ask about. `autoIndent()` was worth sharing
because the order matters and the reason is not obvious: an indenter works out
where a line goes from the lines above it, so the carets are sorted into
document order before any of them is touched.

Tab and Shift+Tab already did exactly what Indent and Unindent do, in the key
handler, written out. They call the methods now, so there is one
implementation - the same rule as the clipboard commands, and the control
proves it: making `unindent()` indent fails both the new test and
`testTabIndentsByTheCodeStyleAndNotByATabCharacter`, which was there already.

Both new tests failed first time, and both were the test rather than the code:

- Indenting with nothing selected puts a step in *at the caret* - `te  xt` -
  because that is what Tab does, and `TextDocument::indent()` is what Tab
  calls. Indenting a line means selecting it first.
- Auto-indent did nothing at all, because a file with no language of its own
  gets an indenter that leaves lines where they are. The assumption that it
  copies the line above was invented. The test brings an indenter that puts
  every line four spaces in, which turns the question into the only one the
  view can answer: does the command reach the indenter.

Eighty-three of a hundred and four. What is left is zoom, the paste variants,
whitespace, printing, encoding, the context menu, suggestions and the last
symbol commands - no group of them alike enough to do together.

## Zoom, and showing whitespace in one view without showing it in all of them

Four commands, and the two halves needed opposite things.

Zoom was already there for the wheel - `zoomBy()`, turned off by the
"zoom with the scroll wheel" setting. What the commands must not do is inherit
that guard: the setting is about the wheel, and Ctrl+= is not the wheel. So
they call the font settings directly and the control that says so puts the
guard back and fails the test.

Showing whitespace needed the opposite. The widget editor's entry changes the
display settings *of that editor* - reading a file with the spaces marked does
not mark them everywhere - and this view had no settings of its own, only the
global ones. So it gained a per-view answer the way wrapping already has one,
except that it falls back rather than defaulting: unset means the setting
answers, which is what keeps every view that was never told behaving as
before. Both halves have a control, and the fallback one fails the whitespace
test that was already there.

Eighty-seven of a hundred and four. What is left needs machinery of its own -
printing, encoding, the context menu, suggestions, the last symbol commands
and the paste variants - and is one or two at a time from here rather than
another sweep.

## Clean Whitespace, No-Format Paste, and asking for the context menu

Three that needed nothing built. Cleaning whitespace is the document's work
and the command only asks for it. The context menu already existed in the
form, opened by the pointer; asking from the keyboard opens the same one at
the caret, which is where a keyboard request means.

No-Format Paste is worth a sentence because it looks like a shortcut and is
not. The widget editor reformats what it pastes and this command turns that
off for one paste; this view has never reformatted a paste - it puts the
clipboard in as it stands - so pasting without formatting is what its ordinary
Paste already does. Registering the entry to `paste()` is the honest answer
rather than leaving the menu dead, and the comment says why rather than
leaving a reader to wonder whether the flag was forgotten.

The read-only control for cleaning whitespace did not bite at first: the
shared fixture had no trailing whitespace, so the command had nothing to do
whether or not it was guarded. That is the third time content has hidden a
missing guard, and the same fix each time - the fixture now ends a line with
two spaces.

Ninety of a hundred and four, and the fourteen left are named rather than
counted now:

- **Deliberately not ported.** `PRINT`. This branch is called
  utils-drop-printsupport; a view that has never needed QPrintSupport is not
  the place to bring it back.
- **Wants a piece that lives on the widget.** `CIRCULAR_PASTE` needs the
  clipboard history offered as a proposal - which this view can now show,
  since it has the quick fix path - but also `duplicateMimeData()`, a static
  on `TextEditorWidget`. That helper wants to be in Utils before this view
  calls it, which is a change to make deliberately rather than in passing.
- **Wants machinery.** the three `SUGGESTION_APPLY*`, `COPY_WITH_HTML`,
  `SELECT_ENCODING`, `SWITCH_UTF8BOM`, `GOTO`.
- **Language questions this view does not ask yet.** `FOLLOW_SYMBOL_TO_TYPE`
  and its split variant, `JUMP_TO_FILE_UNDER_CURSOR` and its split variant,
  `OPEN_TYPE_HIERARCHY`.

## The last of the language questions

Five commands, and the reading was worth more than the writing.

**Jump to File Under Cursor is Follow Symbol.** The widget editor gives them
separate menu entries and separate shortcuts and then sends both to
`openLinkUnderCursor()`. So the two entries are registered to the method this
view already had, and no code was written for either.

**Follow Symbol to Type is a different question**, and the first one this view
asks that answers *back*: where a symbol is used is told to somebody, where its
type is defined has to come back. So `SymbolRequests` gained a signal carrying
a `Utils::LinkHandler`, which is what the widget's own `requestTypeAt` carries,
and the language client answers it with `LinkTarget::SymbolTypeDef` exactly as
it answers the widget's. The control for it hands over an empty callback: the
request still goes, and the test still fails, because a question with nowhere
to send the answer is not the question.

**Open Type Hierarchy is not a question for the view at all.** It opens a pane
and tells it to look at whatever is current. It was dead here only because it
is registered per editor, so registering it per this editor too is the whole
change.

Ninety-five of a hundred and four. The nine left are `PRINT`, which this branch
exists to avoid; `CIRCULAR_PASTE`, waiting on a mime helper that should move to
Utils first; and `GOTO`, `SELECT_ENCODING`, `SWITCH_UTF8BOM`, `COPY_WITH_HTML`
and the three `SUGGESTION_APPLY*`, each of which wants something built.

## Circular Paste, and the line in it that made it a widget feature

The blocker was not what the last batch guessed. `duplicateMimeData()` being a
static on `TextEditorWidget` was awkward but movable. What actually stopped
this working anywhere but a widget was the end of
`ClipboardProposalItem::apply()`:

    if (auto widgetTarget = dynamic_cast<WidgetAssistTarget *>(&target))
        widgetTarget->widget()->paste();

An item offering a choice of things to paste could only paste into a widget,
so the clipboard history was a widget feature by accident rather than by
design. `AssistTarget` exists precisely so that an item need not ask what it is
writing into, and it was missing the one operation this item needed. It has
`paste()` now: the default puts the clipboard text in where the cursor is,
which is what a plain view can do, and `WidgetAssistTarget` overrides it with
the widget's own, which knows about rich text and column selections. The item
just calls `target.paste()`.

With that, circular paste is the same shape as everything else here: collect
what is on the clipboard, and if there is more than one thing in the history
offer it through the list quick fixes already use - the same question, after
all, of here are some things that could be done. `requestQuickFixes()` takes
the provider to ask, defaulting to the document's.

`duplicateMimeData()` and the column-selection mime type moved to
`circularclipboard.h` so there is one definition rather than a copy that would
quietly drop the column payload out of the history.

### What the CppEditor suite could and could not say

A new virtual on an exported base is a full rebuild, and it is also worth
asking whether anything else noticed. CppEditor exercises this machinery, so
it was run - and it cannot answer. On **unmodified HEAD** it gave 40 tests and
an ASan abort one run, and 1549 tests with 2 failures the next. Its totals are
not a signal in either direction.

`-test CppEditor,FollowSymbolTest` is stable where the whole suite is not:
155 passed twice on HEAD, and with these changes 155 passed in five runs of
six. The sixth failed on
`waitForRehighlightedSemanticDocument()` timing out, which is the rehighlight
flake this tree is already known for and has nothing to do with assist. So
there is no evidence of a regression, and it is worth being plain that this is
the strongest statement available rather than a clean bill.

## Go to Line, the encoding, and the byte order mark

Three, and only one of them needed anything written.

`Go to Line` asks the locator, not the editor - it shows a filter and the line
is typed there. `Select Encoding` was already implemented on the viewport,
complete with the reload and save the dialog's answer asks for; it had simply
never been registered, so the entry was dead over working code. Both were one
registration each.

Only the byte order mark needed a method, and it is one line over the document
plus the edit guard. Its control did not bite at first, and for a reason worth
recording: the read-only test compares the text before and after, and
switching the byte order mark changes **how the file is written and nothing
else**. Comparing the text could not see it happen either way. The test now
keeps `format().hasUtf8Bom` alongside the text, and removing the guard fails
it.

Ninety-nine of a hundred and four. What is left is `PRINT`, which this branch
exists to avoid, `COPY_WITH_HTML`, which needs the highlighted text turned
into HTML - the widget builds that in `createMimeDataFromSelection()` - and
the three `SUGGESTION_APPLY*`, which need inline suggestions, a feature this
view does not have at all rather than a command it has not been told about.

## Copy with HTML, and the end of the command list

The last portable one. Copying with the highlighting means building a second
document, filling it with the selected fragment, and then painting the
highlighter's formats over it - fifty-odd lines that were inside the widget's
`createMimeDataFromSelection()`. The formats come from the layout that drew
the text, which is why the extracted `htmlForSelection()` takes one rather
than reading the document alone; whether a line is shown at all becomes a
predicate, because the side-by-side diff editor overrides that question and
must keep its answer.

Moving it turned up three things that were only visible because the compiler
or a test said so, and each would have been a quiet behaviour change:

- The parameter, renamed to `cursor`, collided with the loop variable of the
  same name, so `for (const QTextCursor &cursor : cursor)` compiled.
- With that renaming, `if (!cursor.hasSelection()) continue;` - a check on
  *each* caret - silently became a check on the whole multi cursor.
- `const QTextLayout *layout = layout->blockLayout(current);` shadowed the
  parameter with itself.

The test failed too, and it was the test: it looked for `int value = 1` in the
HTML, and the whole point of the exercise is that those words are *not*
contiguous - each sits in a span of its own carrying its colour. Comparing the
tag-stripped text asks the right question, and the control that stops the
formats being applied fails it.

A hundred of a hundred and four. `PRINT` is out by design on this branch. The
three `SUGGESTION_APPLY*` are the only ones left that are simply absent: they
want inline suggestions - grey text the reader can accept a word at a time -
which this view does not draw, and `TextSuggestion::applyWord()` takes a
`TextEditorWidget *`, so the feature and the abstraction would both have to be
built. That is a piece of work, not a registration, and it is the honest end
of this list.

## Inline suggestions: the half that was an abstraction

The last three commands. Two batches ended by calling them "a feature, not a
registration", which was true and not the whole truth - the feature divides in
two, and only one half is a feature.

Taking a suggestion needed a widget for three things:
`widget->textCursor()`, `widget->document()` and `widget->insertSuggestion()`
when only part of it was taken. None of the three is a widget's, and naming
them is the same move that freed the clipboard history and the quick fixes.
`SuggestionTarget` says them; `TextSuggestion::applyWord()`, `applyLine()` and
`filterSuggestions()` take one; the widget and this view each have a small
adapter. `apply()` - the whole suggestion at once - never needed a view at all
and still does not.

So a suggestion can now be taken by a view that is not a widget, and there is
a test that does it: a suggestion is put on a block by hand, the caret is put
in it, and one word of `return value;` is taken. Three controls - taking the
whole thing instead of a word, not finding the suggestion at the caret, and a
target that will not say where the caret is - each fail it.

**The commands are still not registered, and should not be.** What is missing
is the other half: a suggestion is *drawn* by laying the block out from the
suggestion's replacement document rather than from its own text, and a
multi-line suggestion adds rows - which is the row-to-line mapping this view
spent several batches getting right. Until it draws them there is never a
suggestion to take, and an enabled menu entry that can do nothing is worse
than a grey one. The widget's own entries are `setEnabled(false)` until a
suggestion arrives, for the same reason.

A hundred of a hundred and four, unchanged - this batch moved a wall rather
than a number. What is left is `PRINT`, which this branch exists to avoid, and
drawing a suggestion, which is now the only thing standing between the other
three and their menu entries.

## Why the last three commands stop here

Last batch left drawing a suggestion as "the only thing standing between the
other three and their menu entries". That was wrong, and the reason is worth
recording before anyone picks this up expecting a display problem.

Nothing would ever give this view a suggestion to draw. Suggestions come from
one place - the Copilot plugin - and it is written against the widget
throughout: `BaseTextEditor::currentTextEditor()->editorWidget()`,
`scheduleRequest(TextEditorWidget *)`,
`connect(editor, &TextEditorWidget::cursorPositionChanged, ...)`, and both of
its state maps are keyed by `TextEditorWidget *`. Eighteen references across
three files.

So the drawing is the second thing needed, not the first. Making inline
suggestions work in this view is a port of Copilot's request scheduling to a
view it does not know about, and only then the row work - a block with a
suggestion is laid out from the suggestion's replacement document, and a
multi-line one adds rows, which is the row-to-line mapping this view keeps.
Two pieces of work in two plugins, neither of them a registration.

That is the honest end of the command list: a hundred of a hundred and four,
`PRINT` excluded by what this branch is for, and three behind a port nobody
has asked for.

## Where the whole effort stands

Every plugin these batches touched, run together:

    TextEditor       387 passed, 0 failed
    QuickUi           89 passed, 0 failed
    LanguageClient    20 passed, 0 failed
    DiffEditor        48 passed, 0 failed
    Git               62 passed, 0 failed
    Macros             6 passed, 0 failed
    FakeVim          253 passed, 2 failed

The two FakeVim failures are `test_vim_script_throwpoint` and
`test_vim_visual_selection_focus_out`, which are the same two this tree failed
before any of this work - baselined against HEAD when the Utils layout
counter went in, and unchanged since.

### Inline suggestions: the drawing

The Quick editor now draws a one-line suggestion on the line it would change,
which is the grey preview text a completion offers. `prepareSuggestion()` does
the two things the widget editor does when one arrives — hand the replacement
document this view's tab stops, and ask `updateSuggestionFormats()` for the
colours the scheme shows a suggestion in — and the layout pass substitutes the
suggestion's single row for the block's own text.

Two pieces of formatting have to be read, not one, and that was a real bug
caught by a test that had to be sharpened first. The grey is set as *character*
formats on the replacement document; the syntax highlighting carried over from
the real line is set on that document's *layout*. Reading only the layout's
ranges drew the suggestion in ordinary colours — correct text, no indication it
was not really there. The first attempt to assert this ("the row has some
formatting") was not enough: an unstyled row carries format ranges too. What
the test asks now is that the characters the reader typed and the characters
being offered come out in *different* colours, which is the whole of what tells
them apart.

A suggestion of several lines, or any suggestion in a wrapping view, is not
drawn at all: both need rows this view has not laid out, and showing the first
line of a multi-line suggestion would be a lie about where the rest lands.

Suggestions still have no producer in the Quick editor — Copilot, the only one,
is still widget-coupled — so the three apply commands remain unregistered. A
live menu entry that can do nothing is worse than a grey one.

### Inline suggestions: the offer

Drawing a suggestion is no use while nothing can offer one to this editor.
Copilot, the only thing that offers, took a `TextEditorWidget` everywhere: in
its two request hashes, in the callbacks it keeps, and in the three commands it
registers. None of what it asks of that widget is a widget's to answer - where
the caret is, whether the buffer is read only, whether a suggestion is already
showing, and somewhere to put the answer.

`SuggestionHost` names those, the way `SymbolRequests` names the questions that
need a language. It is the counterpart of `SuggestionTarget`: that one is what
*taking* a suggestion needs, this one is what *making* one needs. It is a
QObject because whoever offers has to know when the caret moved away from what
it asked about and when the view is gone; the handle is parented to the view,
so it dies with it. `suggestionHostForEditor()` hands out the same handle for
either editor, and Copilot no longer mentions a widget at all.

The widget half of that came out red, and not because of the port: with the
FakeVim plugin loaded and *switched off*, a freshly opened widget editor is
already blocked from showing suggestions. FakeVim makes a handler for every
text editor regardless, and the mode change reported as one opens takes the
hold that keeps suggestions out of the way outside insert mode. Switched off,
no further mode change arrives, so the hold is never released. The guard meant
to prevent this asks `inFakeVimMode()`, which means "a key is being processed"
and is true while switched off too. Fixed separately, with a test in FakeVim's
own suite.

Still missing before this is a feature: the three apply commands remain
unregistered, because reaching them needs the popup work the Quick editor does
not have yet.

### Inline suggestions: taking one

With something able to offer a suggestion and the view able to draw it, the
three commands that take one - Apply, Apply one Word, Apply Line - are
registered in the Quick editor's context. They are disabled until a suggestion
is showing, exactly as the widget editor keeps them: their shortcuts are Tab,
Shift+Tab and the next-word key, and an enabled shortcut is taken before the
key ever reaches the view, so a live entry would quietly stop Tab from
indenting.

That needed the half nobody had written yet: the view has to know *which* line
a suggestion is on. `currentSuggestion()` used to ask the block the caret was
in, which answers "no" as soon as the caret moves - while the drawing, which
asks each block for itself, went on showing the ghost text on the line the
caret had left. The view now keeps the block, and the suggestion is looked at
again whenever the caret moves or the text changes: kept while what is there
still leads to it, dropped when it does not.

Both re-checks earn their place, which one negative control had to be rewritten
to show. Typing seems to prove the content one, but typing moves the caret too,
so the caret path alone passed that test. The case only the content path
catches is text changing without this view's caret moving - another view
editing the same document, or an undo - and that is what the test does now.

Taking a suggestion in full does *not* end it: the line then reads what was
offered, so it still describes the text and stays. The widget editor does the
same, and the test says so rather than asserting a tidier rule that neither
editor follows.

### Escape

Escape dismisses a suggestion without taking it, before it means anything else
- the widget editor's order, and the reason for it is that a suggestion is the
one thing on screen the reader never asked for.

The test for it claims less than the first draft did. "Before the carets" needs
a state with both a suggestion and several carets, and there is none: moving to
a second caret leaves the line the suggestion is about, which ends it. The
control for the ordering half accordingly did not bite, and the assertion that
the caret had not moved could not fail either, so both are gone. What is left
says Escape reaches the suggestion, which is what was actually built.

### Snippets: getting the language back

Drawing the Snippets page with Qt Quick lost something that was never noticed
because nothing complained about it. `SnippetProvider::decorateEditor()` took a
`TextEditorWidget`, and it was the widget page that called it; once that page
was gone the function had no callers at all, and every group's decoration - a
better highlighter, an indenter, an auto-completer - was dead code. Editing a
C++ snippet in the Quick page indented like plain text.

Of the three things a decorator did, two are the document's (highlighter,
indenter) and one is the view's (completer). So the decorator now takes a
`TextDocument *`, and the completer is *made* through a separate creator rather
than installed: a document can be shown by more than one view, and each needs
its own. `CodeBuffer` gained a `snippetGroup` - the mime type says how the text
is coloured, the group says how it indents and completes - and `CodeSource`
gained `createAutoCompleter()`, which the viewport asks. It asks again on a new
`languageChanged` signal, because QML sets the group *after* it has bound the
view to the source.

The second test had to be rewritten because its premise was false. It was going
to show a language completer differing from the plain one by typing a quote
inside a comment; both leave it alone, because the plain completer reads the
highlighter's formats and the mime type already made the line a comment. What
the test says now is that the view takes the completer the source offers and
gives it back when the source stops offering one - the mechanism this change
adds. How a C++ completer differs from a plain one is CppEditor's own test.

### The same loss, one page earlier

The C++ Preprocessor Directives dialog lost the same thing and for the same
reason: the widget dialog ran `decorateCppEditor()` over its `SnippetEditorWidget`,
and porting it to `SnippetEditor.qml` dropped the call. It edits text that is
compiled, so it now names the C++ group and gets the language back.

Its test reads the binding out of the form's source rather than out of the
loaded form. CppEditor does not link Qt Quick - `aspectFormRenders()` exists
precisely so a plugin need not - so there is no way from there to ask what a
loaded form bound. What the test can say is that the form names the group and
that the group exists; that naming it has any effect is TextViewport's test.
Worth knowing which half is being checked where.

A sweep for other API left callerless by the migration - exported functions
taking a `TextEditorWidget *` whose only mentions are their own declaration and
definition - turned up nothing else.

### What the field used to remember

Sweeping the porting commits for dropped setup calls turned up `setIndenter`
and `resetSyntaxHighlighter` once - the snippet and preprocessor losses above -
and `setHistoryCompleter` twelve times. The twelve are a different shape: the
calls were not dropped, they *moved onto the aspect*, which looks right in a
diff. Only `aspectwidgetrenderer.cpp` ever read `historyCompleterKey()`, so on
a Quick page they said nothing at all. Make path, qmake path, debugger path,
CMake tool path, Gerrit host, run arguments and the rest each stopped offering
what had been typed there before, and nothing complained because nothing was
broken - the field simply had no memory.

The popup was already there: `CompletionPopup.qml` exists for
`StringAspect::completions()`. What was missing was the history behind it. So
`CompletionHistory` is the store without the QCompleter - `HistoryCompleter`
now reads and writes through it, which is what keeps a field remembering the
same things however it is drawn - and the two aspects that keep a history put
it at the front of what they offer.

Two details worth keeping:

`rememberValue()` is on `BaseAspect` as a no-op rather than only on the two
aspects that do it. `StringDelegate.qml` draws both strings and paths, and its
`aspect` is typed as `Aspect`; a method that exists only on the subclasses
would be a name QML resolves at runtime and qmllint cannot check, which is
exactly the class of mistake the enforced signatures are there to catch.

It records the *volatile* value, not the value. A settings page waits for
Apply, so the aspect still holds the old text when the field is finished with -
and recording that would remember the value the reader had just replaced. A
field remembers what was typed into it, whether or not the page is applied.

### Where Browse opens

The same comparison that found the history - what the widget renderer reads off
an aspect and QML never mentions - also turns up the path chooser's browse
start. On a Quick page the file dialog opened wherever the platform had last
been, rather than beside the path already in the field.

The rule is the widget path chooser's: the field's own contents, the file's
directory when it names a file, then whatever the aspect was told to start
from, then what its relative paths are relative to. It lives on the aspect as a
virtual asked with what the field holds, not in the presentation, because the
base directory is a `Lazy` that may be a project's: a form is described far
more often than it is browsed from, and describing one should not force that
value or ask the filesystem what is a directory.

Two crashes along the way were both the vtable trap: adding a virtual to
`BaseAspect`, or a field to `AspectPresentation`, and then building only the
three targets that were edited leaves every other plugin with the old layout.
The stack pointed at `Layouting`, which had nothing to do with it. Worth
remembering that the first crash was read as evidence that evaluating the
`Lazy` was the problem - it was not, and the design above is right for its own
reasons rather than for the one it was first justified by.

### A tooltip that was carried and never shown

The same list had `showToolTipOnLabel`: four aspects - the run configuration's
executable and three Axivion paths - ask for a label's tooltip to be *the value
it is showing*, because the row elides a long path and the tooltip is the rest
of it. The Quick label carried the aspect's tooltip text and set
`ToolTip.visible: false`, so it showed nothing at all, for any aspect.

It passed the existing sweep, which asks whether every delegate *carries* the
tooltip its aspect has. Carrying is not showing - the same shape as several
earlier findings here, where presence was checked and placement or behaviour
was not.

Two halves are fixed and one is asserted. The text - the value, when the aspect
asked, and otherwise what the aspect says about itself - is tested, and both
controls bite, including the one where the flag stops being copied into the map
the bridge hands QML. The visibility is now bound to a `HoverHandler`, which is
*not* asserted: a hover test on this machine answers to the physical pointer as
well as the synthetic one, and a test that fails when someone's mouse happens
to rest over the window is worse than a missing assertion. Said out loud
because "tested" should not be read as covering both.

Also closed with no work: `elideMode`. Thirty-eight calls, all but one on
`ElidingLabel` widgets in welcome pages rather than on aspects, and the one
that is an aspect asks for the elide the Quick label already does. A count of
call sites is not a measure of a gap.

### What version is that command?

Fifteen aspects call `setCommandVersionArguments()`. In a widget path chooser
that installs a tooltip filter: rest the pointer on a compiler, a debugger or a
cmake path and it runs the command and shows what it reports. On a Quick page
nothing ran.

`extendedToolTip()` and `requestExtendedToolTip()` follow the shape already
used for secrets - ask, and be told when the answer arrives - because the
answer costs a process and nobody wants it until they look. The field asks when
the pointer arrives, the aspect remembers which command it asked about so a
pointer crossing the field twice does not start it twice, and an aspect with no
version arguments never asks at all.

That last one took three attempts to test, and the first two passed while
proving nothing. With `/bin/echo` and no arguments the process prints an empty
line, so "said nothing" was true whether or not it ran. With `/bin/pwd` it
would have printed something - but the assertion ran immediately, before any
process could have answered, so it was still true either way. The absence is
only checkable against something that must happen later: the test now runs the
very command the aspect would have run, waits for *that*, and only then asks
whether the aspect said anything. Both earlier versions passed with the guard
deleted.

### Two things the descriptor said and no delegate read

The other half of the comparison: fields of `AspectPresentation` that no `.qml`
file and no part of the bridge mentions. Eight, of which six are sizing hints
and grid spans that a hand-written form has no use for. Two were real.

`fontFilters`: the terminal's font picker asks for monospaced families.
`Qt.fontFamilies()`, which the delegate used, has no such notion - only the
font database knows which family is fixed pitch - so the terminal offered every
font on the machine. The list now comes from the bridge, filtered.

`alphaAllowed`: this one was the other way round from how it looked. The
delegate has red, green and blue spin boxes and no alpha at all, so *no*
colour's alpha could be edited, and the two aspects that forbid one were right
by accident. There is an alpha now, shown when the aspect allows it.

Both needed the same second step, and the colour test caught it: a field added
to `AspectPresentation` is not visible to QML until it is also copied into the
map the bridge hands over. That is two places, and the compiler checks neither.
The label tooltip earlier in this session had the same miss.

### Suggestions of more than one line

A suggestion of several lines was drawn not at all, on the grounds that showing
the first line would be a lie about where the rest lands. The mechanism for
showing the rest already existed and was built for something else: the inline
diff's *ghost rows* - rows drawn between the file's own, in no document, that
nothing can be typed into and no click maps onto.

So a multi-line suggestion puts its first line on the line it would change and
the rest on ghost rows below, and the file's next line moves down rather than
being drawn over. The widget editor reserves pixels for this instead
(`replacementBlockBoundingRect()` feeding `blockHeight()`), which suits a view
that measures in pixels; this one measures in rows.

Ghost rows had to learn what they are. They were hard-coded to the diff's
removed-line colour, on a band of its own, and a suggestion drawn that way
reads as a deletion. The control for that did not bite at first because the
test only checked the ghost rows' *text* - so the view now reports what colour
they are written in, and the test says the rest of a suggestion is the colour
the offered part of the line above is in.

A wrapping view still shows none of it: it works out its own rows from its
width, so it has none to give.

### A file dialog that can see a device

`allowPathFromDevice` was the one item left needing a decision, because
`QtQuick.Dialogs`' `FileDialog` asks the platform and the platform knows only
the machine it runs on. The answer was: port the dialog, not reach for the
widget one.

It comes in three pieces, and only the first two are done.

`FileBrowser` is the browsing itself with no view: which directory is being
looked at, what is in it, the places to start from - this machine's usual ones
and every device that can be browsed - name filters, hidden files, and what a
typed name means. It sits on `Utils::FileSystemModel`, which already works in
`Utils::FilePath` and so reaches a device as readily as this machine. One thing
had to be learnt the hard way: a model like that hands out a directory's
contents only when someone asks, and in the widget dialog the *view* is what
asks. With no view, the browser has to call `fetchMore()` itself, and until it
did every directory looked empty.

`QtcFileDialog.qml` is the dialog over it - places, listing, path field, name
field, Open and Cancel. A `Window`, because a dialog inside a `QQuickWidget`
cannot be bigger than the page that opened it. The test drives what it *would*
choose rather than showing it: that is the part the platform dialog used to do,
so it is the part that has to be right.

The path field opens it when the path in it is on a device, and on Shift as
well so that a device can be reached from a path that is not on one yet -
otherwise the platform dialog, which is the widget path chooser's own rule.

What is not ported: the icon view, search, inline rename, favourites and
navigation history. The widget dialog keeps all of those and is untouched;
this one is the browsing half. Said plainly because "the file dialog is ported"
would not be true yet.
