add_qtc_plugin(Subversion
  PLUGIN_DEPENDS Core DiffEditor TextEditor VcsBase
  SOURCES
    annotationhighlighter.cpp annotationhighlighter.h
    subversionclient.cpp subversionclient.h
    subversionconstants.h
    subversioneditor.cpp subversioneditor.h
    subversionplugin.cpp subversionplugin.h
    subversionsettings.cpp subversionsettings.h
    subversionsubmiteditor.cpp subversionsubmiteditor.h
    subversiontr.h
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET Subversion)
  qt_add_qml_module(Subversion
    URI QtCreator.Subversion
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      SubversionSettingsPage.qml
  )
endif()
