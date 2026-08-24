// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "texteditor_global.h"

#include <utils/aspects.h>
#include <utils/guard.h>
#include <utils/id.h>

#include <QPointer>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

namespace Utils { class FilePath; }

namespace TextEditor {
class CodeStylePool;
class ICodeStylePreferences;
class ICodeStylePreferencesFactory;
class SnippetEditorWidget;

// Base for a self-managed code style value editor whose deferred state lives
// outside the ICodeStylePreferences (e.g. ClangFormat's .clang-format files and
// global settings). When a value editor derives from this, the hosting
// CodeStyleAspect routes apply/cancel/isDirty to it and listens to changed(),
// and leaves it to lay out its own selector and preview. Plain value editors
// that only edit the preferences do not need it.
class TEXTEDITOR_EXPORT CodeStyleEditor : public QWidget
{
    Q_OBJECT

public:
    using QWidget::QWidget;

    virtual void apply();
    virtual void cancel();
    virtual bool isDirty() const;

signals:
    void changed();
};

// Creates a snippet preview editor bound to codeStyle: decorated with the
// factory's snippet group, filled with its preview text, and re-indented live
// by the factory's indenter as the style changes. Pass a project file for a
// per-project preview, or an empty path for the global one.
TEXTEDITOR_EXPORT SnippetEditorWidget *createCodeStylePreview(
    const ICodeStylePreferencesFactory *factory,
    const Utils::FilePath &projectFile,
    ICodeStylePreferences *codeStyle,
    QWidget *parent = nullptr);

// The standard explanatory note shown beneath a code style preview.
TEXTEDITOR_EXPORT QLabel *createCodeStylePreviewNote();

// The "take effect immediately" hint, for the per-project code style pages
// (which apply live). Global pages defer to Apply/OK and must not use it.
TEXTEDITOR_EXPORT QWidget *createTakeEffectImmediatelyLabel();

// What a code style selector offers besides picking one, factored out so that
// the widget selector and the Qt Quick one do the same thing. Each puts up its
// own dialog, parented to \a dialogParent.
namespace CodeStyleActions {
TEXTEDITOR_EXPORT void copy(ICodeStylePreferences *codeStyle, QWidget *dialogParent);
TEXTEDITOR_EXPORT void remove(ICodeStylePreferences *codeStyle, QWidget *dialogParent);
TEXTEDITOR_EXPORT void importFrom(ICodeStylePreferences *codeStyle, QWidget *dialogParent);
TEXTEDITOR_EXPORT void exportTo(ICodeStylePreferences *codeStyle, QWidget *dialogParent);
} // namespace CodeStyleActions

// How a code style reads in a selector: its name, plus what it delegates to and
// whether it can be edited.
TEXTEDITOR_EXPORT QString codeStyleDisplayName(const ICodeStylePreferences *codeStyle);

// The preview shown beside a Code Style form. Its value is the code, so that
// editing it is an ordinary aspect edit; the rest is what a Qt Quick preview
// needs in order to colour and re-indent that code, which QML cannot get at
// otherwise - it only ever sees a page's aspects. See CodeStylePreview.qml.
class TEXTEDITOR_EXPORT CodeStylePreviewAspect : public Utils::StringAspect
{
    Q_OBJECT

    // The preferences the preview is a view of: the page's own copy, not the
    // saved style, so that it shows the edits being made.
    Q_PROPERTY(QObject *codeStyle READ codeStyleObject CONSTANT)
    // Which language's indenter re-indents it.
    Q_PROPERTY(QString languageId READ languageIdString CONSTANT)
    // What the code is, for looking up a highlight definition.
    Q_PROPERTY(QString mimeType READ mimeType CONSTANT)

public:
    CodeStylePreviewAspect(Utils::AspectContainer *container,
                           const ICodeStylePreferencesFactory *factory,
                           ICodeStylePreferences *codeStyle);

    QObject *codeStyleObject() const;
    QString languageIdString() const;
    QString mimeType() const;

    // Puts the factory's preview text back, discarding whatever was typed.
    void resetText();
    // Runs the language's own formatter over it, where it has one. Where it has
    // not, the indenter is the formatting, and only a re-indent is asked for.
    void formatText();

signals:
    // The text was not changed but should be laid out again: nothing else can
    // ask the QML side's indenter to re-run.
    void reindentRequested();

private:
    ICodeStylePreferences *m_codeStyle = nullptr;
    const ICodeStylePreferencesFactory *m_factory = nullptr;
    QString m_languageId;
    QString m_mimeType;
};

// Reusable settings-page container for editing an ICodeStylePreferences with
// deferred apply/cancel. A page-local copy of the style is its volatile state;
// the language's code style editor edits that copy, and apply() commits it to
// the real style. Lets a code style page be a plain setSettingsProvider().
class TEXTEDITOR_EXPORT CodeStyleAspect : public Utils::AspectContainer
{
public:
    CodeStyleAspect(ICodeStylePreferences *codeStyle, Utils::Id languageId);
    ~CodeStyleAspect() override;

    void apply() override;
    void cancel() override;
    bool isDirty() const override;

private:
    void ensurePageCopy(ICodeStylePreferencesFactory *factory);
    void setupSelectorAspects(const ICodeStylePreferencesFactory *factory);
    void refillStyleOptions();
    void updateSelectorState();
    void syncFromReal();
    bool poolsDiffer() const;
    ICodeStylePreferences *addPageCopy(ICodeStylePreferences *realStyle);

    ICodeStylePreferences *m_codeStyle = nullptr;
    Utils::Id m_languageId;
    CodeStylePool *m_pagePool = nullptr;
    ICodeStylePreferences *m_pageCodeStyle = nullptr;
    QPointer<CodeStyleEditor> m_editor;
    bool m_syncing = false;

    // The selector, as aspects, for a language that draws its page with Qt
    // Quick. Null for one that still uses CodeStyleSelectorWidget.
    Utils::SelectionAspect *m_styleSelection = nullptr;
    Utils::ActionAspect *m_copyStyle = nullptr;
    Utils::ActionAspect *m_removeStyle = nullptr;
    Utils::ActionAspect *m_importStyle = nullptr;
    Utils::ActionAspect *m_exportStyle = nullptr;
    Utils::TextDisplay *m_readOnlyNote = nullptr;
    // What each option in m_styleSelection stands for, in the same order.
    QList<QPointer<ICodeStylePreferences>> m_selectableStyles;
    Utils::Guard m_updatingSelector;
};

} // namespace TextEditor
