add_qtc_plugin(Vcpkg
  PLUGIN_DEPENDS Core ProjectExplorer
  SOURCES
    vcpkg.qrc
    vcpkgconstants.h
    vcpkgmanifesteditor.cpp vcpkgmanifesteditor.h
    vcpkgplugin.cpp
    vcpkgsearch.cpp vcpkgsearch.h
    vcpkgsettings.cpp vcpkgsettings.h
    vcpkgtr.h
)

extend_qtc_plugin(Vcpkg
  CONDITION WITH_TESTS
  SOURCES
    vcpkg_test.cpp vcpkg_test.h
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET Vcpkg AND TARGET Qt::Quick)
  qt_add_qml_module(Vcpkg
    URI QtCreator.Vcpkg
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      VcpkgProjectPanel.qml
      VcpkgSettingsPage.qml
  )
endif()
