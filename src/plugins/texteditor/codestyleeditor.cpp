// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "codestyleeditor.h"

#include "codestyleselectorwidget.h"
#include "codestylepool.h"
#include "displaysettings.h"
#include "icodestylepreferences.h"
#include "icodestylepreferencesfactory.h"
#include "indenter.h"
#include "snippets/snippeteditor.h"
#include "snippets/snippetprovider.h"
#include "textdocument.h"
#include "texteditortr.h"

#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>
#include <utils/aspectwidgets.h>
#include <utils/filepath.h>
#include <utils/guiutils.h>
#include <utils/infolabel.h>
#include <utils/layoutbuilder.h>

#include <utils/filedialogs.h>
#include <utils/fileutils.h>

#include <QChar>
#include <QFont>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTextBlock>

using namespace Utils;

namespace TextEditor {

namespace CodeStyleActions {

void copy(ICodeStylePreferences *codeStyle, QWidget *dialogParent)
{
    QTC_ASSERT(codeStyle, return);
    CodeStylePool *pool = codeStyle->delegatingPool();
    QTC_ASSERT(pool, return);

    ICodeStylePreferences *current = codeStyle->currentPreferences();
    bool ok = false;
    const QString newName = QInputDialog::getText(
        dialogParent,
        Tr::tr("Copy Code Style"),
        Tr::tr("Code style name:"),
        QLineEdit::Normal,
        Tr::tr("%1 (Copy)").arg(current->displayName()),
        &ok);
    if (!ok || newName.trimmed().isEmpty())
        return;

    if (ICodeStylePreferences *copy = pool->cloneCodeStyle(current)) {
        copy->setDisplayName(newName);
        emit codeStyle->aboutToBeCopied(current, copy);
        codeStyle->setCurrentDelegate(copy);
    }
}

void remove(ICodeStylePreferences *codeStyle, QWidget *dialogParent)
{
    QTC_ASSERT(codeStyle, return);
    CodeStylePool *pool = codeStyle->delegatingPool();
    QTC_ASSERT(pool, return);

    QMessageBox messageBox(
        QMessageBox::Warning,
        Tr::tr("Delete Code Style"),
        Tr::tr("Are you sure you want to delete this code style permanently?"),
        QMessageBox::Discard | QMessageBox::Cancel,
        dialogParent);

    // Change the text and role of the discard button
    auto deleteButton = static_cast<QPushButton *>(messageBox.button(QMessageBox::Discard));
    deleteButton->setText(Tr::tr("Delete"));
    messageBox.addButton(deleteButton, QMessageBox::AcceptRole);
    messageBox.setDefaultButton(deleteButton);

    QObject::connect(deleteButton, &QAbstractButton::clicked, &messageBox, &QDialog::accept);
    if (messageBox.exec() == QDialog::Accepted)
        pool->removeCodeStyle(codeStyle->currentPreferences());
}

void importFrom(ICodeStylePreferences *codeStyle, QWidget *dialogParent)
{
    QTC_ASSERT(codeStyle, return);
    CodeStylePool *pool = codeStyle->delegatingPool();
    QTC_ASSERT(pool, return);

    const FilePath filePath = FileUtils::getOpenFilePath(
        Tr::tr("Import Code Style"), {}, Tr::tr("Code styles (*.xml);;All files (*)"));
    if (filePath.isEmpty())
        return;

    if (ICodeStylePreferences *imported = pool->importCodeStyle(filePath)) {
        codeStyle->setCurrentDelegate(imported);
    } else {
        QMessageBox::warning(
            dialogParent,
            Tr::tr("Import Code Style"),
            Tr::tr("Cannot import code style from \"%1\".").arg(filePath.toUserOutput()));
    }
}

void exportTo(ICodeStylePreferences *codeStyle, QWidget *dialogParent)
{
    Q_UNUSED(dialogParent)
    QTC_ASSERT(codeStyle, return);
    CodeStylePool *pool = codeStyle->delegatingPool();
    QTC_ASSERT(pool, return);

    ICodeStylePreferences *current = codeStyle->currentPreferences();
    const FilePath filePath = FileUtils::getSaveFilePath(
        Tr::tr("Export Code Style"),
        FileUtils::homePath().pathAppended(current->displayName() + ".xml"),
        Tr::tr("Code styles (*.xml);;All files (*)"));
    if (!filePath.isEmpty())
        pool->exportCodeStyle(filePath, current);
}

} // namespace CodeStyleActions

QString codeStyleDisplayName(const ICodeStylePreferences *codeStyle)
{
    QTC_ASSERT(codeStyle, return {});
    QString name = codeStyle->displayName();
    if (const ICodeStylePreferences *delegate = codeStyle->currentDelegate())
        name = Tr::tr("%1 [proxy: %2]").arg(name, delegate->displayName());
    if (codeStyle->isReadOnly())
        name = Tr::tr("%1 [built-in]").arg(name);
    else
        name = Tr::tr("%1 [customizable]").arg(name);
    return name;
}

void CodeStyleEditor::apply() {}

void CodeStyleEditor::cancel() {}

bool CodeStyleEditor::isDirty() const
{
    return false;
}

// The default per-project code style editor: a style selector above a live
// preview. Changes apply immediately to the project's code style, so there is
// no apply/cancel here.
class CodeStyleProjectPreviewEditor final : public QWidget
{
public:
    CodeStyleProjectPreviewEditor(
        const ICodeStylePreferencesFactory *factory,
        const FilePath &projectFile,
        ICodeStylePreferences *codeStyle)
        : m_selector{projectFile, this}
    {
        m_selector.setCodeStyle(codeStyle);

        using namespace Layouting;
        Column {
            &m_selector,
            createTakeEffectImmediatelyLabel(),
            createCodeStylePreview(factory, projectFile, codeStyle),
            createCodeStylePreviewNote(),
            noMargin,
        }.attachTo(this);
    }

private:
    CodeStyleSelectorWidget m_selector;
};

QWidget *ICodeStylePreferencesFactory::createProjectEditor(
    const FilePath &projectFile, ICodeStylePreferences *codeStyle) const
{
    if (m_projectEditorCreator)
        return m_projectEditorCreator(projectFile, codeStyle);
    return new CodeStyleProjectPreviewEditor{this, projectFile, codeStyle};
}

SnippetEditorWidget *createCodeStylePreview(const ICodeStylePreferencesFactory *factory,
                                            const FilePath &projectFile,
                                            ICodeStylePreferences *codeStyle,
                                            QWidget *parent)
{
    auto preview = new SnippetEditorWidget(parent);
    DisplaySettingsData displaySettings = preview->displaySettings();
    displaySettings.m_visualizeWhitespace = true;
    preview->setDisplaySettings(displaySettings);
    SnippetProvider::decorateEditor(preview, factory->snippetGroupId());
    preview->setPlainText(factory->previewText());

    Indenter *indenter = factory->createIndenter(preview->document());
    indenter->setOverriddenPreferences(codeStyle);
    const FilePath fileName = !projectFile.isEmpty()
        ? projectFile.pathAppended("snippet.cpp")
        : Core::ICore::userResourcePath("snippet.cpp");
    indenter->setFileName(fileName);
    preview->textDocument()->setIndenter(indenter);

    const auto updatePreview = [preview, codeStyle] {
        QTextDocument *doc = preview->document();
        preview->textDocument()->indenter()->invalidateCache();
        QTextBlock block = doc->firstBlock();
        QTextCursor tc = preview->textCursor();
        tc.beginEditBlock();
        while (block.isValid()) {
            preview->textDocument()
                ->indenter()
                ->indentBlock(block, QChar::Null, codeStyle->currentTabSettings());
            block = block.next();
        }
        tc.endEditBlock();
    };

    QObject::connect(codeStyle, &ICodeStylePreferences::currentTabSettingsChanged,
                     preview, updatePreview);
    QObject::connect(codeStyle, &ICodeStylePreferences::currentValueChanged,
                     preview, updatePreview);
    QObject::connect(codeStyle, &ICodeStylePreferences::currentPreferencesChanged,
                     preview, updatePreview);
    updatePreview();

    return preview;
}

QLabel *createCodeStylePreviewNote()
{
    auto label = new QLabel(
        Tr::tr("Edit preview contents to see how the current settings "
               "are applied to custom code snippets. Changes in the preview "
               "do not affect the current settings."));
    QFont font = label->font();
    font.setItalic(true);
    label->setFont(font);
    label->setWordWrap(true);
    return label;
}

QWidget *createTakeEffectImmediatelyLabel()
{
    auto infoLabel = new InfoLabel(Tr::tr("All changes below take effect immediately."),
                                   InfoLabelType::Information);
    infoLabel->setFilled(true);

    // Wrap in a plain container so callers indent the container, not the label
    // itself (InfoLabel uses its own contentsMargins to place its icon).
    using namespace Layouting;
    return Column { infoLabel, noMargin }.emerge();
}

CodeStylePreviewAspect::CodeStylePreviewAspect(AspectContainer *container,
                                               const ICodeStylePreferencesFactory *factory,
                                               ICodeStylePreferences *codeStyle)
    : StringAspect(container)
    , m_codeStyle(codeStyle)
    , m_factory(factory)
    , m_languageId(factory->languageId().toString())
    , m_mimeType(SnippetProvider::mimeTypeForGroup(factory->snippetGroupId()))
{
    setQmlName("Preview");
    setDisplayStyle(TextEditDisplay);
    setDefaultValue(factory->previewText());
    setValue(factory->previewText());
    setLabelText(Tr::tr("Preview"));
}

QObject *CodeStylePreviewAspect::codeStyleObject() const
{
    return m_codeStyle;
}

QString CodeStylePreviewAspect::languageIdString() const
{
    return m_languageId;
}

QString CodeStylePreviewAspect::mimeType() const
{
    return m_mimeType;
}

void CodeStylePreviewAspect::resetText()
{
    setValue(defaultValue());
    emit reindentRequested();
}

void CodeStylePreviewAspect::formatText()
{
    const ICodeStylePreferencesFactory::PreviewFormatter formatter = m_factory->previewFormatter();
    if (!formatter) {
        emit reindentRequested();
        return;
    }

    const Utils::Result<QString> formatted = formatter(m_codeStyle, volatileValue());
    if (!formatted) {
        Core::MessageManager::writeFlashing(formatted.error());
        return;
    }
    setValue(*formatted);
}

CodeStyleAspect::CodeStyleAspect(ICodeStylePreferences *codeStyle, Id languageId)
    : m_codeStyle(codeStyle)
    , m_languageId(languageId)
{
    // The page is per language, so a language moves to Qt Quick on its own: it
    // names a form and this page renders it. The rest keep the widget editor
    // below until they do.
    if (ICodeStylePreferencesFactory *factory = codeStyleFactory(m_languageId)) {
        if (!factory->qmlSource().isEmpty()) {
            ensurePageCopy(factory);
            syncFromReal();
            // Which style is being edited, and what it does to code: the page's
            // own aspects, so that every language's form gets the same ones.
            setupSelectorAspects(factory);
            auto preview = new CodeStylePreviewAspect(this, factory, m_pageCodeStyle);

            auto resetPreview = new ActionAspect(this);
            resetPreview->setQmlName("ResetPreview");
            resetPreview->setActionText(Tr::tr("Reset to Original Preview Text"));
            resetPreview->setAction([preview] { preview->resetText(); });

            auto formatPreview = new ActionAspect(this);
            formatPreview->setQmlName("FormatPreview");
            formatPreview->setActionText(Tr::tr("Format Current Preview Text"));
            formatPreview->setAction([preview] { preview->formatText(); });
            // The form names aspects, and the page knows none of this
            // language's - the factory hands them over, editing the page-local
            // copy so that Cancel still means something. The page names the
            // container, not the language: every form reaches its own settings
            // as AspectModels.named(aspects.Settings).
            if (AspectContainer *settings = factory->createSettingsAspects(m_pageCodeStyle)) {
                settings->setQmlName("Settings");
                registerAspect(settings, /*takeOwnership=*/true);
            }
            setQmlSource(factory->qmlSource());
        }
    }

    Utils::AspectWidgets::setLayouter(this, [this] {
        ICodeStylePreferencesFactory *factory = codeStyleFactory(m_languageId);
        ensurePageCopy(factory);
        syncFromReal();

        using namespace Layouting;

        QWidget *valueEditor = factory->createValueEditor(m_pageCodeStyle);

        // A self-managed editor lays out its own selector and manages its own
        // deferred apply/cancel (e.g. ClangFormat, whose settings live outside
        // the preferences). Route its contract and show it as it is.
        if (auto selfManaged = qobject_cast<CodeStyleEditor *>(valueEditor)) {
            m_editor = selfManaged;
            connect(m_editor, &CodeStyleEditor::changed,
                    this, [this] { emit volatileValueChanged(); });
            return Column { valueEditor };
        }

        // A plain value editor edits the page-local copy live; build the common
        // selector and preview around it and let this aspect own the deferral.
        auto selector = new CodeStyleSelectorWidget({});
        selector->setCodeStyle(m_pageCodeStyle);
        Utils::installMarkSettingsDirtyTriggerRecursively(selector);

        if (factory->valueEditorHasPreview())
            return Column { selector, valueEditor };
        return Column {
            selector,
            Row {
                Column { valueEditor, st },
                Column {
                    createCodeStylePreview(factory, {}, m_pageCodeStyle),
                    createCodeStylePreviewNote(),
                },
            },
        };
    });
}

void CodeStyleAspect::ensurePageCopy(ICodeStylePreferencesFactory *factory)
{
    if (m_pagePool)
        return;

    // A transient, page-local pool holding editable copies of the real pool's
    // styles, so that selecting, editing, adding and removing styles on the
    // page is all deferred until apply().
    m_pagePool = new CodeStylePool(factory);
    m_pagePool->setTransient(true);

    m_pageCodeStyle = factory->createCodeStyle();
    m_pageCodeStyle->setDelegatingPool(m_pagePool);
    // Share the real style's id so it cannot be picked as its own delegate.
    m_pageCodeStyle->setId(m_codeStyle->id());

    // Any live edit through the value editor or the selector lands in the page
    // copy; reflect it in this aspect's dirtiness. Guarded so the mirroring
    // done by syncFromReal() does not count as a user edit.
    const auto notify = [this] {
        if (!m_syncing)
            emit volatileValueChanged();
    };
    connect(m_pageCodeStyle, &ICodeStylePreferences::currentDelegateChanged, this, notify);
    connect(m_pageCodeStyle, &ICodeStylePreferences::currentValueChanged, this, notify);
    connect(m_pageCodeStyle, &ICodeStylePreferences::currentTabSettingsChanged, this, notify);
    connect(m_pageCodeStyle, &ICodeStylePreferences::currentPreferencesChanged, this, notify);
}

void CodeStyleAspect::setupSelectorAspects(const ICodeStylePreferencesFactory *factory)
{
    Q_UNUSED(factory)

    m_styleSelection = new SelectionAspect(this);
    m_styleSelection->setQmlName("Style");
    m_styleSelection->setLabelText(Tr::tr("Custom settings:"));
    m_styleSelection->setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    connect(m_styleSelection, &BaseAspect::volatileValueChanged, this, [this] {
        if (m_updatingSelector.isLocked())
            return;
        const int index = m_styleSelection->volatileValue();
        if (index < 0 || index >= m_selectableStyles.size())
            return;
        m_pageCodeStyle->setCurrentDelegate(m_selectableStyles.at(index));
    });

    const auto addAction = [this](const QString &qmlName, const QString &text,
                                  const std::function<void()> &action) {
        auto aspect = new ActionAspect(this);
        aspect->setQmlName(qmlName);
        aspect->setActionText(text);
        aspect->setAction(action);
        return aspect;
    };

    QWidget *dialogParent = Core::ICore::dialogParent();
    m_copyStyle = addAction("CopyStyle", Tr::tr("Copy..."), [this, dialogParent] {
        CodeStyleActions::copy(m_pageCodeStyle, dialogParent);
    });
    m_removeStyle = addAction("RemoveStyle", Tr::tr("Remove"), [this, dialogParent] {
        CodeStyleActions::remove(m_pageCodeStyle, dialogParent);
    });
    m_exportStyle = addAction("ExportStyle", Tr::tr("Export..."), [this, dialogParent] {
        CodeStyleActions::exportTo(m_pageCodeStyle, dialogParent);
    });
    m_importStyle = addAction("ImportStyle", Tr::tr("Import..."), [this, dialogParent] {
        CodeStyleActions::importFrom(m_pageCodeStyle, dialogParent);
    });

    m_readOnlyNote = new TextDisplay(
        this,
        Tr::tr("The selected configuration is read-only. Copy the configuration for editing."));
    m_readOnlyNote->setQmlName("ReadOnlyNote");
    m_readOnlyNote->setIconType(InfoType::Warning);

    // Import and export need somewhere to put a style and somewhere to take one
    // from; without a pool there is neither.
    const bool hasPool = m_pageCodeStyle->delegatingPool() != nullptr;
    m_importStyle->setEnabled(hasPool);
    m_exportStyle->setEnabled(hasPool);

    if (CodeStylePool *pool = m_pageCodeStyle->delegatingPool()) {
        const auto refill = [this] { refillStyleOptions(); updateSelectorState(); };
        connect(pool, &CodeStylePool::codeStyleAdded, this, refill);
        connect(pool, &CodeStylePool::codeStyleRemoved, this, refill);
    }
    connect(m_pageCodeStyle, &ICodeStylePreferences::currentDelegateChanged, this, [this] {
        refillStyleOptions();
        updateSelectorState();
    });

    refillStyleOptions();
    updateSelectorState();
}

void CodeStyleAspect::refillStyleOptions()
{
    if (!m_styleSelection)
        return;

    // Setting the index below is this code catching the combo box up, not the
    // user picking a style, so it must not be routed back into the delegate.
    const GuardLocker locker(m_updatingSelector);

    m_styleSelection->clearOptions();
    m_selectableStyles.clear();

    if (CodeStylePool *pool = m_pageCodeStyle->delegatingPool()) {
        const QList<ICodeStylePreferences *> styles = pool->codeStyles();
        for (ICodeStylePreferences *style : styles) {
            // A style cannot delegate to itself, and the page is global, so
            // styles belonging to a project are not on offer either.
            if (style == m_pageCodeStyle || style->id() == m_pageCodeStyle->id())
                continue;
            if (!style->project().isEmpty())
                continue;
            m_selectableStyles.append(style);
            m_styleSelection->addOption(codeStyleDisplayName(style));
        }
    }

    m_styleSelection->setValue(m_selectableStyles.indexOf(m_pageCodeStyle->currentDelegate()));
}

void CodeStyleAspect::updateSelectorState()
{
    if (!m_styleSelection)
        return;

    const ICodeStylePreferences *delegate = m_pageCodeStyle->currentDelegate();
    // A built-in style cannot be deleted, and neither can one that is itself a
    // proxy for another.
    m_removeStyle->setEnabled(delegate && !delegate->isReadOnly() && !delegate->currentDelegate());
    m_readOnlyNote->setVisible(delegate && delegate->isReadOnly());
}

CodeStyleAspect::~CodeStyleAspect()
{
    delete m_editor;
    delete m_pageCodeStyle;
    delete m_pagePool;
}

ICodeStylePreferences *CodeStyleAspect::addPageCopy(ICodeStylePreferences *realStyle)
{
    ICodeStylePreferences *copy = codeStyleFactory(m_languageId)->createCodeStyle();
    copy->setId(realStyle->id());
    // Set read-only before adding so the pool files it as built-in vs. custom.
    copy->setReadOnly(realStyle->isReadOnly());
    copy->setDisplayName(realStyle->displayName());
    copy->setValue(realStyle->value());
    copy->setTabSettings(realStyle->tabSettings());
    m_pagePool->addCodeStyle(copy);
    // addCodeStyle() does not take ownership, so parent the copy to the pool.
    copy->setParent(m_pagePool);
    return copy;
}

void CodeStyleAspect::apply()
{
    // Nothing to commit until the page has been shown at least once.
    if (!m_pageCodeStyle)
        return;

    // Flush the hosted editor's own pending edits.
    if (m_editor)
        m_editor->apply();

    CodeStylePool *realPool = m_codeStyle->delegatingPool();
    const QByteArray selfId = m_codeStyle->id();
    bool changed = false;

    if (realPool) {
        // Remove the real custom styles that were deleted on the page.
        const QList<ICodeStylePreferences *> realCustom = realPool->customCodeStyles();
        for (ICodeStylePreferences *realStyle : realCustom) {
            if (realStyle->id() != selfId && !m_pagePool->codeStyle(realStyle->id())) {
                realPool->removeCodeStyle(realStyle);
                changed = true;
            }
        }
        // Add styles created on the page and write back edited ones. Built-in
        // styles are read-only and never change.
        const QList<ICodeStylePreferences *> pageStyles = m_pagePool->codeStyles();
        for (ICodeStylePreferences *pageStyle : pageStyles) {
            if (pageStyle->isReadOnly())
                continue;
            ICodeStylePreferences *realStyle = realPool->codeStyle(pageStyle->id());
            if (!realStyle) {
                realPool->createCodeStyle(pageStyle->id(), pageStyle->tabSettings(),
                                          pageStyle->value(), pageStyle->displayName());
                changed = true;
                continue;
            }
            if (realStyle->value() != pageStyle->value()) {
                realStyle->setValue(pageStyle->value());
                changed = true;
            }
            if (realStyle->tabSettings() != pageStyle->tabSettings()) {
                realStyle->setTabSettings(pageStyle->tabSettings());
                changed = true;
            }
            if (realStyle->displayName() != pageStyle->displayName()) {
                realStyle->setDisplayName(pageStyle->displayName());
                changed = true;
            }
        }
    }

    // The global style's own settings and selected delegate.
    if (m_codeStyle->value() != m_pageCodeStyle->value()) {
        m_codeStyle->setValue(m_pageCodeStyle->value());
        changed = true;
    }
    if (m_codeStyle->tabSettings() != m_pageCodeStyle->tabSettings()) {
        m_codeStyle->setTabSettings(m_pageCodeStyle->tabSettings());
        changed = true;
    }
    ICodeStylePreferences *pageDelegate = m_pageCodeStyle->currentDelegate();
    ICodeStylePreferences *realDelegate =
        (pageDelegate && realPool) ? realPool->codeStyle(pageDelegate->id()) : nullptr;
    if (m_codeStyle->currentDelegate() != realDelegate) {
        m_codeStyle->setCurrentDelegate(realDelegate);
        changed = true;
    }

    if (changed)
        m_codeStyle->toSettings(m_languageId.toKey());

    // Re-mirror so the page reflects the persisted state (e.g. ids assigned to
    // newly created styles) and dirtiness clears.
    syncFromReal();
    emit volatileValueChanged();
}

void CodeStyleAspect::cancel()
{
    if (!m_pageCodeStyle)
        return;
    if (m_editor)
        m_editor->cancel();
    syncFromReal();
}

bool CodeStyleAspect::isDirty() const
{
    // Guard on the page copy: until the page is shown it is not yet synced.
    if (!m_pageCodeStyle)
        return false;
    if (m_editor && m_editor->isDirty())
        return true;
    if (m_codeStyle->value() != m_pageCodeStyle->value()
        || m_codeStyle->tabSettings() != m_pageCodeStyle->tabSettings()) {
        return true;
    }
    const ICodeStylePreferences *realDelegate = m_codeStyle->currentDelegate();
    const ICodeStylePreferences *pageDelegate = m_pageCodeStyle->currentDelegate();
    const QByteArray realDelegateId = realDelegate ? realDelegate->id() : QByteArray();
    const QByteArray pageDelegateId = pageDelegate ? pageDelegate->id() : QByteArray();
    if (realDelegateId != pageDelegateId)
        return true;
    return poolsDiffer();
}

bool CodeStyleAspect::poolsDiffer() const
{
    CodeStylePool *realPool = m_codeStyle->delegatingPool();
    if (!realPool)
        return false;

    // A page style missing from, or differing from, the real pool?
    const QList<ICodeStylePreferences *> pageStyles = m_pagePool->codeStyles();
    for (ICodeStylePreferences *pageStyle : pageStyles) {
        ICodeStylePreferences *realStyle = realPool->codeStyle(pageStyle->id());
        if (!realStyle)
            return true;
        if (realStyle->value() != pageStyle->value()
            || realStyle->tabSettings() != pageStyle->tabSettings()
            || realStyle->displayName() != pageStyle->displayName()) {
            return true;
        }
    }
    // A real (delegatable) style removed on the page?
    const QByteArray selfId = m_codeStyle->id();
    const QList<ICodeStylePreferences *> realStyles = realPool->codeStyles();
    for (ICodeStylePreferences *realStyle : realStyles) {
        if (realStyle->id() != selfId && !m_pagePool->codeStyle(realStyle->id()))
            return true;
    }
    return false;
}

void CodeStyleAspect::syncFromReal()
{
    m_syncing = true;
    CodeStylePool *realPool = m_codeStyle->delegatingPool();
    const QByteArray selfId = m_codeStyle->id();

    // Drop page styles whose real counterpart is gone.
    const QList<ICodeStylePreferences *> pageStyles = m_pagePool->codeStyles();
    for (ICodeStylePreferences *pageStyle : pageStyles) {
        if (!realPool || !realPool->codeStyle(pageStyle->id()))
            m_pagePool->removeCodeStyle(pageStyle);
    }
    // Add or refresh a page copy for every delegatable real style. The real
    // global is represented by m_pageCodeStyle, so it is skipped.
    if (realPool) {
        const QList<ICodeStylePreferences *> realStyles = realPool->codeStyles();
        for (ICodeStylePreferences *realStyle : realStyles) {
            if (realStyle->id() == selfId)
                continue;
            ICodeStylePreferences *pageStyle = m_pagePool->codeStyle(realStyle->id());
            if (!pageStyle)
                pageStyle = addPageCopy(realStyle);
            pageStyle->setValue(realStyle->value());
            pageStyle->setTabSettings(realStyle->tabSettings());
            pageStyle->setDisplayName(realStyle->displayName());
        }
    }

    // Sync the page global's own settings and its selected delegate.
    m_pageCodeStyle->setValue(m_codeStyle->value());
    m_pageCodeStyle->setTabSettings(m_codeStyle->tabSettings());
    ICodeStylePreferences *realDelegate = m_codeStyle->currentDelegate();
    m_pageCodeStyle->setCurrentDelegate(
        realDelegate ? m_pagePool->codeStyle(realDelegate->id()) : nullptr);

    m_syncing = false;
}

} // TextEditor
