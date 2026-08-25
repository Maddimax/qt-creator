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

**What this does not do.** The virtual has to stay: twenty aspects in
`src/plugins` and five more in Utils still override it. They are not one
group. Most are run-configuration, build-step and kit aspects - project
panels rather than preferences pages, which is a surface the migration has not
reached. The rest are aspects on pages that *have* been migrated and
describe a control the widget renderer had no case for, so they kept a closure
to draw it: `MimeTypesAspect`, `SuppressionAspect`, `EncodingSelectionAspect`,
`ClangDiagnosticConfigIdAspect`. `SecretAspect` was the fifth - see below.
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
