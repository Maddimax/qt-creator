add_qtc_library(ExtensionSystem
  DEPENDS Utils
  PUBLIC_DEPENDS Qt::Core
  SOURCES
    extensionsystem_global.h
    extensionsystemtr.h
    invoker.cpp invoker.h
    iplugin.cpp iplugin.h
    optionsparser.cpp optionsparser.h
    pluginmanager.cpp pluginmanager.h pluginmanager_p.h
    pluginprompts.cpp pluginprompts.h
    pluginspec.cpp pluginspec.h
  EXPLICIT_MOC
    pluginmanager.h
  SKIP_AUTOMOC pluginmanager.cpp
)

# The plugin dialogs. Kept out of ExtensionSystem so that the plugin loader
# itself does not make QtWidgets a public dependency of every plugin.
add_qtc_library(ExtensionSystemWidgets
  PUBLIC_DEPENDS ExtensionSystem Qt::Widgets
  DEPENDS Utils
  SOURCES
    extensionsystemwidgets_global.h
    plugindetailsview.cpp plugindetailsview.h
    pluginenabling.cpp pluginenabling.h
    pluginerroroverview.cpp pluginerroroverview.h
    pluginerrorview.cpp pluginerrorview.h
    pluginview.cpp pluginview.h
    pluginwidgetprompts.cpp pluginwidgetprompts.h
)

# If ExtensionSystem was compiled with QtTest, it should provide the test options
# and the test related API, regardless if Qt Creator was compiled with tests or not,
# and regardless if an external plugin is compiled with tests or not.
# ExtensionSystem may not require QtTest in public headers though.
# API with EXTENSIONSYSTEM_TEST_EXPORT (like CppPluginSpec) should only be exported
# if Qt Creator is compiled with tests.
extend_qtc_library(ExtensionSystem
  CONDITION TARGET Qt::Test
  DEPENDS Qt::Test
  PUBLIC_DEFINES EXTENSIONSYSTEM_WITH_TESTOPTION
)

# Clearing QTestResult's leaked skip flag between test objects is what keeps
# one skipped suite from cancelling every later one, and there is no public
# API for it. Without the private headers the runner still works; it is just
# back to losing everything after a suite that skips in initTestCase.
extend_qtc_library(ExtensionSystem
  CONDITION TARGET Qt::TestPrivate
  DEPENDS Qt::TestPrivate
  DEFINES EXTENSIONSYSTEM_CAN_CLEAR_TEST_SKIP
)

extend_qtc_library(ExtensionSystem
  CONDITION TARGET Nanotrace
  PUBLIC_DEPENDS Nanotrace
)
