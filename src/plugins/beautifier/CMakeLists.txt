add_qtc_plugin(Beautifier
  DEPENDS Qt::Xml
  PLUGIN_DEPENDS Core ProjectExplorer TextEditor
  SOURCES
    artisticstyle/artisticstyle.cpp artisticstyle/artisticstyle.h
    beautifier.qrc
    beautifierconstants.h
    beautifierplugin.cpp
    beautifiertool.cpp beautifiertool.h
    beautifiertr.h
    clangformat/clangformat.cpp clangformat/clangformat.h
    configurationdialog.cpp configurationdialog.h
    configurationeditor.cpp configurationeditor.h
    configurationpanel.cpp configurationpanel.h
    generalsettings.cpp generalsettings.h
    uncrustify/uncrustify.cpp uncrustify/uncrustify.h
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET Beautifier AND TARGET Qt::Quick)
  qt_add_qml_module(Beautifier
    URI QtCreator.Beautifier
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      BeautifierSettingsPage.qml
  )
endif()
