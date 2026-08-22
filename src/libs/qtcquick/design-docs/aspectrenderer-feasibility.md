# Can a generic renderer driven by presentation() replace addToLayoutImpl()?

> Status note, added after the study: structural problem (1) below - the undo
> protocol bypass - has since been fixed. setVolatileVariantValueFromGui()
> records the write on the aspect's UndoableValue and each aspect connects its
> own undo signal, so a QML delegate's edit is undoable with no widget present
> (see "Utils: Make a GUI-driven aspect write undoable"). Problem (2), the four
> aspects keeping GUI state in a widget pointer, still stands and remains the
> prerequisite for deleting addToLayoutImpl().

## Verdict

Mostly, but not cleanly. Eleven of the sixteen overrides are reproducible by a
control-keyed renderer once `AspectPresentation` grows a dozen plain-data
fields. Five are not: they configure widgets with *function-valued* state
(validators, display filters, macro expanders, async model fillers, an
arbitrary `layouter()` closure) that no descriptor can carry. Two structural
problems bite before any field work: (1) the undo protocol is **not** behind
`setVolatileValue()` — undo-recording writes happen only inside the widget
lambdas, so every current QML delegate silently bypasses `QUndoStack`; and
(2) four aspects (`FilePathAspect`, `IntegerAspect`, `DoubleAspect`,
`MultiSelectionAspect`) keep their GUI state *in a widget pointer inside the
aspect* — their `guiToVolatileValue()`/`volatileValueToGui()` dereference it
(aspects.cpp:1784-1815, 2918-2929, 3055-3066, 2829-2850) — so they must be
converted to the `UndoableValue` pattern before `addToLayoutImpl()` can be
deleted at all. Finally, composition (groups, rows, group-checkers) lives in
plugin `Layouting` code, not in the aspects; `presentation()` describes leaves
only, and no field on the struct fixes that.

## The sixteen overrides

| Aspect | control | Verdict | What a renderer still misses |
|---|---|---|---|
| BoolAspect (aspects.cpp:2420) | CheckBox/RadioButton | generic + additions | `labelPlacement` (4 modes incl. ShowTip sub-label, 2360-2388); undo write path |
| StringAspect (1314) | Label/LineEdit/TextEdit/Password | **bespoke** | validator fns, displayFilter, completers, macro expansion, right-side icon + signal, reset button, checker — see below |
| FilePathAspect (1830) | PathChooser | **bespoke** | ~15 PathChooser properties, validator fn, env, browse/terminal handlers; widget-held state; public `pathChooser()` |
| ColorAspect (2069) | ColorPicker | generic + additions | `alphaAllowed`, `withResetButton` (else press-and-hold menu reset), `minimumSize` |
| FontFamilyAspect (2147) | FontFamilyPicker | generic + additions | `fontFilters`; undo write path (2160) |
| SelectionAspect (2560) | ComboBox/RadioButtonGroup | generic + additions | per-option `tooltip`/`enabled` (2570-2572); `choices` must become a struct list |
| MultiSelectionAspect (2779) | MultiSelection | generic (after refactor) | gui-sync lives in `d->m_listView` (2829-2850); no undo at all today |
| IntegerAspect (2900) | SpinBox | generic + additions | `prefix`, `suffix`, `specialValueText`, `displayIntegerBase`, `displayScaleFactor` (displayed = volatile / factor!); widget-held state |
| DoubleAspect (3039) | DoubleSpinBox | generic + additions | `prefix`, `suffix`, `specialValueText`; widget-held state |
| StringListAspect (3237) | StringList/CommaSeparatedLineEdit | generic + additions | `allowAdding/Removing/Editing` (3277-3279); comma split-trim semantics duplicated per backend (3243-3249); per-item edit invokables (see kindOf comment, aspectcontainermodel.cpp:106) |
| FilePathListAspect (3473) | FilePathList | generic | only gap: `presentation()` forgets to fill the existing `placeholderText` field (3466) |
| IntegersAspect (3574) | IntegerList | generic | `addToLayoutImpl` is an empty TODO; `QList<int>`↔QVariantList conversion missing for QML writes |
| TextDisplay (3617) | Label | generic + additions | `infoType` (InfoLabel icon), `wordWrap`; `linkActivated` signal forwarding |
| AspectContainer (3729) | Container | **bespoke** | `parent.addItem(layouter()())` — an arbitrary closure; no child/group structure is visible to `presentation()` |
| StringSelectionAspect (4127) | ComboBox | **bespoke** | async `FillCallback` into `QStandardItemModel`, id != display text (UserRole+1), editable+completer, virtual `fixupComboBox()` widget hook |
| FontAspect (4270) | FontPicker | **bespoke** | is an AspectContainer (no `value` property, aspectcontainermodel.cpp:110); size combo repopulated from `QFontDatabase::pointSizes` on family change with closest-match logic |

Sizes: **generic 3** (FilePathList, Integers, MultiSelection-after-refactor),
**generic with additions 8** (Bool, Color, FontFamily, Selection, Integer,
Double, StringList, TextDisplay), **bespoke 5** (String¹, FilePath,
StringSelection, Font, AspectContainer). ¹StringAspect's Label and TextEdit
styles are near-generic; the FancyLineEdit style is what makes it bespoke.
BoolAspect's `groupChecker()` is a 17th rendering path with no override of its
own; see "Group checker" below.

