if (MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj")
elseif (MINGW)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wa,-mbig-obj")
endif()

add_qtc_plugin(LanguageClient
  DEPENDS McpServerLib Qt::Qml
  PUBLIC_DEPENDS LanguageServerProtocol Qt::Core
  PLUGIN_DEPENDS ProjectExplorer Core TextEditor
  SOURCES
    callandtypehierarchy.cpp callandtypehierarchy.h
    client.cpp client.h
    clientrequest.cpp clientrequest.h
    currentdocumentsymbolsrequest.cpp currentdocumentsymbolsrequest.h
    diagnosticmanager.cpp diagnosticmanager.h
    documentsymbolcache.cpp documentsymbolcache.h
    dynamiccapabilities.cpp dynamiccapabilities.h
    foldingrangesupport.cpp foldingrangesupport.h
    jsonmessagebox.cpp jsonmessagebox.h
    languageclientcompletionassist.cpp languageclientcompletionassist.h
    languageclientfunctionhint.cpp languageclientfunctionhint.h
    languageclientformatter.cpp languageclientformatter.h
    languageclienthoverhandler.cpp languageclienthoverhandler.h
    languageclientinterface.cpp languageclientinterface.h
    languageclientmanager.cpp languageclientmanager.h
    languageclientoutline.cpp languageclientoutline.h
    languageclientplugin.cpp
    languageclientquickfix.cpp languageclientquickfix.h
    languageclientsettings.cpp languageclientsettings.h
    languageclientsymbolsupport.cpp languageclientsymbolsupport.h
    languageclientutils.cpp languageclientutils.h
    languageclient_global.h
    languageclienttr.h
    locatorfilter.cpp locatorfilter.h
    lspinspector.cpp lspinspector.h
    mcptools.cpp mcptools.h
    mimetypesaspect.cpp mimetypesaspect.h
    progressmanager.cpp progressmanager.h
    semantichighlightsupport.cpp semantichighlightsupport.h
    snippet.cpp snippet.h
)

qt_add_resources(LanguageClient lc_images_rcc
  PREFIX "/languageclient"
  FILES
    images/languageclient.png
    images/languageclient@2x.png
    images/settingscategory_languageclient.png
    images/settingscategory_languageclient@2x.png
)

# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET LanguageClient)
  qt_add_qml_module(LanguageClient
    URI QtCreator.LanguageClient
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      JsonMessageEditor.qml
      LanguageClientProjectPanel.qml
      LanguageClientsPage.qml
      MimeTypeDialog.qml
  )
endif()

add_subdirectory(lualanguageclient)
