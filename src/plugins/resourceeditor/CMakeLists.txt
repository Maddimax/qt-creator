add_qtc_plugin(ResourceEditor
  DEPENDS Qt::Xml
  PLUGIN_DEPENDS Core ProjectExplorer
  SOURCES
    qrceditor/qrceditor.cpp
    qrceditor/qrceditor.h
    qrceditor/resourcefile.cpp
    qrceditor/resourcefile_p.h
    resource_global.h
    resourceeditor.cpp
    resourceeditor.h
    resourceeditorconstants.h
    resourceeditorplugin.cpp
    resourceeditortr.h
    resourcenode.cpp
    resourcenode.h
)

# The dialogs' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET ResourceEditor)
  qt_add_qml_module(ResourceEditor
    URI QtCreator.ResourceEditor
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      PrefixLangDialog.qml
  )
endif()
