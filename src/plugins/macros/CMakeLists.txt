add_qtc_plugin(Macros
  PLUGIN_DEPENDS Core TextEditor
  SOURCES
    actionmacrohandler.cpp actionmacrohandler.h
    findmacrohandler.cpp findmacrohandler.h
    imacrohandler.cpp imacrohandler.h
    macro.cpp macro.h
    macroevent.cpp macroevent.h
    macrolocatorfilter.cpp macrolocatorfilter.h
    macromanager.cpp macromanager.h
    macrooptionspage.cpp macrooptionspage.h
    macros.qrc
    macrosconstants.h
    macrosplugin.cpp
    macrotextfind.cpp macrotextfind.h
    macrostr.h
    texteditormacrohandler.cpp texteditormacrohandler.h
)

extend_qtc_plugin(Macros
  CONDITION WITH_TESTS
  DEPENDS Qt::Test
  SOURCES
    macrooptions_test.cpp macrooptions_test.h
)

# The settings page's Qt Quick form. A module rather than a plain resource so
# that qmllint and qmlcachegen see the file; qbs cannot build QML modules, so it
# only lists it - the same split qtcquick already lives with.
if(TARGET Macros)
  qt_add_qml_module(Macros
    URI QtCreator.Macros
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    NO_PLUGIN
    QML_FILES
      MacroSettingsPage.qml
      SaveMacroDialog.qml
  )
endif()
