add_qtc_plugin(GNProjectManager
  PLUGIN_CLASS GNProjectPlugin
  PLUGIN_DEPENDS Core ProjectExplorer TextEditor QtSupport
  SOURCES
    gnactionsmanager.cpp
    gnactionsmanager.h
    gnbuildconfiguration.cpp
    gnbuildconfiguration.h
    gnbuildstep.cpp
    gnbuildstep.h
    gnbuildsystem.cpp
    gnbuildsystem.h
    gninfoparser.cpp
    gninfoparser.h
    gnkitaspect.cpp
    gnkitaspect.h
    gnpluginconstants.h
    gnproject.cpp
    gnproject.h
    gnprojectmanagertr.h
    gnprojectnodes.cpp
    gnprojectnodes.h
    gnprojectparser.cpp
    gnprojectparser.h
    gnprojectplugin.cpp
    gntarget.h
    gntools.cpp
    gntools.h
    gntoolssettingspage.cpp
    gntoolssettingspage.h
    gnutils.cpp
    gnutils.h
    ninjaparser.cpp
    ninjaparser.h
)

extend_qtc_plugin(GNProjectManager
  CONDITION WITH_TESTS
  DEPENDS Qt::Test
)

# The settings page's Qt Quick form. A module rather than a plain resource so
# that qmllint and qmlcachegen see the file; qbs cannot build QML modules, so
# it only lists it - the same split qtcquick already lives with.
if(TARGET GNProjectManager)
  qt_add_qml_module(GNProjectManager
    URI QtCreator.GNProjectManager
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      GNToolsPage.qml
  )
endif()
