add_qtc_plugin(Bazaar
  PLUGIN_DEPENDS Core TextEditor VcsBase
  SOURCES
    annotationhighlighter.cpp annotationhighlighter.h
    bazaarclient.cpp bazaarclient.h
    bazaarcommitwidget.cpp bazaarcommitwidget.h
    bazaareditor.cpp bazaareditor.h
    bazaarplugin.cpp
    bazaarsettings.cpp bazaarsettings.h
    bazaartr.h
    branchinfo.cpp branchinfo.h
    commiteditor.cpp commiteditor.h
    constants.h
    pullorpushdialog.cpp pullorpushdialog.h
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET Bazaar AND TARGET Qt::Quick)
  qt_add_qml_module(Bazaar
    URI QtCreator.Bazaar
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      BazaarSettingsPage.qml
  )
endif()
