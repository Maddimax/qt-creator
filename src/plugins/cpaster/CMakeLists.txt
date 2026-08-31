add_subdirectory(frontend)

add_qtc_plugin(CodePaster
  PLUGIN_DEPENDS Core TextEditor
  DEFINES CPASTER_PLUGIN_GUI
  DEPENDS Qt::Network
  INCLUDES ../../shared/cpaster
  SOURCES
    codepasterservice.h
    columnindicatortextedit.cpp columnindicatortextedit.h
    cpasterconstants.h
    cpaster.qrc
    cpasterplugin.cpp
    cpastertr.h
    dpastedotcomprotocol.cpp dpastedotcomprotocol.h
    fileshareprotocol.cpp fileshareprotocol.h
    fileshareprotocolsettingspage.cpp fileshareprotocolsettingspage.h
    frontend/argumentscollector.cpp frontend/argumentscollector.h
    frontend/main.cpp
    pastebindotcomprotocol.cpp pastebindotcomprotocol.h
    pasteselectdialog.cpp pasteselectdialog.h
    pasteview.cpp pasteview.h
    protocol.cpp protocol.h
    settings.cpp settings.h
    urlopenprotocol.cpp urlopenprotocol.h

    ../../shared/cpaster/cgi.cpp ../../shared/cpaster/cgi.h
    ../../shared/cpaster/splitter.cpp ../../shared/cpaster/splitter.h
)

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET CodePaster)
  qt_add_qml_module(CodePaster
    URI QtCreator.CodePaster
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      CodePasterSettingsPage.qml
      PasteView.qml
      PasteSelectDialog.qml
      FileShareSettingsPage.qml
  )
endif()
