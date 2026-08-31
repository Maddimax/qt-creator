add_qtc_plugin(Squish
  PLUGIN_DEPENDS
    Core Debugger ProjectExplorer TextEditor
  DEPENDS ExtensionSystem Utils Qt::Qml QtcQuick
  SOURCES
    deletesymbolicnamedialog.cpp deletesymbolicnamedialog.h
    objectsmapdocument.cpp objectsmapdocument.h
    objectsmapeditor.cpp objectsmapeditor.h
    objectsmapeditorwidget.cpp objectsmapeditorwidget.h
    objectsmaptreeitem.cpp objectsmaptreeitem.h
    opensquishsuitesdialog.cpp opensquishsuitesdialog.h
    propertyitemdelegate.cpp propertyitemdelegate.h
    propertytreeitem.cpp propertytreeitem.h
    scripthelper.cpp scripthelper.h
    squish.qrc
    squishfilehandler.cpp squishfilehandler.h
    squishmessages.cpp squishmessages.h
    squishnavigationwidget.cpp squishnavigationwidget.h
    squishoutputpane.cpp squishoutputpane.h
    squishperspective.cpp squishperspective.h
    squishplugin.cpp
    squishprocessbase.cpp squishprocessbase.h
    squishresultmodel.cpp squishresultmodel.h
    squishrunnerprocess.cpp squishrunnerprocess.h
    squishsettings.cpp squishsettings.h
    squishserverprocess.cpp squishserverprocess.h
    squishtesttreemodel.cpp squishtesttreemodel.h
    squishtesttreeview.cpp squishtesttreeview.h
    squishtools.cpp squishtools.h
    squishtr.h
    squishwizardpages.cpp squishwizardpages.h
    squishxmloutputhandler.cpp squishxmloutputhandler.h
    suiteconf.cpp suiteconf.h
    symbolnameitemdelegate.cpp symbolnameitemdelegate.h
    testresult.cpp testresult.h
)

extend_qtc_plugin(Squish
  CONDITION WITH_TESTS
  DEPENDS Qt::Test
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET Squish)
  qt_add_qml_module(Squish
    URI QtCreator.Squish
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      SquishResults.qml
      DeleteSymbolicNameDialog.qml
      MappedAutDialog.qml
      OpenSquishSuitesDialog.qml
      SquishSettingsPage.qml
  )
endif()
