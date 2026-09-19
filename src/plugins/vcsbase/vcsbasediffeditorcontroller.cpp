// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "vcsbasediffeditorcontroller.h"

#include "vcsbaseeditor.h"
#include "vcsbasetr.h"
#include "vcseditordocument.h"

#include <coreplugin/editormanager/ieditor.h>

#include <diffeditor/diffeditorconstants.h>

#include <texteditor/textdocument.h>

#include <utils/ansiescapecodehandler.h>
#include <utils/async.h>
#include <utils/environment.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>

using namespace Core;
using namespace DiffEditor;
using namespace QtTaskTree;
using namespace Utils;

namespace VcsBase {

// The description pane above a VCS's diff: a VCS editor over other content
// with change links all the same, prose rather than code - no line numbers,
// no wrapping, no tool bar - in the description context the widget pane was
// in. What the pane links to and describes is the controller's to say, per
// editor, through the document.
static VcsEditorFactory &descriptionEditorFactory()
{
    class DescriptionEditorFactory final : public VcsEditorFactory
    {
    public:
        DescriptionEditorFactory()
            : VcsEditorFactory(parameters())
        {
            setLineNumbersVisible(false);
            setWrapsLines(false);
            setToolBarVisible(false);
            addEditorContext(DiffEditor::Constants::C_DIFF_EDITOR_DESCRIPTION);
        }

    private:
        static VcsBaseEditorParameters parameters()
        {
            VcsBaseEditorParameters parameters;
            parameters.type = OtherContent;
            parameters.id = "VcsBase.DescriptionEditor";
            parameters.displayName = Tr::tr("Version Control Description");
            parameters.changeLinksInOtherContent = true;
            return parameters;
        }
    };
    static DescriptionEditorFactory theFactory;
    return theFactory;
}

void setupVcsBaseDescriptionEditorFactory()
{
    descriptionEditorFactory();
}

class VcsBaseDiffEditorControllerPrivate
{
public:
    VcsBaseDiffEditorControllerPrivate(VcsBaseDiffEditorController *q) : q(q) {}

    VcsBaseDiffEditorController *q;
    Environment m_processEnvironment;
    FilePath m_vcsBinary;
};

/////////////////////

VcsBaseDiffEditorController::VcsBaseDiffEditorController(Core::IDocument *document)
    : DiffEditorController(document)
    , d(new VcsBaseDiffEditorControllerPrivate(this))
{}

VcsBaseDiffEditorController::~VcsBaseDiffEditorController()
{
    delete d;
}

DiffEditor::DescriptionEditorProvider createVcsBaseDescriptionEditorProvider(
    const VcsBaseDescriptionEditorParameters &parameters)
{
    return {
        [parameters] {
            IEditor *editor = descriptionEditorFactory().createEditor();
            QTC_ASSERT(editor, return editor);
            auto *document = qobject_cast<VcsEditorDocument *>(editor->document());
            QTC_ASSERT(document, return editor);
            document->setDescriptionParameters(parameters);
            return editor;
        },
        [](IEditor *editor, const QString &text, bool ansiEnabled) {
            auto *document = qobject_cast<TextEditor::TextDocument *>(editor->document());
            QTC_ASSERT(document, return);
            if (ansiEnabled)
                AnsiEscapeCodeHandler::setTextInDocument(document->document(), text);
            else
                document->setPlainText(text);
        }
    };
}

DiffEditor::DescriptionEditorProvider
VcsBaseDiffEditorController::descriptionEditorProvider() const
{
    VcsBaseDescriptionEditorParameters parameters;
    parameters.source = workingDirectory();
    return createVcsBaseDescriptionEditorProvider(parameters);
}

GroupItem VcsBaseDiffEditorController::postProcessTask(const Storage<QString> &inputStorage)
{
    const auto onSetup = [inputStorage](Async<QList<FileData>> &async) {
        async.setConcurrentCallData(&DiffUtils::readPatchWithPromise, *inputStorage);
    };
    const auto onDone = [this](const Async<QList<FileData>> &async, DoneWith result) {
        setDiffFiles(result == DoneWith::Success && async.isResultAvailable()
                     ? async.result() : QList<FileData>());
        // TODO: We should set the right starting line here
    };
    return AsyncTask<QList<FileData>>(onSetup, onDone);
}

void VcsBaseDiffEditorController::setupCommand(Process &process, const QStringList &args) const
{
    process.setEnvironment(d->m_processEnvironment);
    process.setWorkingDirectory(workingDirectory());
    process.setCommand({d->m_vcsBinary, args});
    process.setUseCtrlCStub(true);
}

void VcsBaseDiffEditorController::setVcsBinary(const FilePath &path)
{
    d->m_vcsBinary = path;
}

void VcsBaseDiffEditorController::setProcessEnvironment(const Environment &value)
{
    d->m_processEnvironment = value;
}

} // namespace VcsBase
