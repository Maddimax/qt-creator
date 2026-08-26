add_qtc_plugin(UpdateInfo
  DEPENDS Qt::Xml
  PLUGIN_DEPENDS Core
  SOURCES
    updateinfoplugin.cpp
    updateinfoplugin.h
    updateinfoservice.h
    updateinfosettings.cpp
    updateinfosettings.h
    updateinfotools.h
    updateinfotr.h
)

# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET UpdateInfo)
  qt_add_qml_module(UpdateInfo
    URI QtCreator.UpdateInfo
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      UpdateSettingsPage.qml
  )
endif()
