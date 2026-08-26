add_qtc_plugin(FakeVim
  DEPENDS McpServerLib
  PLUGIN_DEPENDS Core TextEditor
  PLUGIN_TEST_DEPENDS CppEditor
  SOURCES ${TEST_SOURCES}
    fakevim.qrc
    fakevimactions.cpp fakevimactions.h
    fakevimhandler.cpp fakevimhandler.h
    fakevimplugin.cpp
    fakevimtr.h
    mcpsupport.cpp mcpsupport.h
)

extend_qtc_plugin(FakeVim
  CONDITION WITH_TESTS
  SOURCES fakevim_test.cpp fakevim_test.h
)

if (MSVC)
  # Prevent fatal error C1128: both fakevimhandler.cpp and fakevim_test.cpp
  # hold more sections than the object format takes by default.
  target_compile_options(FakeVim PRIVATE /bigobj)
endif()
if (MINGW)
  target_compile_options(FakeVim PRIVATE "-Wa,-mbig-obj")
endif()

# Settings pages' Qt Quick forms. A module rather than a plain resource so that
# qmllint and qmlcachegen see the files; qbs cannot build QML modules, so it
# only lists them - the same split qtcquick already lives with.
if(TARGET FakeVim)
  qt_add_qml_module(FakeVim
    URI QtCreator.FakeVim
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      FakeVimExCommandsPage.qml
      FakeVimSettingsPage.qml
      FakeVimUserCommandsPage.qml
  )
endif()
