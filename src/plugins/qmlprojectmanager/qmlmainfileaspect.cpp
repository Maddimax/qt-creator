// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmlmainfileaspect.h"

#include "buildsystem/qmlbuildsystem.h"
#include "qmlprojectconstants.h"
#include "qmlprojectmanagertr.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>

#include <projectexplorer/projectexplorer.h>

#include <utils/mimeconstants.h>
#include <utils/mimeutils.h>
#include <utils/qtcassert.h>

#ifdef WITH_TESTS
#include <QTest>
#endif


using namespace Core;
using namespace ProjectExplorer;
using namespace Utils;

namespace QmlProjectManager {

const char M_CURRENT_FILE[] = "CurrentFile";
const char CURRENT_FILE[]  = QT_TRANSLATE_NOOP("QtC::QmlProjectManager", "<Current File>");

static bool caseInsensitiveLessThan(const FilePath &s1, const FilePath &s2)
{
    return s1.toUrlishString().toCaseFolded() < s2.toUrlishString().toCaseFolded();
}

QmlMainFileAspect::QmlMainFileAspect(AspectContainer *container)
    : BaseAspect(container)
    , m_scriptFile(M_CURRENT_FILE)
{
    addDataExtractor(this, &QmlMainFileAspect::mainScript, &Data::mainScript);
    addDataExtractor(this, &QmlMainFileAspect::currentFile, &Data::currentFile);

    connect(EditorManager::instance(), &EditorManager::currentEditorChanged,
            this, &QmlMainFileAspect::changeCurrentFile);
    connect(EditorManager::instance(), &EditorManager::currentDocumentStateChanged,
            this, [this] { changeCurrentFile(); });
    connect(ProjectExplorerPlugin::instance(),
            &ProjectExplorerPlugin::fileListChanged,
            this,
            &QmlMainFileAspect::updateFileList);
}

QmlMainFileAspect::~QmlMainFileAspect() = default;

AspectPresentation QmlMainFileAspect::presentation() const
{
    AspectPresentation p = BaseAspect::presentation();
    p.control = AspectControls::ComboBox;
    p.labelText = Tr::tr("Main QML file:");
    // The stored value is the file, not a position: the list is rebuilt
    // whenever the project's files change.
    p.valueIsChoiceId = true;
    for (const QString &id : m_fileList)
        p.choices.append({id == QLatin1String(M_CURRENT_FILE) ? Tr::tr(CURRENT_FILE) : id,
                          {}, true, id});
    return p;
}

QVariant QmlMainFileAspect::volatileVariantValue() const
{
    switch (mainScriptSource()) {
    case FileInEditor:
        return QString(M_CURRENT_FILE);
    case FileInProjectFile:
        // The .qmlproject said which file, and it is the only one on offer.
        return m_fileList.value(0);
    case FileInSettings:
        break;
    }
    return m_scriptFile;
}

void QmlMainFileAspect::setVolatileVariantValue(const QVariant &value, Announcement)
{
    const QString id = value.toString();
    if (id.isEmpty() || id == QLatin1String(M_CURRENT_FILE))
        setScriptSource(FileInEditor);
    else
        setScriptSource(FileInSettings, id);
}

void QmlMainFileAspect::toMap(Store &map) const
{
    map.insert(Constants::QML_MAINSCRIPT_KEY, m_scriptFile);
}

void QmlMainFileAspect::fromMap(const Store &map)
{
    m_scriptFile = map.value(Constants::QML_MAINSCRIPT_KEY, M_CURRENT_FILE).toString();

    if (m_scriptFile == M_CURRENT_FILE)
        setScriptSource(FileInEditor);
    else if (m_scriptFile.isEmpty())
        setScriptSource(FileInProjectFile);
    else
        setScriptSource(FileInSettings, m_scriptFile);
}

QStringList mainFileChoices(const FilePaths &projectFiles, const FilePath &projectDir)
{
    FilePaths relative;
    for (const FilePath &fn : projectFiles) {
        // fn relative to projectDir, not the other way round. Reversed, this
        // answered an empty path for every file, and the chooser has offered
        // nothing but the file in the editor for as long as it has existed.
        relative += fn.relativeChildPath(projectDir);
    }
    std::stable_sort(relative.begin(), relative.end(), caseInsensitiveLessThan);

    QStringList choices{QLatin1String(M_CURRENT_FILE)};
    for (const FilePath &fn : std::as_const(relative)) {
        if (fn.suffixView() == u"qml")
            choices += fn.toUrlishString();
    }
    return choices;
}

void QmlMainFileAspect::updateFileList()
{
    auto buildSystem = qmlBuildSystem();
    QTC_ASSERT(buildSystem, return);
    const FilePath projectDir = buildSystem->projectDirectory();

    // A .qmlproject that names its main file leaves nothing to choose.
    if (mainScriptSource() == FileInProjectFile) {
        m_fileList = {mainScript().relativePathFromDir(projectDir)};
        setEnabled(false);
    } else {
        m_fileList = mainFileChoices(buildSystem->project()->files(Project::SourceFiles),
                                     projectDir);
        setEnabled(true);
    }
    emit controlConfigurationChanged();
}

QmlMainFileAspect::MainScriptSource QmlMainFileAspect::mainScriptSource() const
{
    QTC_ASSERT(qmlBuildSystem(), return FileInEditor);
    if (!qmlBuildSystem()->mainFile().isEmpty())
        return FileInProjectFile;
    if (!m_mainScriptFilename.isEmpty())
        return FileInSettings;
    return FileInEditor;
}

void QmlMainFileAspect::setScriptSource(MainScriptSource source, const QString &settingsPath)
{
    if (source == FileInEditor) {
        m_scriptFile = M_CURRENT_FILE;
        m_mainScriptFilename.clear();
    } else if (source == FileInProjectFile) {
        m_scriptFile.clear();
        m_mainScriptFilename.clear();
    } else { // FileInSettings
        m_scriptFile = settingsPath;
        if (QTC_GUARD(qmlBuildSystem()))
            m_mainScriptFilename = qmlBuildSystem()->projectDirectory() / m_scriptFile;
    }

    emit changed();
    updateFileList();
}

/**
  Returns absolute path to main script file.
  */
FilePath QmlMainFileAspect::mainScript() const
{
    if (QTC_GUARD(qmlBuildSystem()) && !qmlBuildSystem()->mainFile().isEmpty()) {
        const FilePath pathInProject = qmlBuildSystem()->mainFilePath();
        return qmlBuildSystem()->canonicalProjectDir().resolvePath(pathInProject);
    }

    if (!m_mainScriptFilename.isEmpty())
        return m_mainScriptFilename;

    return m_currentFileFilename;
}

FilePath QmlMainFileAspect::currentFile() const
{
    return m_currentFileFilename;
}

void QmlMainFileAspect::changeCurrentFile(Core::IEditor *editor)
{
    if (!editor)
        editor = EditorManager::currentEditor();

    if (editor)
        m_currentFileFilename = editor->document()->filePath();

    emit changed();
}

bool QmlMainFileAspect::isQmlFilePresent()
{
    bool qmlFileFound = false;
    if (mainScriptSource() == FileInEditor && !mainScript().isEmpty()) {
        using namespace Utils::Constants;
        IDocument *document = EditorManager::currentDocument();
        const MimeType mainScriptMimeType = mimeTypeForFile(mainScript());
        if (document) {
            m_currentFileFilename = document->filePath();
            if (mainScriptMimeType.matchesName(QML_MIMETYPE)
                    || mainScriptMimeType.matchesName(QMLUI_MIMETYPE)) {
                qmlFileFound = true;
            }
        }
        if (!document
                || mainScriptMimeType.matchesName(QMLPROJECT_MIMETYPE)) {
            // find a qml file with lowercase filename. This is slow, but only done
            // in initialization/other border cases.

            QTC_ASSERT(qmlBuildSystem(), return qmlFileFound);
            const FilePaths files = qmlBuildSystem()->project()->files(Project::SourceFiles);
            for (const FilePath &filename : files) {
                if (!filename.isEmpty() && filename.baseName().at(0).isLower()) {
                    const MimeType type = mimeTypeForFile(filename);
                    if (type.matchesName(QML_MIMETYPE) || type.matchesName(QMLUI_MIMETYPE)) {
                        m_currentFileFilename = filename;
                        qmlFileFound = true;
                        break;
                    }
                }
            }
        }
    } else { // use default one
        qmlFileFound = !mainScript().isEmpty();
    }
    return qmlFileFound;
}

QmlBuildSystem *QmlMainFileAspect::qmlBuildSystem() const
{
    RunConfiguration *runConfig = qobject_cast<RunConfiguration *>(container());
    QTC_ASSERT(runConfig, return nullptr);
    return qobject_cast<QmlBuildSystem *>(runConfig->buildSystem());
}

#ifdef WITH_TESTS
class QmlMainFileTest : public QObject
{
    Q_OBJECT

private slots:
    void testTheEntriesAreTheProjectsQmlFilesInReadingOrder()
    {
        const FilePath dir = FilePath::fromString("/project");
        const FilePaths files = {
            dir / "src" / "Zoo.qml",
            dir / "main.cpp",             // Not QML, so not on offer.
            dir / "Main.qml",
            dir / "README.md",
            dir / "src" / "apple.qml",
        };

        // "<Current File>" first, then the QML files sorted the way a reader
        // would - apple before Zoo, which is not what sorting by byte does.
        QCOMPARE(mainFileChoices(files, dir),
                 QStringList({"CurrentFile", "Main.qml", "src/apple.qml", "src/Zoo.qml"}));
    }

