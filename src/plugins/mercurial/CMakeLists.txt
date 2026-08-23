add_qtc_plugin(Mercurial
  PLUGIN_DEPENDS Core TextEditor VcsBase
  SOURCES
    annotationhighlighter.cpp annotationhighlighter.h
    authenticationdialog.cpp authenticationdialog.h
    commiteditor.cpp commiteditor.h
    constants.h
    mercurialclient.cpp mercurialclient.h
    mercurialcommitwidget.cpp mercurialcommitwidget.h
    mercurialeditor.cpp mercurialeditor.h
    mercurialplugin.cpp
    mercurialsettings.cpp mercurialsettings.h
    mercurialtr.h
    revertdialog.cpp revertdialog.h
    srcdestdialog.cpp srcdestdialog.h
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET Mercurial AND TARGET Qt::Quick)
  qt_add_qml_module(Mercurial
    URI QtCreator.Mercurial
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      MercurialSettingsPage.qml
  )
endif()
