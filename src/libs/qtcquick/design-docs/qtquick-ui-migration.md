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

## Status

Branch `utils-drop-printsupport`, 16 commits, not pushed. Phase 1 complete;
phase 2 in progress.

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

Pre-existing test failures on this checkout, unchanged by this work and confirmed
against the base commit: `tst_debugger_dumpers`, `tst_baseenginedebugclient`,
7 `AuxiliaryPropertyStorageView`, 3 `Model_Imports`, `McuModuleProjectItem`.

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
Measured by resolving the real include graph (`.h`/`.cpp`/`.mm`, excluding
3rdparty), counting a file as tainted if it reaches a QtWidgets or
QtPrintSupport header transitively:

| | tainted | clean |
|---|---|---|
| Start | 137 files / 68153 lines | 229 / 35973 |
| Now | 117 / 49319 | 258 / 55122 |

The clean side is now the larger of the two by line count.

The split is **not** a file sort. 15 headers are clean while their own
implementation needs widgets, and a class's header and implementation have to
land in the same library, so each of those is a small de-widgeting change that
has to happen first.

## Next steps

In order:

1. **Split `terminalcommand.cpp`**, moving the "Select Terminal Emulator" dialog
   to the widget side. Cheapest first step: it unpins `filepath.h`,
   `qtcprocess.h`, `environment.h` and `devicefileaccess.h` in one change. See
   "Removing Layouting" above for the measured chain.
2. **Invert `addToLayoutImpl` into `AspectPresentation`.** Started: the
   descriptor and `BaseAspect::presentation()` exist, with all 16 built-in
   aspects and 7 of ~30 plugin aspects reporting a control. What remains is the
   widget renderer that consumes it, after which `addToLayoutImpl` and its two
   dozen QtWidgets includes can leave `aspects.cpp`.
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