    void testTheStoredValueIsTheFileRatherThanItsPlaceInTheList()
    {
        QmlMainFileAspect aspect;
        aspect.m_fileList = {"CurrentFile", "Main.qml", "src/apple.qml"};

        const AspectPresentation p = aspect.presentation();
        QCOMPARE(p.control, AspectControls::ComboBox);
        // The list is rebuilt whenever the project's files change, so a
        // position in it means nothing across a rebuild.
        QVERIFY(p.valueIsChoiceId);
        QCOMPARE(p.choices.size(), 3);

        // What is stored for the first entry is a sentinel; what is shown is
        // a name for it.
        QCOMPARE(p.choices.at(0).id.toString(), QString("CurrentFile"));
        QVERIFY(p.choices.at(0).display != QLatin1String("CurrentFile"));
        QVERIFY(!p.choices.at(0).display.isEmpty());

        // The rest are stored as what they are.
        QCOMPARE(p.choices.at(1).id.toString(), QString("Main.qml"));
        QCOMPARE(p.choices.at(1).display, QString("Main.qml"));
    }

    void testAProjectWithNoQmlInItStillOffersTheEditorsFile()
    {
        const FilePath dir = FilePath::fromString("/project");
        QCOMPARE(mainFileChoices({dir / "main.cpp"}, dir), QStringList("CurrentFile"));
        QCOMPARE(mainFileChoices({}, dir), QStringList("CurrentFile"));
    }
};

QObject *createQmlMainFileTest()
{
    return new QmlMainFileTest;
}
#endif // WITH_TESTS

} // QmlProjectManager

#ifdef WITH_TESTS
#include "qmlmainfileaspect.moc"
#endif
