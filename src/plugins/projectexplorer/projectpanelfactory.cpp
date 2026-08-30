// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "projectpanelfactory.h"

#include "environmentaspect.h"

#include "project.h"
#include "projectexplorertr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <utils/aspects.h>
#include <utils/environment.h>

#include <QLabel>

#ifdef WITH_TESTS
#include <QQmlError>
#include <QScopeGuard>
#include <QTest>
#endif

using namespace ProjectExplorer::Internal;
using namespace Utils;

namespace ProjectExplorer {

static QList<ProjectPanelFactory *> s_factories;
bool s_sorted = false;

ProjectPanelFactory::ProjectPanelFactory()
    : m_supportsFunction([] (Project *) { return true; })
{
    s_factories.append(this);
    s_sorted = false;
}

int ProjectPanelFactory::priority() const
{
    return m_priority;
}

void ProjectPanelFactory::setPriority(int priority)
{
    m_priority = priority;
}

QString ProjectPanelFactory::displayName() const
{
    return m_displayName;
}

void ProjectPanelFactory::setDisplayName(const QString &name)
{
    m_displayName = name;
}

QList<ProjectPanelFactory *> ProjectPanelFactory::factories()
{
    if (!s_sorted) {
        s_sorted = true;
        std::sort(s_factories.begin(), s_factories.end(),
                  [](ProjectPanelFactory *a, ProjectPanelFactory *b)  {
            return (a->priority() == b->priority() && a < b) || a->priority() < b->priority();
        });
    }
    return s_factories;
}

Id ProjectPanelFactory::id() const
{
    return m_id;
}

void ProjectPanelFactory::setId(Id id)
{
    m_id = id;
}

QWidget *ProjectPanelFactory::createWidget(Project *project) const
{
    QTC_ASSERT(project, return nullptr);

    if (m_widgetCreator)
        return m_widgetCreator(project);

    if (m_settingsProvider) {
        AspectContainer * const container = m_settingsProvider(project);
        QTC_ASSERT(container, return nullptr);
        QWidget * const form = Core::createAspectForm(container);
        QTC_ASSERT(form, return nullptr);
        // The tab title, which a hand-built panel sets on the widget it makes.
        form->setWindowTitle(m_displayName);
        return form;
    }

    QTC_CHECK(false);
    return nullptr;
}

void ProjectPanelFactory::setCreateWidgetFunction(const WidgetCreator &createWidgetFunction)
{
    m_widgetCreator = createWidgetFunction;
}

void ProjectPanelFactory::setSettingsProvider(const SettingsProvider &provider)
{
    m_settingsProvider = provider;
}

std::optional<AspectContainer *> ProjectPanelFactory::aspects(Project *project) const
{
    if (!m_settingsProvider)
        return std::nullopt;
    return std::make_optional(m_settingsProvider(project));
}

bool ProjectPanelFactory::supports(Project *project)
{
    return m_supportsFunction(project);
}

void ProjectPanelFactory::setSupportsFunction(std::function<bool (Project *)> function)
{
    m_supportsFunction = function;
}

#ifdef WITH_TESTS

// Enough of a project for a panel to be asked what it shows. A panel that needs
// more than this is one that builds its own widget, and those are counted, not
// built.
class PanelCensusProject final : public Project
{
public:
    PanelCensusProject()
        : Project("text/plain", FilePath::fromString("/tmp/qtc-panel-census.project"))
    {
        setDisplayName("Panel census");
    }

