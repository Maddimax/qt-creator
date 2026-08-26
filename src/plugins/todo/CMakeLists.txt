add_qtc_plugin(Todo
  DEPENDS QmlJS
  PLUGIN_DEPENDS Core CppEditor ProjectExplorer
  SOURCES
    constants.h
    cpptodoitemsscanner.cpp cpptodoitemsscanner.h
    keyword.cpp keyword.h
    lineparser.cpp lineparser.h
    projectfiletodoitemsscanner.cpp projectfiletodoitemsscanner.h
    qmljstodoitemsscanner.cpp qmljstodoitemsscanner.h
    settings.cpp settings.h
    todoicons.cpp todoicons.h
    todoitem.h
    todoitemsmodel.cpp todoitemsmodel.h
    todoitemsprovider.cpp todoitemsprovider.h
    todoitemsscanner.cpp todoitemsscanner.h
    todooutputpane.cpp todooutputpane.h
    todooutputtreeview.cpp todooutputtreeview.h
    todooutputtreeviewdelegate.cpp todooutputtreeviewdelegate.h
    todoplugin.cpp
    todoplugin.qrc
    todoprojectpanel.cpp todoprojectpanel.h
    todotr.h
)

extend_qtc_plugin(Todo
  CONDITION WITH_TESTS
  DEPENDS Qt::Test
)

# The settings page's Qt Quick form. A module rather than a plain resource so
# that qmllint and qmlcachegen see the file; qbs cannot build QML modules, so
# it only lists it - the same split qtcquick already lives with.
if(TARGET Todo)
  qt_add_qml_module(Todo
    URI QtCreator.Todo
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      TodoProjectPanel.qml
      TodoSettingsPage.qml
  )
endif()
