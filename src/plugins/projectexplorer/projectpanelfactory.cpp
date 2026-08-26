// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "projectpanelfactory.h"

#include "project.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <utils/aspects.h>

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
    if (s_previousHandler)
        s_previousHandler(type, context, message);
}

class ProjectPanelFactoryTest final : public QObject
{
    Q_OBJECT

private slots:
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
