add_qtc_library(TerminalModel
  DEPENDS Qt::Gui libvterm
  SOURCES
    boxdrawing.cpp boxdrawing.h
    celliterator.cpp celliterator.h
    keys.cpp keys.h
    scrollback.cpp scrollback.h
    sixel.cpp sixel.h
    surfaceintegration.h
    terminaldefaults.cpp terminaldefaults.h
    terminalmodel_global.h
    terminalsurface.cpp terminalsurface.h
)

add_qtc_library(TerminalLib
  DEPENDS Qt::Widgets
  PUBLIC_DEPENDS TerminalModel
  SOURCES
    glyphcache.cpp glyphcache.h
    terminal_global.h
    terminal.qrc
    terminalview.cpp terminalview.h
)

add_qtc_library(TerminalQuick
  PUBLIC_DEPENDS TerminalModel Qt::Quick
  SOURCES
    terminalquick.qrc
    terminalquick_global.h
    terminalquickitem.cpp terminalquickitem.h
)
