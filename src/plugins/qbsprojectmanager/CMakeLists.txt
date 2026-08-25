add_qtc_plugin(QbsProjectManager
  DEPENDS Qt::Qml Qt::Widgets QmlJS
  DEFINES
    IDE_LIBRARY_BASENAME="${IDE_LIBRARY_BASE_PATH}"
  PLUGIN_DEPENDS Core CppEditor LanguageClient ProjectExplorer QmlJSTools QmlJSEditor QtSupport
  SOURCES
    customqbspropertiesdialog.cpp customqbspropertiesdialog.h
    defaultpropertyprovider.cpp defaultpropertyprovider.h
    propertyprovider.h
    qbsbuildconfiguration.cpp qbsbuildconfiguration.h
    qbsbuildstep.cpp qbsbuildstep.h
    qbscleanstep.cpp qbscleanstep.h
    qbseditor.cpp qbseditor.h
    qbsinstallstep.cpp qbsinstallstep.h
    qbskitaspect.cpp qbskitaspect.h
    qbslanguageclient.cpp qbslanguageclient.h
    qbsnodes.cpp qbsnodes.h
    qbsnodetreebuilder.cpp qbsnodetreebuilder.h
    qbspmlogging.cpp qbspmlogging.h
    qbsprofilemanager.cpp qbsprofilemanager.h
    qbsprofilessettingspage.cpp qbsprofilessettingspage.h
    qbsproject.cpp qbsproject.h
    qbsprojectimporter.cpp qbsprojectimporter.h
    qbsprojectmanager.qrc
    qbsprojectmanager_global.h
    qbsprojectmanagertr.h
    qbsprojectmanagerconstants.h
    qbsprojectmanagerplugin.cpp qbsprojectmanagerplugin.h
    qbsprojectparser.cpp qbsprojectparser.h
    qbsrequest.cpp qbsrequest.h
    qbssession.cpp qbssession.h
    qbssettings.cpp qbssettings.h
)

extend_qtc_plugin(QbsProjectManager
  CONDITION WITH_TESTS
  DEPENDS Qt::Test
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET QbsProjectManager AND TARGET Qt::Quick)
  qt_add_qml_module(QbsProjectManager
    URI QtCreator.QbsProjectManager
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      QbsProfilesPage.qml
      QbsSettingsPage.qml
  )
endif()
