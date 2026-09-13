add_qtc_plugin(QuickUi
  PLUGIN_DEPENDS Core
  DEPENDS QtcQuick Utils
  SOURCES
    quickoutputview.h
    quickuiplugin.cpp
)

extend_qtc_plugin(QuickUi
  CONDITION WITH_TESTS
  DEPENDS Qt::Test
  SOURCES
    quickui_test.cpp
    quickui_test.h
)
