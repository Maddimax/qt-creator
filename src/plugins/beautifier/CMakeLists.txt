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
    configurationsaspect.cpp configurationsaspect.h
    generalsettings.cpp generalsettings.h
    uncrustify/uncrustify.cpp uncrustify/uncrustify.h
)

extend_qtc_plugin(Beautifier
  CONDITION WITH_TESTS
  DEPENDS Qt::Test
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET Beautifier)
  qt_add_qml_module(Beautifier
    URI QtCreator.Beautifier
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      ArtisticStylePage.qml
      BeautifierConfigurations.qml
      BeautifierSettingsPage.qml
      ClangFormatPage.qml
      UncrustifyPage.qml
  )
endif()
