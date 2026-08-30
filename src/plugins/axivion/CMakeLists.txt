add_qtc_plugin(Axivion
  PLUGIN_DEPENDS
    Core Debugger ProjectExplorer TextEditor
  DEPENDS Qt::Network Qt::Sql Qt::Widgets ExtensionSystem Utils
  LONG_DESCRIPTION_MD AxivionDescription.md
  SOURCES
    axivionperspective.cpp axivionperspective.h
    axivionplugin.cpp axivionplugin.h
    axivionsettings.cpp axivionsettings.h
    axiviontextmarks.cpp axiviontextmarks.h
    axiviontr.h
    axivionutils.cpp axivionutils.h
    dashboard/dto.cpp dashboard/dto.h
    dashboard/concat.cpp dashboard/concat.h
    dashboard/error.h dashboard/error.cpp
    dashboard/error.cpp dashboard/error.h
    dynamiclistmodel.cpp dynamiclistmodel.h
    issueheaderview.cpp issueheaderview.h
    localbuild.cpp localbuild.h
    pluginarserver.cpp pluginarserver.h
    singlefileanalysis.cpp singlefileanalysis.h
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET Axivion)
  qt_add_qml_module(Axivion
    URI QtCreator.Axivion
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      AxivionSettingsPage.qml
      SingleFileAnalysisDialog.qml
      PathMappingForm.qml
  )
endif()

file(GLOB_RECURSE images RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} images/*)
qtc_add_resources(Axivion "images"
  PREFIX "/axivion"
  BASE "."
  FILES ${images}
)