    bool needsConfiguration() const final { return false; }
};

// A form names its aspects, and a name no aspect answers to is undefined in
// QML rather than an error: the delegate is built and draws nothing. What it
// does do is complain at runtime, which is the only place it shows up at all -
// qmllint cannot see through a QQmlPropertyMap.
static QStringList *s_qmlComplaints = nullptr;
static QtMessageHandler s_previousHandler = nullptr;

static void collectQmlComplaints(QtMsgType type,
                                 const QMessageLogContext &context,
                                 const QString &message)
{
    if (s_qmlComplaints && type == QtWarningMsg && message.contains("qrc:/qt/qml/QtCreator"))
        *s_qmlComplaints << message;
    // A name a container does not hold does not fail as a binding: it reaches
    // AspectModels, which soft-asserts. That is a debug message, so without
    // this the sweep would pass on a page asking for an aspect that is not
    // there.
    if (s_qmlComplaints && type == QtDebugMsg && message.contains("SOFT ASSERT")
        && message.contains("aspectmodels.cpp")) {
        *s_qmlComplaints << message;
    }
    if (s_previousHandler)
        s_previousHandler(type, context, message);
}

class ProjectPanelFactoryTest final : public QObject
{
    Q_OBJECT

private slots:
    // The environment panel shows one thing in two surfaces - the resulting
    // environment as a table, and the changes as text - and the widget form it
    // replaced kept them in step. Typing into one has to reach the other and
    // the project, and must not come back round as a change to itself.
    // A run or build configuration's environment is drawn by the same editor
    // the project panel uses, so a Qt Quick page can show one at all: it used
    // to draw itself with a widget, which a Quick page has no way to host and
    // reported as "(no Qt Quick editor yet)".
    void testAnEnvironmentAspectOffersTheSameEditor()
    {
        EnvironmentAspect aspect;
        aspect.addSupportedBaseEnvironment("Clean Environment", [] { return Environment(); });
        aspect.addSupportedBaseEnvironment("System Environment", [] {
            Environment env;
            env.set("QTC_ASPECT_TEST", "one");
            return env;
        });
        aspect.setBaseEnvironmentBase(1);

        const auto byName = [&aspect](const QString &name) -> BaseAspect * {
            for (BaseAspect * const sub : aspect.aspects()) {
                if (sub->qmlName() == name)
                    return sub;
            }
            return nullptr;
        };

        // What a page draws: which base, the editor, and whether to print it.
        BaseAspect * const base = byName("BaseEnvironment");
        BaseAspect * const editor = byName("Editor");
        BaseAspect * const printOnRun = byName("PrintOnRun");
        QVERIFY(base);
        QVERIFY(editor);
        QVERIFY(printOnRun);
        QCOMPARE(int(editor->presentation().control),
                 int(Utils::AspectControls::EnvironmentEditor));

        // The editor shows the environment the chosen base gives.
        QAbstractItemModel *model = nullptr;
        for (BaseAspect * const sub : qobject_cast<AspectContainer *>(editor)->aspects()) {
            if (sub->qmlName() == "Variables")
                model = sub->tableModel();
        }
        QVERIFY2(model, "the editor hands out no table");
        const auto rowFor = [model](const QString &name) {
            for (int row = 0; row < model->rowCount({}); ++row) {
                if (model->index(row, 0).data().toString() == name)
                    return row;
            }
            return -1;
        };
        QVERIFY2(rowFor("QTC_ASPECT_TEST") >= 0, "the editor was given no base environment");

        // And what is edited *there* is what the aspect then reports, which is
        // what the run configuration builds its environment from. Through the
        // model, which is what the table writes to: setChanges() is the other
        // direction and deliberately does not report back.
        const int row = rowFor("QTC_ASPECT_TEST");
        QVERIFY(model->setData(model->index(row, 1), "two"));
        QCOMPARE(aspect.environment().expandedValueForKey("QTC_ASPECT_TEST"), QString("two"));
        QCOMPARE(aspect.userEnvironmentChanges().itemsFromUser().size(), 1);
    }

    void testTheEnvironmentPanelKeepsItsTwoSurfacesInStep()
    {
        PanelCensusProject project;

        ProjectPanelFactory *factory = nullptr;
        for (ProjectPanelFactory * const candidate : ProjectPanelFactory::factories()) {
            if (candidate->displayName() == Tr::tr("Project Environment"))
                factory = candidate;
        }
        QVERIFY2(factory, "no Project Environment panel");

        const std::optional<AspectContainer *> aspects = factory->aspects(&project);
        QVERIFY2(aspects && *aspects, "the panel offers no settings");

        // At any depth: the panel holds an EnvironmentEditorAspect, which is
        // where the two surfaces live - every page that edits an environment
        // uses the same one.
        const std::function<BaseAspect *(AspectContainer *, const QString &)> byNameIn =
            [&byNameIn](AspectContainer *container, const QString &name) -> BaseAspect * {
            for (BaseAspect * const aspect : container->aspects()) {
                if (aspect->qmlName() == name)
                    return aspect;
                if (auto * const nested = qobject_cast<AspectContainer *>(aspect)) {
                    if (BaseAspect * const found = byNameIn(nested, name))
                        return found;
                }
            }
            return nullptr;
        };
        const auto byName = [&](const QString &name) { return byNameIn(*aspects, name); };

        BaseAspect * const changes = byName("Changes");
        BaseAspect * const variables = byName("Variables");
        QVERIFY(changes);
        QVERIFY(variables);
        QAbstractItemModel * const model = variables->tableModel();
        QVERIFY(model);

        const auto rowFor = [model](const QString &name) {
            for (int row = 0; row < model->rowCount({}); ++row) {
                if (model->index(row, 0).data().toString() == name)
                    return row;
            }
            return -1;
        };

        QCOMPARE(rowFor("QTC_PANEL_TEST"), -1);

        // Typed into the text surface: the table shows it, and so does the
        // project, which is what the widget form's userChangesChanged did.
        changes->setVariantValue("QTC_PANEL_TEST=one");
        const int row = rowFor("QTC_PANEL_TEST");
        QVERIFY2(row >= 0, "the table did not follow the text");
        QCOMPARE(model->index(row, 1).data().toString(), QString("one"));
        QCOMPARE(project.additionalEnvironment().itemsFromUser().size(), 1);

        // And back the other way, without the write coming round again: the
        // text is rewritten from the model, so a guard that does not hold
        // would leave the text as it was typed.
        QVERIFY(model->setData(model->index(row, 1), "two"));
        QCOMPARE(model->index(row, 1).data().toString(), QString("two"));
        QCOMPARE(changes->variantValue().toString(), QString("QTC_PANEL_TEST=two"));
    }