## Hard case 1: undo is not behind setVolatileValue()

`UndoableValue::set(stack, v)` pushes a `QUndoCommand` (aspects.h:1288-1295);
`setWithoutUndo()` does not (aspects.h:1300). Only the widget signal handlers
call the pushing form — e.g. StringAspect aspects.cpp:1415, BoolAspect 2392,
SelectionAspect 2585/2606, StringList 3255, StringSelection 4203. The generic
write path `TypedAspect::setVolatileValue()` (aspects.h:392-402) calls
`volatileValueToGui()`, which is `setWithoutUndo` everywhere (e.g. 1557, 2471,
2620). The QML delegates write `aspect.value` (= `setVolatileVariantValue`,
aspects.h:77) and therefore never record undo. Consumers exist:
compilerexplorereditor.cpp:376, aspectlist.cpp:460. A renderer must stay
ignorant of `UndoableValue`, so BaseAspect needs one generic undo-recording
entry point (e.g. `setVolatileValueFromGui(QVariant)`) that each aspect routes
through its own undoable. Beware: several `UndoSignaller::changed` handlers
also call `handleGuiChanged()` with the *widget* as context (2394-2397), so
today undo/redo only reaches `m_volatileValue` if a widget exists — that leg
must move into the aspect.

## Hard case 2: group checker and composition

`BoolAspect::groupChecker()` (2429-2446) returns a closure that makes a
`QGroupBox` checkable and wires clicked ↔ undoable. It is consumed by plugin
layouters (`Group { title(...), groupChecker(...), ... }`, e.g.
gitsettings.cpp:173-181, ~11 files). The declarative equivalent is a
*container-level* fact: "group G is checked-by aspect B". But groups are not
aspects — they exist only inside `AspectContainer::layouter()` closures, so
there is nothing for `presentation()` to describe. The honest options: give
AspectContainer a real child/group tree (each group node carrying `title` and
`checkedBy: BaseAspect*`) and port every plugin layouter, or keep containers
bespoke per backend. The aspect-to-aspect `setEnabler()` (aspects.cpp:414)
already covers "bool disables other aspects" backend-neutrally;
`makeCheckable()` (CheckableAspectImplementation, aspects.cpp:932-1030) is the
same idea per-field and would map to `checkedBy` + `checkBoxPlacement` +
`uncheckedSemantics` fields.

## Hard case 3: function-valued configuration

StringAspect/FilePathAspect carry `std::optional<ValidationFunction>`,
`m_displayFilter`, `m_validatorFactory`, completers, macro expanders
(aspects.cpp:1328, 1359-1362, 850-861), and StringSelectionAspect carries
`m_fillCallback` and virtual `fixupComboBox()` (aspects.h:1326-1347). None can
live in a value struct. The workable pattern: the descriptor names the control;
the delegate (on either backend) asks the aspect for these services through a
narrow interface — i.e. these aspects get their own delegate on both backends.

## Where the label decision lives

In the aspect. `createLabel()` (aspects.cpp:283) decides *whether* a label
exists (none when `m_labelText` and pixmap are empty) and owns its behaviour
(selectable text, `labelLinkActivated`, live text/pixmap updates);
`addLabeledItem()` (307) only places it and spans the field by `m_spanX - 1`.
So `labelText` in the presentation already carries the decision; missing are
`labelPixmap`, `span`, and BoolAspect's `labelPlacement`.

## Fields AspectPresentation needs, in dependency order

1. `labelPlacement` + `span` + `labelPixmap` — every form row depends on them.
2. Fill `placeholderText` in `FilePathListAspect::presentation()` (field exists).
3. Numeric: `prefix`, `suffix`, `specialValueText`, `displayIntegerBase`, `displayScaleFactor`.
4. `choices` becomes `QList<Choice{display, toolTip, enabled, id}>` (breaking change to the existing QStringList; do before more users appear).
5. List editing: `allowAdding`, `allowRemoving`, `allowEditing`.
6. `infoType`, `wordWrap` (TextDisplay); `alphaAllowed`, `withResetButton` (Color); `fontFilters` (FontFamily).
7. Checkable: `checkedBy` (aspect reference), `checkBoxPlacement`, `uncheckedSemantics` — requires deciding how a descriptor references another aspect.
8. Container children/groups — not a field; a structural change replacing `layouter()`.

Prerequisite refactors (before deleting `addToLayoutImpl`): the generic
undo-recording write path (hard case 1); converting FilePath/Integer/Double/
MultiSelection gui-sync off widget pointers; note `QTC_CHECK(!d->m_spinBox)`
(2903) means those aspects support only one live widget today.
`aspectForWidget()`/`registerSubWidget()`'s back-pointer has no consumers
outside aspects.cpp (repo grep), so it can die with the widget path; its
enabled/visible/tooltip/readOnly propagation is already replicated by QML
property bindings (BoolDelegate.qml:20-21).

Existing assertions: tst_aspects.cpp:94-165 pins control choice per display
style and bounds; :169-207 requires every built-in aspect to report a
non-Custom control. Nothing yet asserts choices metadata, undo behaviour, or
round-trips through `setVolatileVariantValue`.
