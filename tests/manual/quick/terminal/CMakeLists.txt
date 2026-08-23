add_qtc_test(tst_manual_quick_terminal
  MANUALTEST
  CONDITION TARGET TerminalQuick
  DEPENDS TerminalQuick TerminalModel Qt::Gui Qt::Qml Qt::Quick
  DEFINES RESULTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/results"
  SOURCES
    tst_manual_terminal.cpp
    editmenu.mm
    ptyhost.cpp ptyhost.h
    terminalmanual.qrc
)

if (TARGET tst_manual_quick_terminal)
  find_library(FWAppKit AppKit)
  target_link_libraries(tst_manual_quick_terminal PRIVATE ${FWAppKit})
endif()
