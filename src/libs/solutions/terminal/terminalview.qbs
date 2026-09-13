QtcLibrary {
    name: "TerminalLib"

    Depends { name: "TerminalModel" }
    Depends { name: "Qt.widgets" }

    cpp.defines: base.concat("TERMINALLIB_LIBRARY")

    files: [
        "boxdrawing.cpp",
        "boxdrawing.h",
        "glyphcache.cpp",
        "glyphcache.h",
        "terminal.qrc",
        "terminal_global.h",
        "terminalview.cpp",
        "terminalview.h",
    ]
}
