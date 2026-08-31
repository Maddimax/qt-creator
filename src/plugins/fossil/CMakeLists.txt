add_qtc_plugin(Fossil
  PLUGIN_DEPENDS
    Core TextEditor ProjectExplorer VcsBase
  SOURCES
    annotationhighlighter.cpp annotationhighlighter.h
    branchinfo.h
    commiteditor.cpp commiteditor.h
    configuredialog.cpp configuredialog.h
    constants.h
    fossil.qrc
    fossilclient.cpp fossilclient.h
    fossilcommitwidget.cpp fossilcommitwidget.h
    fossileditor.cpp fossileditor.h
    fossilplugin.cpp
    fossilsettings.cpp fossilsettings.h
    fossiltr.h
    pullorpushdialog.cpp pullorpushdialog.h
    revisioninfo.h
    wizard/fossiljsextension.cpp wizard/fossiljsextension.h
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET Fossil)
  qt_add_qml_module(Fossil
    URI QtCreator.Fossil
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      ConfigureDialog.qml
      PullOrPushDialog.qml
      RevertDialog.qml
      FossilSettingsPage.qml
  )
endif()