    void testPanelsThatSayWhatTheyShowRenderWithQuick()
    {
        PanelCensusProject project;

        QStringList complaints;
        s_qmlComplaints = &complaints;
        s_previousHandler = qInstallMessageHandler(collectQmlComplaints);
        const QScopeGuard restoreHandler([] {
            qInstallMessageHandler(s_previousHandler);
            s_qmlComplaints = nullptr;
        });

        int aspectDriven = 0;
        QStringList buildTheirOwn;
        for (ProjectPanelFactory * const factory : ProjectPanelFactory::factories()) {
            if (!factory->supports(&project))
                continue;

            const std::optional<AspectContainer *> aspects = factory->aspects(&project);
            if (!aspects) {
                buildTheirOwn << factory->displayName();
                continue;
            }
            QVERIFY2(*aspects,
                     qPrintable(factory->displayName()
                                + " offers settings and then hands over nothing"));
            ++aspectDriven;

            // A panel that says what it shows has to say it in QML: the point
            // of the seam is that the form is the container's, and a container
            // without QML would quietly fall back to a widget layout.
            const QUrl source = (*aspects)->qmlSource();
            QVERIFY2(!source.isEmpty(),
                     qPrintable(factory->displayName() + " names no QML file"));

            const std::unique_ptr<QWidget> widget(factory->createWidget(&project));
            QVERIFY2(widget, qPrintable(factory->displayName() + " built no form"));
            // The tab title, which the panel used to set on the widget it made.
            QCOMPARE(widget->windowTitle(), factory->displayName());

            // Found by class name rather than by type: this plugin has no
            // reason to link QuickWidgets, and the two properties asked for
            // below are enough to tell a loaded form from an empty one.
            QWidget *quick = nullptr;
            for (QWidget * const child : widget->findChildren<QWidget *>()) {
                if (child->inherits("QQuickWidget")) {
                    quick = child;
                    break;
                }
            }
            // A panel the user cannot get out of. The setting that turns a
            // panel off - "use global settings" - must stay usable while
            // everything it turns off is disabled, so at least one aspect is
            // always enabled. The mistake this catches is putting that setting
            // inside the very container it disables.
            bool anyEnabled = false;
            (*aspects)->forEachAspect([&anyEnabled](Utils::BaseAspect *aspect) {
                anyEnabled = anyEnabled || aspect->isEnabled();
            });
            QVERIFY2(anyEnabled,
                     qPrintable(factory->displayName()
                                + " is disabled as a whole, so there is no way back"));

            QVERIFY2(quick, qPrintable(factory->displayName() + " has no Quick form"));
            QCOMPARE(quick->property("source").toUrl(), source);
            // QQuickWidget::Ready. A component that failed to load is Error,
            // and its widget is empty rather than absent - so without this the
            // check above passes on a panel that shows nothing.
            QCOMPARE(quick->property("status").toInt(), 1);

            // A form names its aspects, and a name that does not exist is
            // undefined in QML rather than an error: the delegate is built and
            // draws nothing. So every delegate must have found its aspect, and
            // there must be one. Delegates are recognised by declaring an
            // "aspect" property, which is what makes them one - read through
            // the metaobject, so this needs no Quick headers.
        }

        QVERIFY2(aspectDriven > 0, "no panel is aspect-driven, so nothing was checked");
        QVERIFY2(complaints.isEmpty(), qPrintable("QML complained: " + complaints.join("; ")));
        qInfo().noquote() << aspectDriven << "panel(s) say what they show;"
                          << buildTheirOwn.size() << "still build a widget:"
                          << buildTheirOwn.join(", ");
    }
};

QObject *createProjectPanelFactoryTest()
{
    return new ProjectPanelFactoryTest;
}

#endif // WITH_TESTS

// Helpers

QLabel *createGlobalSettingsLink(Utils::Id globalId)
{
    const auto label = new QLabel(R"(<a href="dummy">Global settings</a>)");
    QObject::connect(label, &QLabel::linkActivated, label, [globalId] {
        Core::ICore::showSettings(globalId);
    });
    return label;
}

} // namespace ProjectExplorer

#ifdef WITH_TESTS
#include "projectpanelfactory.moc"
#endif
