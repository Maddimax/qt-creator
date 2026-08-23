add_qtc_plugin(ZenMode
  PLUGIN_DEPENDS
    Core
    TextEditor
  DEPENDS
    Qt::Widgets
    QtCreator::ExtensionSystem
    QtCreator::Utils
  SOURCES
    zenmodeplugin.cpp
    zenmodepluginconstants.h
    zenmodeplugintr.h
    zenmode.qrc
    zenmodesettings.cpp
    zenmodesettings.h
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET ZenMode AND TARGET Qt::Quick)
  qt_add_qml_module(ZenMode
    URI QtCreator.ZenMode
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      ZenModeSettingsPage.qml
  )
endif()
