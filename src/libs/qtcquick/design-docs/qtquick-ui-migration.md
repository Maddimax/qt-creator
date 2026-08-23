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

Branch `utils-drop-printsupport`, 16 + 29 commits, not pushed. Phase 1
complete; phase 2 in progress. The second batch adds: the validator-type
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

**One page that cannot be ported as it stands,** noted so nobody rediscovers
it: Testing's "Active Test Frameworks" is a `FrameworksAspect` with no
`presentation()`, so it is `Custom` and would vanish. (Display was the other
one, until `named()` above; it draws aspects from two containers.)

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
all`, minus `QmlDesigner` and `UpdateInfo`, see below): **73 aspect-driven
pages, 57 with their own QML and rendered with Qt Quick, 16 still on widgets.**
Before the gate was narrowed, 65 pages rendered generically; the delegate work
that made that possible is all still in place and is what the ports build on:
`StringListAspect` (a real list editor), `IntegersAspect` (`Invisible`, because
it draws nothing in the widget path either), `StringSelectionAspect` (it builds
its own choices now), index-valued selections with no options (an empty combo
box is what the widget editor draws too), and `AspectList`'s
list-with-details style.

All eight remaining pages are held by aspects whose control is `Custom`
because they build their own widget in `addToLayoutImpl`. Not all of them need
porting one by one, though: `EnvironmentChangesAspect` turned out to be a
summary plus one button, which is now the `TextWithAction` control, and
`BaseAspect` grew `displayText()` and `triggerAction()` for it. Look for that
shape before writing a bespoke delegate. What is left, from walking each
declined page:

| page | blocked by |
|---|---|
| Clang Tools | `ClangDiagnosticConfigIdAspect` - a combo plus a manage button |
| Font && Colors | the colour-scheme editor |
| Snippets | the snippets editor |
| QML/JS Editing | two unnamed `Custom` aspects |
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

In order:

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
